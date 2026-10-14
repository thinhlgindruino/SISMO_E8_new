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
 *   0x1015 / 0x1042 nút / icon Mode 1..5 (icon 0..4)                     trang 0030
 *   0x1016 / 0x1043 nút / icon Card Timer / Sismo Timer (0 / 1)           trang 0030
 *   0x1017 / 0x1044 nút / icon Motor ratio 2..17 (icon 0..15)             trang 0030
 *   0x1018 / 0x1045 nút / icon MANUAL 5' / 90' (0 / 1)                    trang 0030
 *   0x1019 / 0x1046 nút / icon Accès libre-service / badgeuse (0 / 1)     trang 0029
 *   0x1047          icon "E8 CM8 - Statut" (trạng thái thẻ): 0 déconnecté, 1 connecté   trang 0029
 *   0x1048          thời gian hiện trên màn hình = phút * 100 + giây (vd. 9000 = 90:00)   trang 0010
 *   0x1039          số dấu * trên trang mật khẩu (0..6)
 *
 * Trang: 0000 khởi động, 0001 menu chính, 0010 buổi tập Manuel, 0026 mật khẩu,
 * 0027 cài đặt WiFi (74700), 0028 MODE WIFI / LOCAL (74710), 0029 thẻ + ngôn ngữ (74766),
 * 0030 Mode / Timer / Ratio / Manual (200816).
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
#define PAGE_ACCESS               29
#define PAGE_FACTORY              30
#define PAGE_MANUEL               10

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
#define ADDR_SECONDS              (ADDR_VAR_BASE + VAR_SECONDS)

#define ADDR_PASSWORD_KEY         0x1011
#define ADDR_SISMO_BUTTON         0x1012
#define ADDR_SETTINGS_BUTTON      0x1013
#define ADDR_STAR_COUNT           0x1039
#define ADDR_NETWORK_ICON         0x1040
#define ADDR_MODE_BUTTON          0x1014
#define ADDR_MODE_ICON            0x1041
#define ADDR_MOTOR_MODE_BUTTON    0x1015
#define ADDR_CARD_TIMER_BUTTON    0x1016
#define ADDR_MOTOR_RATIO_BUTTON   0x1017
#define ADDR_MANUAL_BUTTON        0x1018
#define ADDR_ACCESS_BUTTON        0x1019
#define ADDR_MOTOR_MODE_ICON      0x1042
#define ADDR_CARD_TIMER_ICON      0x1043
#define ADDR_MOTOR_RATIO_ICON     0x1044
#define ADDR_MANUAL_ICON          0x1045
#define ADDR_ACCESS_ICON          0x1046
#define ADDR_CM8_STATUS_ICON      0x1047
#define ADDR_TIME_DISPLAY         0x1048

#define KEY_CLEAR                 10
#define KEY_EMPTY                 0xFFFF

#define NET_ICON_OFFLINE          0
#define NET_ICON_CONFIG           1
#define NET_ICON_ONLINE           2

bool hmi_is_session_page(int page);
void hmi_page_name(int page, char *out, size_t n);
