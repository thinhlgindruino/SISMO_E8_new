/*
 * page_setting.h - Chữ Sismo (mọi trang), trang mật khẩu 0026, trang cài đặt WiFi 0027, trang khởi động 0000
 *
 * Task "setting" (ưu tiên 3):
 *   - Chạm "Sismo" ở trang bất kỳ        -> về trang chính ngay
 *   - Giữ "Sismo" SISMO_HOLD_MS (2 s)      -> trang mật khẩu (0026)
 *   - Nhập đủ 74700                       -> tự sang trang cài đặt (0027): hiện ONLINE / OFFLINE hiện tại
 *   - Bấm ONLINE / OFFLINE                -> "Configuration WIFI", ESP32 phát AP + web cấu hình
 *   - Bấm "Configuration WIFI"            -> tắt AP, về trạng thái hiện tại (ONLINE vẫn ONLINE)
 *   - Submit trên web (đúng/sai)          -> lưu wifi.json, tắt AP, màn hình về trang 0, ESP32 khởi động lại
 *   - Rời trang cài đặt khi đang cấu hình -> tắt AP, về chế độ thường
 *   - Trang 0 (khởi động) hiện 3 s        -> tự sang trang chính
 */
#pragma once
#include <stdbool.h>
#include "hmi.h"

void page_setting_start(void);
bool page_setting_on_event(const hmi_event_t *e);   // ui_router gọi; true = trang này nhận
