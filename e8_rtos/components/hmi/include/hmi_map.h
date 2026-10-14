/*
 * hmi_map.h - Bản đồ giao diện (UI_Editor-II project HMI_E8): số trang, địa chỉ biến, giá trị đặc biệt
 *
 * Mọi con số "của UI" nằm ở đây. Sửa UI (thêm trang, đổi địa chỉ) thì chỉ sửa file này.
 *
 *   0x7000          trang hiện tại (ghi = chuyển trang)
 *   0x1000..0x1009  biến ESP32 đọc định kỳ: [0] cường độ 0..31, [1] 1 = đang tập, [3] giây còn lại,
 *                   [5] Xung, [6] Perso 1..6, [7] Programmes 0..21, [9] cấp độ 0..2
 *   0x1011          phím trang mật khẩu (0..9, 10 = C); ESP32 ghi lại KEY_EMPTY sau mỗi lần đọc
 *   0x1012          chữ "Sismo" trên mọi trang (giữ = giá trị tăng liên tục)
 *   0x1013 / 0x1040 nút / icon trạng thái mạng trang 0027: 0 OFFLINE, 1 Configuration WIFI, 2 ONLINE
 *   0x1014 / 0x1041 nút / icon trang 0028: 0 MODE WIFI, 1 MODE LOCAL
 *   0x1039          số dấu * trên trang mật khẩu (0..5)
 *
 * Trang: 0000 khởi động, 0001 menu chính, 0026 mật khẩu, 0027 cài đặt WiFi (74700),
 * 0028 MODE WIFI / LOCAL (74710).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ADDR_PAGE                 0x7000

#define PAGE_STARTUP              0
#define PAGE_MAIN                 1
#define PAGE_PASSWORD             26
#define PAGE_SETTINGS             27
#define PAGE_MODE                 28

#define ADDR_VAR_BASE             0x1000
#define HMI_VAR_COUNT             10
#define VAR_INTENSITY             0
#define VAR_RUN                   1
#define VAR_SECONDS               3
#define VAR_PULSE                 5
#define VAR_PERSO                 6
#define VAR_PROGRAM               7
#define VAR_LEVEL                 9
#define ADDR_INTENSITY            (ADDR_VAR_BASE + VAR_INTENSITY)
#define ADDR_RUN                  (ADDR_VAR_BASE + VAR_RUN)

#define ADDR_PASSWORD_KEY         0x1011
#define ADDR_SISMO_BUTTON         0x1012
#define ADDR_SETTINGS_BUTTON      0x1013
#define ADDR_STAR_COUNT           0x1039
#define ADDR_NETWORK_ICON         0x1040
#define ADDR_MODE_BUTTON          0x1014
#define ADDR_MODE_ICON            0x1041

#define KEY_CLEAR                 10
#define KEY_EMPTY                 0xFFFF

#define NET_ICON_OFFLINE          0
#define NET_ICON_CONFIG           1
#define NET_ICON_ONLINE           2

bool hmi_is_session_page(int page);
void hmi_page_name(int page, char *out, size_t n);
