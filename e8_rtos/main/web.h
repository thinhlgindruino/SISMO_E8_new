/*
 * web.h - Trang web cấu hình (giống E8 cũ): tab Settings (WiFi) + tab Upload (cập nhật firmware)
 */
#pragma once
void web_start(void);     // khởi động web server (gọi khi vào chế độ cấu hình)
void web_stop(void);     // dừng web server (khi thoát chế độ cấu hình)
