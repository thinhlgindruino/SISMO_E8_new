/*
 * cai_dat.h - Task "cai_dat": chữ Sismo, trang mật khẩu, trang cài đặt WiFi
 *
 *   - Chạm "Sismo" ở trang bất kỳ        -> về trang chính ngay
 *   - Giữ "Sismo" 3 s                     -> trang mật khẩu (0026)
 *   - Nhập đủ 74700                       -> tự sang trang cài đặt (0027): OFFLINE / ONLINE
 *   - Bấm OFFLINE                         -> "Configuration WIFI", ESP32 phát AP + web cấu hình
 *   - Rời trang cài đặt khi đang cấu hình -> ESP32 khởi động lại, về chế độ thường
 */
#pragma once

void cai_dat_khoi_dong(void);
