/*
 * motor_modbus.h - Thư viện ESP32 (ESP-IDF) làm Modbus MASTER điều khiển mạch CM8
 *
 * Viết lại theo ý tưởng file control_motor.cpp (Arduino) anh hướng dẫn gửi:
 *   - Liên tục GHI khối thanh ghi 0..14 (FC16): lệnh, tốc độ, giới hạn dòng, mode, ratio
 *   - Liên tục ĐỌC trạng thái (FC03): lỗi, thẻ, DAC, dòng điện...
 *   => CM8 luôn nhận được tin nhắn nên KHÔNG báo lỗi 2 (mất kết nối).
 *
 * Việc gửi/nhận chạy trong task FreeRTOS riêng "motor", nên task khác không bị chặn.
 *
 * Cách dùng:
 *   motor_start();                                  // 1 lần lúc khởi động
 *   motor_set(lenh, speed, gioi_han_dong, mode, ratio);   // đổi giá trị bất cứ lúc nào
 *   motor_status_t t = motor_get_status();    // đọc trạng thái mới nhất
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---- Chân / thông số (đổi ở đây nếu cần) ----
#define MOTOR_UART              UART_NUM_1
#define MOTOR_TX_PIN                32            // -> module SP3232 RXD
#define MOTOR_RX_PIN                33            // <- module SP3232 TXD
#define MOTOR_BAUD                  9600
#define MOTOR_SLAVE_ID                    1

// ---- Giá trị lệnh (thanh ghi 0) ----
#define MOTOR_CMD_RUN               1             // bật quạt + bật nguồn công suất + chạy theo tốc độ đặt
#define MOTOR_CMD_STOP              2             // giảm tốc về 0, tắt quạt

typedef struct {
    bool     connected;        // true = CM8 đang trả lời
    uint16_t cmd;           // reg 0  (CM8 đang giữ)
    uint16_t speed;         // reg 6
    uint16_t mode, ratio;    // reg 8, 9
    uint16_t upper_mv;     // reg 4  upper_setting (mV)
    uint16_t lower_mv;    // reg 5  lower_setting (mV)
    uint16_t error;            // reg 17 0 OK, 2 mất kết nối, 3 quá dòng, 4 quá áp
    uint16_t card;            // reg 18 trạng thái thẻ / IN1
    uint16_t dac_mv;         // reg 19 điện áp DAC (mV) - càng THẤP motor càng NHANH
    uint16_t current_x100;      // reg 22 dòng điện x100 (A)
    uint16_t overcurrent_count;   // reg 23
    uint16_t run_state;     // reg 28 state_run: 0 dừng, 1 khởi động mềm, 2 đang chạy
    uint32_t ok_count;      // số giao dịch thành công
    uint32_t fail_count;     // số giao dịch không có phản hồi / sai CRC
    uint32_t rx_bytes_total; // tổng số byte nhận được trên chân RX (kể cả rác)
    uint8_t  sample[8];         // vài byte nhận được gần nhất (để chẩn đoán)
    uint8_t  sample_len;
    bool     echo;       // true = nhận lại đúng khung mình gửi (TX nối vòng về RX)
} motor_status_t;

void motor_start(void);
void motor_set(uint16_t cmd, uint16_t speed, uint16_t current_limit_x100,
             uint16_t mode, uint16_t ratio);
motor_status_t motor_get_status(void);
const char *motor_error_name(uint16_t error);
