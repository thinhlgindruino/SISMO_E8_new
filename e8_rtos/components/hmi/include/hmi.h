/*
 * hmi.h - Giao tiếp màn hình HMI (LT7689, UI_Editor-II) qua UART2, bằng 2 task:
 *   hmi_rx   : nhận + giải mã khung từ màn hình, cập nhật dữ liệu chung, phát sự kiện nút bấm
 *   hmi_poll : hỏi 0x1011/0x1012 mỗi 100 ms, biến 0x1000..0x1009 và trang (0x7000) xen kẽ
 *
 * Nối dây (board WT32-ETH01): GPIO36 (RX ESP32, chân IO36) <- TXA màn hình, GPIO17 (TX ESP32, chân TXD) -> RXB
 * màn hình, GND chung. GPIO16 không dùng được nữa vì board dùng để bật bộ dao động 50 MHz của LAN.
 * 115200 baud. Quá HMI_LINK_TIMEOUT_MS (2 s) không nhận được dữ liệu -> coi như mất màn hình.
 * Mỗi lần có lại liên lạc với màn hình: ghi âm lượng loa HMI_VOLUME (0..16) vào thanh ghi 0x700B
 * (loa rời cắm CON6, phát WAV; tiếng "tách" khi chạm là còi BZ1, không theo âm lượng này).
 *
 * Khung: 5A A5 | độ dài | lệnh | addr H L | data ... | CRC16 Modbus (L H)
 *   0x10 ghi biến, 0x03 đọc biến, 0x41 màn hình tự báo khi bấm nút (reportToHost)
 *
 * Sự kiện nút (hmi_event_t):
 *   EV_SRC_REPORT  màn hình tự báo 0x41 - với nút longPress, màn hình chỉ báo lúc THẢ TAY
 *   EV_SRC_POLL    ESP32 tự đọc thấy giá trị đổi (0x1011 phím, 0x1012 Sismo) - biết được lúc đang giữ
 * Module này KHÔNG biết trang nào xử lý nút nào: mọi sự kiện được chuyển cho hàm do ui_router
 * đăng ký (hmi_set_event_handler). Hàm đó chạy trong task hmi_rx nên phải ngắn, không chờ.
 *
 * hmi_get() trả về bản sao dữ liệu mới nhất (an toàn giữa các task); hmi_write() gọi từ task nào
 * cũng được (uart_write_bytes có khóa riêng nên không bị xen byte).
 * hmi_write_words / hmi_read_words: ghi nhiều word, đọc vùng bất kỳ (không gọi hmi_read_words trong
 * task hmi_rx).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hmi_map.h"

#define HMI_UART                  UART_NUM_2
#define HMI_RX_PIN                36
#define HMI_TX_PIN                17
#define HMI_BAUD                  115200
#define HMI_LINK_TIMEOUT_MS       2000
#define HMI_MAX_WORDS             120
#define HMI_VOLUME                3

typedef struct {
    int      page;
    uint16_t vars[HMI_VAR_COUNT];
    bool     has_vars;
    int64_t  last_rx_ms;
} hmi_data_t;

typedef enum { EV_SRC_REPORT = 0, EV_SRC_POLL = 1 } hmi_source_t;
typedef struct { uint16_t addr; uint16_t value; uint8_t source; } hmi_event_t;
typedef void (*hmi_event_handler_t)(const hmi_event_t *e);

void       hmi_start(void);
void       hmi_set_event_handler(hmi_event_handler_t handler);
hmi_data_t hmi_get(void);
bool       hmi_is_linked(const hmi_data_t *d);
void       hmi_write(uint16_t addr, uint16_t value);
void       hmi_goto_page(int page);
void       hmi_write_words(uint16_t addr, const uint16_t *v, int n);
bool       hmi_read_words(uint16_t addr, int n, uint16_t *out, int timeout_ms);

typedef struct {
    uint32_t rx_bytes;
    uint32_t frames_ok;
    uint32_t crc_fail;
    uint8_t  last[12];
    uint8_t  last_len;
} hmi_diag_t;
hmi_diag_t hmi_get_diag(void);
