/*
 * network.h - Kết nối mạng WiFi cho E8+
 *
 * - Chế độ thường (STA): khởi động lên tự kết nối WiFi đã lưu trong /spiffs/wifi.json.
 *   5 lần đầu thử lại mỗi 2 s (DANG KET NOI), sau đó báo OFFLINE nhưng vẫn thử lại mỗi 15 s.
 * - Chế độ cấu hình (AP + STA): ESP32 phát WiFi "SISMO_E8_XXXXXX" (XXXXXX = 6 ký tự cuối MAC,
 *   viết hoa), mật khẩu NET_AP_PASSWORD, IP 192.168.10.100. Vào http://192.168.10.100 -> nhập SSID
 *   + mật khẩu -> Submit -> lưu wifi.json -> tắt AP -> ESP32 khởi động lại (màn hình về trang 0)
 *   -> vào WiFi vừa lưu. Đang ONLINE mà bật cấu hình thì vẫn giữ kết nối; tắt cấu hình là về lại ONLINE.
 * - net_set_before_restart_hook(): hàm được gọi ngay trước khi khởi động lại sau Submit / OTA
 *   (page_setting dùng để đưa màn hình về trang 0).
 *
 * Viết theo esp_netif chung để sau này thêm Ethernet (LAN) không phải sửa lại phần web.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NET_AP_PASSWORD           "12345678"
#define NET_AP_IP                 "192.168.10.100"
#define NET_AP_CHANNEL            1
#define NET_AP_MAX_CLIENTS        4

typedef enum {
    NET_OFFLINE = 0,
    NET_CONNECTING,
    NET_ONLINE,
    NET_CONFIG,
} net_state_t;

void net_start(void);
void net_config_start(void);
void net_config_stop(void);
net_state_t net_get_state(void);
const char *net_state_name(net_state_t t);
const char *net_ap_name(void);
void net_get_info(char *ssid, size_t n_ssid, char *ip, size_t n_ip);

bool net_save_wifi(const char *ssid, const char *pass);
void net_clear_wifi(void);
void net_restart_after(int ms);
void net_finish_config_and_restart(int ms);

void net_set_before_restart_hook(void (*hook)(void));
int net_read_json(char *buf, size_t n, bool mask_password);
