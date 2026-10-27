/*
 * network.h - Kết nối mạng WiFi cho E8+
 *
 * - Chế độ thường (STA): khởi động lên tự kết nối WiFi đã lưu trong /spiffs/wifi.json.
 *   5 lần đầu thử lại mỗi 2 s (DANG KET NOI), sau đó báo OFFLINE nhưng vẫn thử lại mỗi 15 s.
 * - Chế độ cấu hình (AP + STA): ESP32 phát WiFi "SISMO_E8_XXXXXX" (XXXXXX = 6 ký tự cuối MAC,
 *   viết hoa), mật khẩu NET_AP_PASSWORD, IP 192.168.10.100. Vào http://192.168.10.100 -> nhập SSID
 *   + mật khẩu -> Submit -> lưu wifi.json -> tắt AP -> ESP32 khởi động lại (màn hình về trang 0)
 *   -> vào WiFi vừa lưu. Đang ONLINE mà bật cấu hình thì vẫn giữ kết nối; tắt cấu hình là về lại ONLINE.
 * - net_get_info(): khi đang dùng LAN thì ssid = "LAN", ip = IP của LAN.
 * - net_mac_text(): MAC WiFi dạng "AA:BB:CC:DD:EE:FF"; net_get_rssi(): cường độ sóng WiFi (dBm, 0 = chưa kết nối).
 * - net_set_before_restart_hook(): hàm được gọi ngay trước khi khởi động lại sau Submit / OTA
 *   (page_setting dùng để đưa màn hình về trang 0).
 *
 * - LAN (board WT32-ETH01, chip LAN8720): chạy song song với WiFi, lấy IP bằng DHCP. Cắm dây LAN có IP -> ONLINE
 *   qua LAN và LAN được ưu tiên (route_prio NET_ETH_ROUTE_PRIO > WiFi 100); rút dây -> dùng lại WiFi nếu có.
 *   Board không có LAN (khởi tạo lỗi) -> chỉ báo log, chạy WiFi như cũ. net_get_link(): mạng đang dùng.
 * - Chân LAN của WT32-ETH01: RMII cố định (TXD0 19, TX_EN 21, TXD1 22, RXD0 25, RXD1 26, CRS_DV 27), MDC 23, MDIO 18,
 *   clock 50 MHz vào GPIO0, GPIO16 bật bộ dao động, PHY địa chỉ 1.
 * - NET_ETH_STATIC_IP: rỗng = DHCP; đặt vd. "192.168.137.50" -> cắm dây là dùng IP tĩnh đó (+ GW / MASK), để thử LAN
 *   khi không có DHCP. Có dây mà chưa có IP -> cứ NET_ETH_WAIT_LOG_MS in 1 dòng log chờ.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NET_AP_PASSWORD           "12345678"
#define NET_AP_IP                 "192.168.10.100"
#define NET_AP_CHANNEL            1
#define NET_AP_MAX_CLIENTS        4

#define NET_ETH_ENABLED           1
#define NET_ETH_POWER_GPIO        16
#define NET_ETH_MDC_GPIO          23
#define NET_ETH_MDIO_GPIO         18
#define NET_ETH_PHY_ADDR          1
#define NET_ETH_ROUTE_PRIO        150
#define NET_ETH_STATIC_IP         ""
#define NET_ETH_STATIC_GW         "192.168.137.1"
#define NET_ETH_STATIC_MASK       "255.255.255.0"
#define NET_ETH_WAIT_LOG_MS       10000

typedef enum { NET_LINK_NONE = 0, NET_LINK_WIFI, NET_LINK_LAN } net_link_t;

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
const char *net_mac_text(void);
int net_get_rssi(void);
net_link_t net_get_link(void);
const char *net_link_name(net_link_t l);
void net_get_info(char *ssid, size_t n_ssid, char *ip, size_t n_ip);

bool net_save_wifi(const char *ssid, const char *pass);
void net_clear_wifi(void);
void net_restart_after(int ms);
void net_finish_config_and_restart(int ms);

void net_set_before_restart_hook(void (*hook)(void));
int net_read_json(char *buf, size_t n, bool mask_password);
