/*
 * page_setting.h - Chữ Sismo (mọi trang), trang mật khẩu 0026, cài đặt WiFi 0027, MODE 0028, khởi động 0000
 *
 * Task "setting" (ưu tiên 3):
 *   - Chạm "Sismo" ở trang bất kỳ        -> về trang chính ngay
 *   - Giữ "Sismo" 2 s                    -> trang mật khẩu (0026)
 *   - Nhập 74700                         -> trang cài đặt WiFi (0027): hiện ONLINE / OFFLINE hiện tại
 *       bấm ONLINE / OFFLINE             -> "Configuration WIFI", ESP32 phát AP + web cấu hình
 *       bấm "Configuration WIFI"         -> tắt AP, về trạng thái hiện tại
 *       Submit trên web (đúng / sai)     -> lưu wifi.json, tắt AP, màn hình về trang 0, khởi động lại
 *       rời trang khi đang cấu hình      -> tắt AP
 *   - Nhập 74710                         -> trang MODE (0028): bấm đổi MODE WIFI / MODE LOCAL (lưu flash)
 *   - Đủ 5 số mà sai -> đứng yên; bấm số thứ 6 -> xóa hết. Phím C -> xóa hết.
 *   - Trang 0 (khởi động) hiện 3 s       -> tự sang trang chính
 */
#pragma once
#include <stdbool.h>
#include "hmi.h"

void page_setting_start(void);
bool page_setting_on_event(const hmi_event_t *e);
