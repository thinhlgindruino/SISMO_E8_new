/*
 * network.h - Kết nối mạng WiFi cho E8+
 *
 *   - Chế độ thường (STA): khởi động lên tự kết nối WiFi đã lưu trong NVS.
 *     Không có / sai thì báo OFFLINE (vẫn tự thử lại định kỳ phía sau).
 *   - Chế độ cấu hình (AP): ESP32 phát WiFi "SISMO_E8_XXXXXX" (XXXXXX = 6 ký tự cuối MAC,
 *     viết hoa), mật khẩu NET_AP_PASSWORD, IP 192.168.10.100. Vào trình duyệt trang
 *     http://192.168.10.100 -> nhập SSID + mật khẩu -> Submit -> lưu file /spiffs/wifi.json
 *     -> tắt AP -> ESP32 khởi động lại (màn hình về trang 0) -> vào WiFi vừa lưu.
 *   - Đang vào WiFi mà bật cấu hình thì vẫn giữ kết nối (AP + STA); tắt cấu hình là về lại ONLINE.
 *
 * Viết theo esp_netif chung để sau này thêm Ethernet (LAN) không phải sửa lại phần web / NVS.
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
    NET_OFFLINE = 0,       // chưa cấu hình / không kết nối được
    NET_CONNECTING,      // đang thử kết nối WiFi đã lưu
    NET_ONLINE,            // đã có IP
    NET_CONFIG,          // đang phát AP + web cấu hình
} net_state_t;

void net_start(void);                 // gọi 1 lần lúc khởi động (khởi tạo NVS, WiFi)
void net_config_start(void);              // chuyển sang chế độ AP + web cấu hình
void net_config_stop(void);              // tắt AP + web, về chế độ thường (STA), không khởi động lại
net_state_t net_get_state(void);
const char *net_state_name(net_state_t t);
const char *net_ap_name(void);             // "SISMO_E8_XXXXXX"
void net_get_info(char *ssid, size_t n_ssid, char *ip, size_t n_ip);

// Dùng bởi web.c
bool net_save_wifi(const char *ssid, const char *pass);   // lưu /spiffs/wifi.json
void net_clear_wifi(void);                                     // xóa WiFi đã lưu
void net_restart_after(int ms);                          // ESP32 reset sau ms
void net_finish_config_and_restart(int ms);                 // sau ms: tắt AP rồi reset (sau khi Submit)

// Gọi ngay trước khi ESP32 khởi động lại sau Submit (vd. đưa màn hình về trang 0)
void net_set_before_restart_hook(void (*hook)(void));
// Đọc nội dung wifi.json vào buf (mask_password: thay mật khẩu bằng ****). Trả về độ dài, 0 = chưa có file.
int net_read_json(char *buf, size_t n, bool mask_password);
