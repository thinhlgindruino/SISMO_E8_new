/*
 * motor_modbus.h - ESP32 làm Modbus RTU MASTER điều khiển mạch CM8 (task "motor")
 *
 * Nối dây: UART1 9600 baud qua module SP3232 (RS-232):
 *   GPIO32 (TX) -> SP3232 RXD, GPIO33 (RX) <- SP3232 TXD, GND chung. CM8 là slave ID 1.
 *
 * Viết lại theo ý tưởng file control_motor.cpp (Arduino) anh hướng dẫn gửi:
 *   - Liên tục GHI khối thanh ghi 0..14 (FC16): lệnh, giới hạn dòng, tốc độ, mode, ratio
 *   - Liên tục ĐỌC trạng thái 0..28 (FC03): lỗi, thẻ, DAC, dòng điện, trạng thái chạy
 *   => CM8 luôn nhận được tin nhắn nên KHÔNG báo lỗi 2 (mất kết nối).
 *
 * Thanh ghi CM8:
 *   0 lệnh (1 = chạy: bật quạt + nguồn công suất, 2 = dừng: giảm tốc về 0, tắt quạt)
 *   3 giới hạn dòng ×100, 6 tốc độ 0..32, 8 mode, 9 ratio,
 *   17 lỗi (0 OK, 2 mất kết nối, 3 quá dòng, 4 quá áp), 18 thẻ / IN1,
 *   19 DAC mV (càng THẤP motor càng NHANH), 22 dòng điện ×100, 23 số lần quá dòng,
 *   28 trạng thái (0 dừng, 1 khởi động mềm, 2 đang chạy).
 * 5 giao dịch liên tiếp không trả lời -> connected = false.
 *
 * Cách dùng:
 *   motor_start()                                    1 lần lúc khởi động
 *   motor_set(cmd, speed, current_limit, mode, ratio) đổi giá trị bất cứ lúc nào (task motor tự gửi)
 *   motor_get_status()                               đọc trạng thái mới nhất
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MOTOR_UART              UART_NUM_1
#define MOTOR_TX_PIN                32
#define MOTOR_RX_PIN                33
#define MOTOR_BAUD                  9600
#define MOTOR_SLAVE_ID                    1

#define MOTOR_CMD_RUN               1
#define MOTOR_CMD_STOP              2

typedef struct {
    bool     connected;
    uint16_t cmd;
    uint16_t speed;
    uint16_t mode, ratio;
    uint16_t upper_mv;
    uint16_t lower_mv;
    uint16_t error;
    uint16_t card;
    uint16_t dac_mv;
    uint16_t current_x100;
    uint16_t overcurrent_count;
    uint16_t run_state;
    uint32_t ok_count;
    uint32_t fail_count;
    uint32_t rx_bytes_total;
    uint8_t  sample[8];
    uint8_t  sample_len;
    bool     echo;
} motor_status_t;

void motor_start(void);
void motor_set(uint16_t cmd, uint16_t speed, uint16_t current_limit_x100,
             uint16_t mode, uint16_t ratio);
motor_status_t motor_get_status(void);
const char *motor_error_name(uint16_t error);
