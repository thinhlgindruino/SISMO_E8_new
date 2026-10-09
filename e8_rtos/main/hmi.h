/*
 * hmi.h - Giao tiếp màn hình HMI (LT7689, UI_Editor-II) bằng 2 task:
 *   hmi_rx   : chỉ nhận và giải mã khung từ màn hình
 *   hmi_poll : hỏi 0x1011/0x1012 mỗi 100 ms, biến 0x1000..0x1009 và trang (0x7000) xen kẽ
 * Dữ liệu dùng chung được bảo vệ bằng mutex; task khác lấy bản sao qua hmi_get().
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HMI_VAR_COUNT 10                // 0x1000 .. 0x1009

typedef struct {
    int      page;                  // trang hiện tại, -1 = chưa biết
    uint16_t vars[HMI_VAR_COUNT];   // vars[i] = giá trị tại 0x1000 + i
    bool     has_vars;              // đã đọc được biến ít nhất 1 lần
    int64_t  last_rx_ms;            // lần cuối nhận được dữ liệu (ms)
} hmi_data_t;

void hmi_start(void);
hmi_data_t hmi_get(void);            // bản sao dữ liệu mới nhất (an toàn giữa các task)
bool hmi_is_linked(const hmi_data_t *d);
void hmi_write(uint16_t addr, uint16_t value);   // ghi 1 biến lên màn hình (gọi từ task nào cũng được)

// Lấy lần bấm +/- tiếp theo (giá trị 0x1000 màn hình báo về). false = không còn lần bấm nào.
bool hmi_get_intensity_touch(uint16_t *value);

// Các lần bấm nút của phần cài đặt (Touch Returned Message 0x41, địa chỉ 0x1011..0x1013)
// Nút Sismo (0x1012) còn được ESP32 tự đọc mỗi 100 ms (task hmi_poll): màn hình chỉ báo 0x41 lúc THẢ TAY,
// còn lúc đang giữ (longPress Repeat) giá trị tăng liên tục -> đọc thấy đổi = ngón tay đang giữ.
typedef enum { EV_SRC_REPORT = 0, EV_SRC_POLL = 1 } hmi_source_t;
typedef struct { uint16_t addr; uint16_t value; uint8_t source; } hmi_event_t;
bool hmi_get_event(hmi_event_t *e, TickType_t wait);    // chờ tối đa 'wait' tick
