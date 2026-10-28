/*
 * page_setting.h - Chữ Sismo (mọi trang), mật khẩu 0026, các trang cài đặt 0027..0030, khởi động 0000
 *
 * Task "setting" (ưu tiên 3):
 *   - Chạm "Sismo" ở trang bất kỳ        -> về trang chính ngay
 *   - Giữ "Sismo" 2 s                    -> trang mật khẩu (0026)
 *   - Nhập 74700                         -> trang cài đặt WiFi (0027): ONLINE = đang nối được server ThingsBoard
 *       bấm ONLINE / OFFLINE             -> "Configuration WIFI", ESP32 phát AP + web cấu hình
 *       bấm "Configuration WIFI"         -> tắt AP, về trạng thái hiện tại
 *       Submit trên web (đúng / sai)     -> lưu wifi.json, tắt AP, màn hình về trang 0, khởi động lại
 *       rời trang khi đang cấu hình      -> tắt AP
 *   - Nhập 74710                         -> trang MODE (0028): bấm đổi vòng MODE WIFI / LAN / LOCAL (lưu flash)
 *   - Nhập 74766                         -> trang thẻ (0029): Accès libre-service (chế độ test, luôn
 *                                           connecté) / Accès badgeuse (connecté = đang có thẻ)
 *   - Nhập 200816                        -> trang 0030: Mode 1..5, Card / Sismo Timer, Motor ratio 2..17,
 *                                           MANUAL 5' / 90' (không đổi được khi động cơ đang chạy)
 *   - Mật khẩu 5 hoặc 6 số. Đủ 5 số mà sai (và không phải đầu của mật khẩu 6 số nào) -> đứng yên,
 *     bấm số tiếp theo -> xóa hết; đủ 6 số mà sai -> bấm số tiếp theo -> xóa hết. Phím C -> xóa hết.
 *   - Trang 0 (khởi động) hiện 3 s       -> tự sang trang chính
 *   - page_setting_refresh()             -> ghi lại ngôn ngữ + icon trang cài đặt (sau khi đổi từ ThingsBoard)
 */
#pragma once
#include <stdbool.h>
#include "hmi.h"

void page_setting_start(void);
void page_setting_refresh(void);
bool page_setting_on_event(const hmi_event_t *e);
