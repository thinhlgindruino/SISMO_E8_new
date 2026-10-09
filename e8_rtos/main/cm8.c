/*
 * cm8.c - Task Modbus RTU master cho mạch CM8 (xem cm8.h)
 *   - Chưa có lệnh (cm8_set chưa được gọi) thì chỉ ĐỌC, để task điều khiển biết
 *     CM8 vừa bật hay đã chạy từ trước. Đọc cũng tính là tin nhắn nên CM8 không báo lỗi 2.
 *   - Mỗi vòng báo watchdog "còn sống".
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "config.h"
#include "cm8.h"

#define TIMEOUT_MS        300    // chờ CM8 trả lời tối đa
#define GAP_MS      50     // nghỉ giữa 2 giao dịch (giống control_motor: ~100 ms/chu kỳ)
#define LINK_LOST_COUNT     5      // 5 lần liên tiếp không trả lời => báo mất kết nối

#define WRITE_REG_COUNT        15     // ghi reg 0..14 (giống telegram[0] của control_motor)
#define READ_REG_COUNT        29     // đọc reg 0..28

// ---- Giá trị muốn gửi (main đặt) ----
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t set_cmd = CM8_CMD_STOP, set_speed = 0, set_current = 0, set_mode = 0, set_ratio = 0;
static bool     has_cmd = false;      // đã có lệnh từ task điều khiển chưa

// ---- Trạng thái đọc về ----
static cm8_status_t status;

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

static bool crc_ok(const uint8_t *d, int n)
{
    return n >= 4 && (uint16_t)(d[n - 2] | (d[n - 1] << 8)) == crc16_modbus(d, n - 2);
}

// Gửi khung (tự thêm CRC), chờ đủ 'expected' byte. Trả về số byte nhận được.
static int transact(uint8_t *frame, int n, uint8_t *reply, int expected)
{
    uint16_t c = crc16_modbus(frame, n);
    frame[n++] = c & 0xFF;
    frame[n++] = c >> 8;

    uart_flush_input(CM8_UART_NUM);
    vTaskDelay(pdMS_TO_TICKS(5));                       // khoảng lặng > 3,5 ký tự
    uart_write_bytes(CM8_UART_NUM, (const char *)frame, n);
    uart_wait_tx_done(CM8_UART_NUM, pdMS_TO_TICKS(100));

    int got = 0;
    int64_t t0 = ms_now();
    while (got < expected && ms_now() - t0 < TIMEOUT_MS) {
        int k = uart_read_bytes(CM8_UART_NUM, reply + got, expected - got, pdMS_TO_TICKS(20));
        if (k > 0) got += k;
    }

    // Ghi lại để chẩn đoán: nhận được bao nhiêu byte, có phải đang nghe lại chính mình không
    if (got > 0) {
        portENTER_CRITICAL(&lock);
        status.rx_bytes_total += got;
        status.sample_len = got < 8 ? got : 8;
        memcpy(status.sample, reply, status.sample_len);
        if (frame[1] == 0x03)   // chỉ xét với lệnh đọc (trả lời thật bắt đầu 01 03 3A..., khác khung gửi)
            status.echo = (got >= 6 && memcmp(reply, frame, 6) == 0);
        portEXIT_CRITICAL(&lock);
    }
    return got;
}

// FC16: ghi reg 0..14 một lần (lệnh, tốc độ, giới hạn dòng, mode, ratio; còn lại = 0)
static bool write_command_block(void)
{
    uint16_t v[WRITE_REG_COUNT] = {0};
    portENTER_CRITICAL(&lock);
    v[0] = set_cmd;
    v[3] = set_current;
    v[6] = set_speed;
    v[8] = set_mode;
    v[9] = set_ratio;
    portEXIT_CRITICAL(&lock);

    uint8_t f[7 + 2 * WRITE_REG_COUNT + 2] = {CM8_ID, 0x10, 0x00, 0x00, 0x00, WRITE_REG_COUNT, 2 * WRITE_REG_COUNT};
    int n = 7;
    for (int i = 0; i < WRITE_REG_COUNT; i++) { f[n++] = v[i] >> 8; f[n++] = v[i] & 0xFF; }

    uint8_t r[8];
    int k = transact(f, n, r, 8);
    return k == 8 && crc_ok(r, 8) && r[0] == CM8_ID && r[1] == 0x10;
}

// FC03: đọc reg 0..28
static bool read_status(void)
{
    uint8_t f[8] = {CM8_ID, 0x03, 0x00, 0x00, 0x00, READ_REG_COUNT};
    uint8_t r[5 + 2 * READ_REG_COUNT];
    int need = sizeof(r);
    int k = transact(f, 6, r, need);
    if (k != need || !crc_ok(r, k) || r[1] != 0x03 || r[2] != 2 * READ_REG_COUNT) return false;

    uint16_t g[READ_REG_COUNT];
    for (int i = 0; i < READ_REG_COUNT; i++) g[i] = (r[3 + 2 * i] << 8) | r[4 + 2 * i];

    portENTER_CRITICAL(&lock);
    status.cmd = g[0];        status.speed = g[6];
    status.mode = g[8];        status.ratio = g[9];
    status.upper_mv = g[4];  status.lower_mv = g[5];
    status.error = g[17];        status.card = g[18];
    status.dac_mv = g[19];     status.current_x100 = g[22];
    status.overcurrent_count = g[23];
    status.run_state = g[28];
    portEXIT_CRITICAL(&lock);
    return true;
}

static void update_counters(bool ok, int *fail_streak)
{
    portENTER_CRITICAL(&lock);
    if (ok) { status.ok_count++; *fail_streak = 0; status.connected = true; }
    else {
        status.fail_count++;
        if (++(*fail_streak) >= LINK_LOST_COUNT) status.connected = false;
    }
    portEXIT_CRITICAL(&lock);
}

static void task_cm8(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    int fail_streak = 0;
    while (1) {
        // Luân phiên GHI rồi ĐỌC (sửa lỗi u8query của control_motor: chỉ có 2 telegram 0 và 1)
        portENTER_CRITICAL(&lock);
        bool do_write = has_cmd;
        portEXIT_CRITICAL(&lock);
        if (do_write) {
            update_counters(write_command_block(), &fail_streak);
            vTaskDelay(pdMS_TO_TICKS(GAP_MS));
        }
        update_counters(read_status(), &fail_streak);
        vTaskDelay(pdMS_TO_TICKS(GAP_MS));
        esp_task_wdt_reset();               // 1 vòng tối đa ~0,8 s (2 lần chờ 300 ms) < 3 s
    }
}

void cm8_begin(void)
{
    const uart_config_t cfg = {
        .baud_rate  = CM8_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(CM8_UART_NUM, 512, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CM8_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(CM8_UART_NUM, CM8_TX_PIN, CM8_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreatePinnedToCore(task_cm8, "cm8", STACK_CM8, NULL, PRIO_CM8, NULL, APP_CORE);
}

void cm8_set(uint16_t cmd, uint16_t speed, uint16_t current_limit_x100,
             uint16_t mode, uint16_t ratio)
{
    if (speed > 32) speed = 32;
    portENTER_CRITICAL(&lock);
    set_cmd = cmd;
    set_speed = speed;
    set_current = current_limit_x100;
    set_mode = mode;
    set_ratio = ratio;
    has_cmd = true;
    portEXIT_CRITICAL(&lock);
}

cm8_status_t cm8_get_status(void)
{
    portENTER_CRITICAL(&lock);
    cm8_status_t t = status;
    portEXIT_CRITICAL(&lock);
    return t;
}

const char *cm8_error_name(uint16_t error)
{
    switch (error) {
    case 0:  return "OK";
    case 2:  return "MAT KET NOI";
    case 3:  return "QUA DONG";
    case 4:  return "QUA AP";
    default: return "?";
    }
}
