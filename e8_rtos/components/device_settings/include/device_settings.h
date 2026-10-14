/*
 * device_settings.h - Cài đặt của máy lưu trong flash (file JSON trên SPIFFS), giữ nguyên khi mất điện
 *
 *   /spiffs/settings.json   {"control_mode": "local"}       <- module này
 *   /spiffs/wifi.json       {"ssid": ..., "password": ...}  <- network (dùng chung vùng SPIFFS)
 *
 * Chế độ điều khiển (trang 74710):
 *   CONTROL_MODE_WIFI   phải ONLINE mới cho động cơ chạy
 *   CONTROL_MODE_LOCAL  không cần mạng (mặc định)
 *
 * Module này gắn (mount) phân vùng "storage" lúc khởi động -> phải gọi device_settings_start()
 * TRƯỚC net_start(). Sau này thêm cài đặt (ngôn ngữ 74766, mode / ratio / timer 200816...)
 * thì thêm getter / setter ở đây.
 */
#pragma once
#include <stdbool.h>

#define STORAGE_BASE              "/spiffs"

typedef enum {
    CONTROL_MODE_WIFI  = 0,
    CONTROL_MODE_LOCAL = 1,
} control_mode_t;
#define CONTROL_MODE_DEFAULT      CONTROL_MODE_LOCAL

void           device_settings_start(void);
bool           device_storage_ok(void);
bool           storage_write_text(const char *path, const char *text);

control_mode_t device_get_control_mode(void);
bool           device_set_control_mode(control_mode_t m);
const char    *control_mode_name(control_mode_t m);
