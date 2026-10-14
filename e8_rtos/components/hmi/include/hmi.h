/*
 * hmi.h - Giao tiếp màn hình HMI (LT7689, UI_Editor-II) qua UART, bằng 2 task:
 *   hmi_rx   : nhận + giải mã khung từ màn hình, cập nhật dữ liệu chung, phát sự kiện nút bấm
 *   hmi_poll : hỏi 0x1011/0x1012 mỗi 100 ms, biến 0x1000..0x1009 và trang (0x7000) xen kẽ
 *
 * Khung: 5A A5 | độ dài | lệnh | addr H L | data ... | CRC16 Modbus (L H)
 *   0x10 ghi biến, 0x03 đọc biến, 0x41 màn hình tự báo khi bấm nút (reportToHost)
 *
 * Module này KHÔNG biết trang nào xử lý nút nào: mọi sự kiện được chuyển cho hàm do
 * ui_router đăng ký (hmi_set_event_handler), ui_router chia cho từng page.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hmi_map.h"

// ---- Chân / thông số (đổi ở đây nếu cần) ----
#define HMI_UART                  UART_NUM_2
#define HMI_RX_PIN                16            // nối TXA của màn hình
#define HMI_TX_PIN                17            // nối RXB của màn hình
#define HMI_BAUD                  115200
#define HMI_LINK_TIMEOUT_MS       2000          // quá 2 s không nhận được dữ liệu -> coi như mất màn hình

// ---- Dữ liệu màn hình (bản sao lấy qua hmi_get) ----
typedef struct {
    int      page;                  // trang hiện tại, -1 = chưa biết
    uint16_t vars[HMI_VAR_COUNT];   // vars[i] = giá trị tại 0x1000 + i (xem VAR_* trong hmi_map.h)
    bool     has_vars;              // đã đọc được biến ít nhất 1 lần
    int64_t  last_rx_ms;            // lần cuối nhận được dữ liệu (ms)
} hmi_data_t;

// ---- Sự kiện nút bấm ----
// EV_SRC_REPORT: màn hình tự báo (0x41) - với nút có longPress, màn hình chỉ báo lúc THẢ TAY
// EV_SRC_POLL  : ESP32 tự đọc thấy giá trị đổi (0x1011 phím, 0x1012 Sismo) - biết được lúc đang giữ
typedef enum { EV_SRC_REPORT = 0, EV_SRC_POLL = 1 } hmi_source_t;
typedef struct { uint16_t addr; uint16_t value; uint8_t source; } hmi_event_t;
typedef void (*hmi_event_handler_t)(const hmi_event_t *e);   // chạy trong task hmi_rx: phải NGẮN, không chờ

void       hmi_start(void);
void       hmi_set_event_handler(hmi_event_handler_t handler);
hmi_data_t hmi_get(void);                                   // bản sao dữ liệu mới nhất (an toàn giữa các task)
bool       hmi_is_linked(const hmi_data_t *d);
void       hmi_write(uint16_t addr, uint16_t value);        // ghi 1 biến (gọi từ task nào cũng được)
void       hmi_goto_page(int page);                         // = hmi_write(ADDR_PAGE, page)

// ---- Chẩn đoán đường UART: có byte về không, có khung hợp lệ không ----
typedef struct {
    uint32_t rx_bytes;      // tổng số byte nhận được trên chân RX (kể cả rác)
    uint32_t frames_ok;     // số khung 5A A5 ... đúng CRC
    uint32_t crc_fail;      // số khung sai CRC
    uint8_t  last[12];      // vài byte nhận gần nhất
    uint8_t  last_len;
} hmi_diag_t;
hmi_diag_t hmi_get_diag(void);
