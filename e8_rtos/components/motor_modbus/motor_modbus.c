/*
 * motor_modbus.c - Task "motor": Modbus RTU master cho mạch CM8 (xem motor_modbus.h)
 *
 * - Mỗi giao dịch: xóa bộ đệm nhận, nghỉ > 3,5 ký tự, gửi khung + CRC16, chờ đủ số byte trả lời
 *   (tối đa 300 ms), kiểm tra CRC. Nghỉ 50 ms giữa 2 giao dịch.
 * - Luân phiên GHI (FC16, reg 0..14) rồi ĐỌC (FC03, reg 0..28). Chưa có lệnh (motor_set chưa được
 *   gọi) thì chỉ ĐỌC, để task session biết CM8 vừa bật hay đã chạy từ trước; đọc cũng tính là
 *   tin nhắn nên CM8 không báo lỗi 2.
 * - Lưu vài byte nhận gần nhất + cờ "nghe lại chính mình" (TX nối vòng về RX) để chẩn đoán dây.
 * - Giá trị đặt / trạng thái đọc về dùng chung giữa các task qua critical section.
 * - Mỗi vòng báo watchdog "còn sống" (1 vòng tối đa ~0,8 s < 3 s).
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "app_config.h"
#include "motor_modbus.h"

#define TIMEOUT_MS        300
#define GAP_MS      50
#define LINK_LOST_COUNT     5

#define WRITE_REG_COUNT        15
#define READ_REG_COUNT        29

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t set_cmd = MOTOR_CMD_STOP, set_speed = 0, set_current = 0, set_mode = 0, set_ratio = 0;
static bool     has_cmd = false;

static motor_status_t status;

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

static int transact(uint8_t *frame, int n, uint8_t *reply, int expected)
{
    uint16_t c = crc16_modbus(frame, n);
    frame[n++] = c & 0xFF;
    frame[n++] = c >> 8;

    uart_flush_input(MOTOR_UART);
    vTaskDelay(pdMS_TO_TICKS(5));
    uart_write_bytes(MOTOR_UART, (const char *)frame, n);
    uart_wait_tx_done(MOTOR_UART, pdMS_TO_TICKS(100));

    int got = 0;
    int64_t t0 = ms_now();
    while (got < expected && ms_now() - t0 < TIMEOUT_MS) {
        int k = uart_read_bytes(MOTOR_UART, reply + got, expected - got, pdMS_TO_TICKS(20));
        if (k > 0) got += k;
    }

    if (got > 0) {
        portENTER_CRITICAL(&lock);
        status.rx_bytes_total += got;
        status.sample_len = got < 8 ? got : 8;
        memcpy(status.sample, reply, status.sample_len);
        if (frame[1] == 0x03)
            status.echo = (got >= 6 && memcmp(reply, frame, 6) == 0);
        portEXIT_CRITICAL(&lock);
    }
    return got;
}

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

    uint8_t f[7 + 2 * WRITE_REG_COUNT + 2] = {MOTOR_SLAVE_ID, 0x10, 0x00, 0x00, 0x00, WRITE_REG_COUNT, 2 * WRITE_REG_COUNT};
    int n = 7;
    for (int i = 0; i < WRITE_REG_COUNT; i++) { f[n++] = v[i] >> 8; f[n++] = v[i] & 0xFF; }

    uint8_t r[8];
    int k = transact(f, n, r, 8);
    return k == 8 && crc_ok(r, 8) && r[0] == MOTOR_SLAVE_ID && r[1] == 0x10;
}

static bool read_status(void)
{
    uint8_t f[8] = {MOTOR_SLAVE_ID, 0x03, 0x00, 0x00, 0x00, READ_REG_COUNT};
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

static void task_motor(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    int fail_streak = 0;
    while (1) {
        portENTER_CRITICAL(&lock);
        bool do_write = has_cmd;
        portEXIT_CRITICAL(&lock);
        if (do_write) {
            update_counters(write_command_block(), &fail_streak);
            vTaskDelay(pdMS_TO_TICKS(GAP_MS));
        }
        update_counters(read_status(), &fail_streak);
        vTaskDelay(pdMS_TO_TICKS(GAP_MS));
        esp_task_wdt_reset();
    }
}

void motor_start(void)
{
    const uart_config_t cfg = {
        .baud_rate  = MOTOR_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(MOTOR_UART, 512, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MOTOR_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MOTOR_UART, MOTOR_TX_PIN, MOTOR_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreatePinnedToCore(task_motor, "motor", STACK_MOTOR, NULL, PRIO_MOTOR, NULL, APP_CORE);
}

void motor_set(uint16_t cmd, uint16_t speed, uint16_t current_limit_x100,
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

motor_status_t motor_get_status(void)
{
    portENTER_CRITICAL(&lock);
    motor_status_t t = status;
    portEXIT_CRITICAL(&lock);
#if DEBUG_FAKE_CM8_ERROR
    if (t.connected && esp_timer_get_time() / 1000 >= DEBUG_FAKE_CM8_ERROR_AFTER_MS) t.error = DEBUG_FAKE_CM8_ERROR;
#endif
    return t;
}

const char *motor_error_name(uint16_t error)
{
    switch (error) {
    case 0:  return "OK";
    case 2:  return "MAT KET NOI";
    case 3:  return "QUA DONG";
    case 4:  return "QUA AP";
    default: return "?";
    }
}
