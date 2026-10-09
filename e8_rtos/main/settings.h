/*
 * settings.h - Task "settings": chữ Sismo, trang mật khẩu, trang cài đặt WiFi
 *
 *   - Chạm "Sismo" ở trang bất kỳ        -> về trang chính ngay
 *   - Giữ "Sismo" SISMO_HOLD_MS (2 s)      -> trang mật khẩu (0026)
 *   - Nhập đủ 74700                       -> tự sang trang cài đặt (0027): hiện ONLINE / OFFLINE hiện tại
 *   - Bấm ONLINE / OFFLINE                -> "Configuration WIFI", ESP32 phát AP + web cấu hình
 *   - Bấm "Configuration WIFI"            -> tắt AP, về trạng thái hiện tại (ONLINE vẫn ONLINE)
 *   - Submit trên web (đúng/sai)          -> lưu wifi.json, tắt AP, màn hình về trang 0, ESP32 khởi động lại
 *   - Rời trang cài đặt khi đang cấu hình -> tắt AP, về chế độ thường
 */
#pragma once

void settings_start(void);
