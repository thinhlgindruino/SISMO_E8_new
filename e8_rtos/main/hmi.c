/*
 * hmi.c - Task nhận / task hỏi màn hình HMI (xem hmi.h)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "config.h"
#include "hmi.h"
#include "logger.h"

static SemaphoreHandle_t lock;          // mutex bảo vệ 'dl'
static hmi_data_t shared = { .page = -1 };

// Hàng đợi các lần người dùng bấm +/- (Touch Returned Message của biến 0x1000).
// Task điều khiển cần biết từng lần bấm khi đang Xung (lúc đó 0x1000 do ESP32 ghi dao động).
static QueueHandle_t touch_queue;
static QueueHandle_t event_queue;   // bấm nút của phần cài đặt

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

static uint16_t crc16_modbus(const uint8_t *d, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c & 1) ? (c >> 1) ^ 0xA001 : (c >> 1);
    }
    return c;
}

// Khung 10 byte: 5A A5 07 | lệnh | addr H L | data H L | CRC L H
// uart_write_bytes của ESP-IDF có khóa riêng nên nhiều task cùng gửi không bị xen byte.
static void send_frame(uint8_t cmd, uint16_t addr, uint16_t value)
{
    uint8_t f[10] = {0x5A, 0xA5, 0x07, cmd,
                     (uint8_t)(addr >> 8), (uint8_t)addr,
                     (uint8_t)(value >> 8), (uint8_t)value, 0, 0};
    uint16_t c = crc16_modbus(&f[3], 5);
    f[8] = c & 0xFF;
    f[9] = c >> 8;
    uart_write_bytes(HMI_UART, (const char *)f, sizeof(f));
}

void hmi_write(uint16_t addr, uint16_t value) { send_frame(0x10, addr, value); }

bool hmi_get_intensity_touch(uint16_t *value)
{
    return xQueueReceive(touch_queue, value, 0) == pdTRUE;
}

bool hmi_get_event(hmi_event_t *e, TickType_t wait)
{
    return xQueueReceive(event_queue, e, wait) == pdTRUE;
}

hmi_data_t hmi_get(void)
{
    hmi_data_t d;
    xSemaphoreTake(lock, portMAX_DELAY);
    d = shared;
    xSemaphoreGive(lock);
    return d;
}

bool hmi_is_linked(const hmi_data_t *d)
{
    return d->has_vars && (ms_now() - d->last_rx_ms < HMI_LINK_TIMEOUT_MS);
}

// ---------------- Chẩn đoán: in mỗi lần cường độ đổi ----------------
static void log_intensity_change(uint16_t prev, uint16_t next, const char *source)
{
#if DEBUG_INTENSITY
    static int64_t last_ms = 0;
    int64_t now_ms = ms_now();
    if (prev != next) {
        log_send("   [HMI] cuong do %u -> %u (%+d) | cach lan truoc %lld ms | %s",
                    prev + 1, next + 1, (int)next - (int)prev, (long long)(now_ms - last_ms), source);
        last_ms = now_ms;
    }
#else
    (void)prev; (void)next; (void)source;
#endif
}

// ---------------- Giải mã khung nhận được ----------------
static void handle_frame(const uint8_t *p, uint16_t n)
{
    if (n < 3) return;
    uint16_t rx_crc = p[n - 2] | (p[n - 1] << 8);
    if (rx_crc != crc16_modbus(p, n - 2)) return;

    // Kết quả đọc: 03 | addr(2) | số word(2) | data | CRC
    if (p[0] == 0x03 && n >= 7) {
        uint16_t addr = (p[1] << 8) | p[2];
        uint16_t word_count = (p[3] << 8) | p[4];
        if (n != 5 + 2 * word_count + 2) return;
        // 0x1011 (phím mật khẩu) + 0x1012 (nút Sismo), đọc chung 1 khung
        if (addr == ADDR_PASSWORD_KEY && word_count == 2) {
            uint16_t key  = (p[5] << 8) | p[6];
            uint16_t sismo_val = (p[7] << 8) | p[8];

            // Phím: ESP32 ghi KEY_EMPTY sau mỗi lần đọc được phím -> bấm lại cùng phím vẫn thấy đổi.
            // Nhanh hơn chờ 0x41 (màn hình chỉ báo 0x41 lúc thả tay).
            static bool     first_read_done = false;
            static bool     clear_pending = false;         // đã ghi KEY_EMPTY, chờ đọc thấy nó
            static uint16_t last_key = KEY_EMPTY;
            if (!first_read_done) {                   // lần đầu: giá trị còn sót -> chỉ xóa, không tính là bấm
                first_read_done = true;
                if (key != KEY_EMPTY) {
                    send_frame(0x10, ADDR_PASSWORD_KEY, KEY_EMPTY);
                    clear_pending = true;
                    last_key = key;
                }
            } else if (key == KEY_EMPTY) {
                clear_pending = false;
            } else if (!clear_pending || key != last_key) {    // phím mới
                hmi_event_t e = { .addr = ADDR_PASSWORD_KEY, .value = key, .source = EV_SRC_POLL };
                xQueueSend(event_queue, &e, 0);
                send_frame(0x10, ADDR_PASSWORD_KEY, KEY_EMPTY);
                clear_pending = true;
                last_key = key;
            } else {
                send_frame(0x10, ADDR_PASSWORD_KEY, KEY_EMPTY);   // lệnh xóa trước bị mất -> ghi lại
            }

            // Sismo: giá trị đổi so với lần đọc trước = đang bấm / đang giữ
            static int32_t last_sismo = -1;
            if (last_sismo >= 0 && sismo_val != (uint16_t)last_sismo) {
                hmi_event_t e = { .addr = ADDR_SISMO_BUTTON, .value = sismo_val, .source = EV_SRC_POLL };
                xQueueSend(event_queue, &e, 0);
            }
            last_sismo = sismo_val;
            return;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        uint16_t prev_intensity = shared.vars[0];
        bool got_intensity = false;
        for (int i = 0; i < word_count; i++) {
            uint16_t v = (p[5 + 2 * i] << 8) | p[6 + 2 * i];
            uint16_t a = addr + i;
            if (a == 0x7000) shared.page = v;
            else if (a >= 0x1000 && a < 0x1000 + HMI_VAR_COUNT) {
                shared.vars[a - 0x1000] = v;
                shared.has_vars = true;
                if (a == 0x1000) got_intensity = true;
            }
        }
        shared.last_rx_ms = ms_now();
        uint16_t new_intensity = shared.vars[0];
        xSemaphoreGive(lock);
        if (got_intensity) log_intensity_change(prev_intensity, new_intensity, "doc dinh ky");
    }
    // Touch Returned Message: 41 | addr(2) | giá trị(2)
    else if (p[0] == 0x41 && n == 7) {
        uint16_t addr = (p[1] << 8) | p[2];
        uint16_t v = (p[3] << 8) | p[4];
        if (addr >= 0x1000 && addr < 0x1000 + HMI_VAR_COUNT) {
            xSemaphoreTake(lock, portMAX_DELAY);
            uint16_t prev_intensity = shared.vars[0];
            shared.vars[addr - 0x1000] = v;
            xSemaphoreGive(lock);
            if (addr == 0x1000) {
                xQueueSend(touch_queue, &v, 0);
                log_intensity_change(prev_intensity, v, "man hinh tu bao (0x41)");
            }
        } else if (addr == ADDR_SISMO_BUTTON || addr == ADDR_SETTINGS_BUTTON) {
            // (phím mật khẩu 0x1011 KHÔNG lấy từ 0x41 nữa - đã đọc trực tiếp ở trên, lấy cả 2 sẽ bị đếm đôi)
            hmi_event_t e = { .addr = addr, .value = v, .source = EV_SRC_REPORT };
            xQueueSend(event_queue, &e, 0);
        }
    }
}

typedef enum { WAIT_5A, WAIT_A5, WAIT_LEN, READ_BODY } parse_state_t;
static parse_state_t   st = WAIT_5A;
static uint8_t  buf[260];
static uint16_t frame_len = 0, idx = 0;

static void rx_byte(uint8_t c)
{
    switch (st) {
    case WAIT_5A:  if (c == 0x5A) st = WAIT_A5; break;
    case WAIT_A5:  st = (c == 0xA5) ? WAIT_LEN : (c == 0x5A ? WAIT_A5 : WAIT_5A); break;
    case WAIT_LEN: frame_len = c; idx = 0; st = (frame_len > 0) ? READ_BODY : WAIT_5A; break;
    case READ_BODY:
        buf[idx++] = c;
        if (idx >= frame_len) { handle_frame(buf, frame_len); st = WAIT_5A; }
        break;
    }
}

// ---------------- Task nhận: ngủ cho tới khi có byte về ----------------
static void task_hmi_rx(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    uint8_t rx[128];
    while (1) {
        // Chờ byte ĐẦU TIÊN (tối đa 100 ms), rồi lấy hết phần đã có sẵn, không chờ thêm.
        // (Không gọi uart_read_bytes(..., 128, ...): hàm đó chờ đủ 128 byte, mà màn hình trả lời
        //  liên tục nên ~300 ms mới đủ -> mọi dữ liệu màn hình bị trễ tới 300 ms.)
        int n = uart_read_bytes(HMI_UART, rx, 1, pdMS_TO_TICKS(100));
        if (n > 0) {
            size_t avail = 0;
            uart_get_buffered_data_len(HMI_UART, &avail);
            if (avail > sizeof(rx) - 1) avail = sizeof(rx) - 1;
            if (avail) {
                int m = uart_read_bytes(HMI_UART, rx + 1, avail, 0);
                if (m > 0) n += m;
            }
        }
        for (int i = 0; i < n; i++) rx_byte(rx[i]);
        esp_task_wdt_reset();               // báo watchdog "task này còn sống"
    }
}

// ---------------- Task hỏi ----------------
// Mỗi nửa chu kỳ (50 ms) gửi 1 câu hỏi, xen kẽ:
//   - phím mật khẩu 0x1011 + nút Sismo 0x1012 (mỗi 100 ms)
//   - biến 0x1000..0x1009 / trang 0x7000 như cũ
// Hỏi thưa vừa đủ: đồng hồ cường độ do màn hình tự vẽ, việc bấm +/- màn hình tự báo (0x41).
static void task_hmi_poll(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    TickType_t wake = xTaskGetTickCount();
    const int page_poll_every = HMI_PAGE_POLL_MS / HMI_VAR_POLL_MS;
    int counter = 0;
    bool sismo_turn = false;
    while (1) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(HMI_VAR_POLL_MS / 2));   // chu kỳ đều, không trôi
        sismo_turn = !sismo_turn;
        if (sismo_turn) {
            send_frame(0x03, ADDR_PASSWORD_KEY, 2);    // 0x1011, 0x1012
        } else if (++counter >= page_poll_every) {
            counter = 0;
            send_frame(0x03, 0x7000, 1);
        } else {
            send_frame(0x03, 0x1000, HMI_VAR_COUNT);    // 0x1000..0x1009
        }
        esp_task_wdt_reset();

#if TEST_TASK_HANG
        if (ms_now() > 20000) {
            log_send("!! THU: task hmi_poll bi treo (watchdog se khoi dong lai sau %d s)", WDT_TIMEOUT_MS / 1000);
            while (1) vTaskDelay(pdMS_TO_TICKS(1000));   // treo: không còn báo watchdog
        }
#endif
    }
}

void hmi_start(void)
{
    lock = xSemaphoreCreateMutex();
    touch_queue = xQueueCreate(16, sizeof(uint16_t));
    event_queue = xQueueCreate(16, sizeof(hmi_event_t));

    const uart_config_t cfg = {
        .baud_rate  = HMI_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(HMI_UART, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(HMI_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(HMI_UART, HMI_TX_PIN, HMI_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreatePinnedToCore(task_hmi_rx, "hmi_rx", STACK_HMI, NULL, PRIO_HMI_RX, NULL, APP_CORE);
    xTaskCreatePinnedToCore(task_hmi_poll,  "hmi_poll",  STACK_HMI, NULL, PRIO_HMI_POLL,  NULL, APP_CORE);
}
