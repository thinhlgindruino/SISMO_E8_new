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
 *   0x1030          ngôn ngữ 0..5 (Français, English, Español, Italiano, Deutsch, Português): mọi icon
 *                   chữ cố định đọc biến này; nút ngôn ngữ trang 0029 tự tăng vòng và báo về ESP32
 *   Icon chữ phụ thuộc trạng thái: giá trị = LANG_ICON(trạng thái, ngôn ngữ) = trạng thái * 6 + ngôn ngữ + 1
 *   0x1013 / 0x1040 nút / icon mạng trang 0027: 1 OFFLINE, 2 ONLINE, 3 + ngôn ngữ = Configuration WIFI
 *   0x1014 / 0x1041 nút / icon trang 0028: 0 MODE WIFI, 1 MODE LOCAL
 *   0x1015 / 0x1042 nút / icon Mode 1..5 (trạng thái 0..4, theo ngôn ngữ)   trang 0030
 *   0x1016 / 0x1043 nút / icon Card Timer / Sismo Timer (0 / 1, theo ngôn ngữ) trang 0030
 *   0x1017 / 0x1044 nút / icon Motor ratio 2..17 (icon 0..15)             trang 0030
 *   0x1018 / 0x1045 nút / icon MANUAL 5' / 90' (0 / 1)                    trang 0030
 *   0x1019 / 0x1046 nút / icon Accès libre-service / badgeuse (0 / 1, theo ngôn ngữ) trang 0029
 *   0x1047          icon "E8 CM8 - Statut" (thẻ): 0 déconnecté, 1 connecté (theo ngôn ngữ) trang 0029
 *   0x1048          thời gian hiện trên màn hình = phút * 100 + giây (vd. 9000 = 90:00)   trang 0010
 *   0x1039          số dấu * trên trang mật khẩu (0..6)
 *   0x1020          số phút trang Durée Exercices (0002), 1..10: nút - / + và icon vòng tròn
 *   0x1050..0x105A  trang buổi tập Programmes / Perso 0006 / 0014: hình minh họa 1..32, tên bài 1..48
 *                   (theo ngôn ngữ, 0 = ẩn; tên 49..60 Proprioception / Perso nằm ở 0x105B),
 *                   trục (0 = 10 phút, 1 = 2 phút), phút, giây, vòng cường độ 1..32, icon X (0 sáng /
 *                   1 xám), Xung, Play / Pause, nút (1 X, 2 Xung, 3 Play, 4 Stop, 5 mũi tên, 6 <), Progress Bar
 *   0x1080..0x108E  trang bài tập con 0013: 15 ô (3 cột x 5 hàng), số ảnh ô 1..N (icon 1000..), 0 = trống
 *   0x108F          mũi tên trang 0013: 0 không, 1 >, 2 <
 *   0x105D / 0x105E icon giữ ảnh biểu đồ (không ghi) / icon cố định (luôn 0: dấu ":" và nút Stop)
 *   0x2020 / 0x2030 parameterAddr Progress Bar trang 0006 / 0014
 *   0x700A          phát file WAV thứ N trong WavBin (1 = tiếng bíp)
 *   0x700B          âm lượng loa 0..16
 *
 * Trang: 0000 khởi động, 0001 menu chính, 0002 Durée Exercices (chọn 1..10 phút), 0010 buổi tập
 * Manuel, 0011 buổi tập Durée, 0003..0006 / 0013 / 0014 / 0021..0025 Programmes, 0009 / 0012 / 0015..0020
 * trống (không widget), 0026 mật khẩu,
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
#define PAGE_DUREE                2
#define PAGE_DUREE_SESSION        11
#define PAGE_LEVEL                5
#define PAGE_PROGRAM_X            6
#define PAGE_EXERCISES            13
#define PAGE_PROGRAM              14
#define PAGE_PROPRIO              25
#define PAGE_PERSO                7
#define PAGE_PROGRAMMES           3
#define PAGE_TONIFICATION         4
#define PAGE_REMODELAGE           21
#define PAGE_ANTICELLULITE        22
#define PAGE_GAINAGE              23
#define PAGE_RELAXATION           24

#define ADDR_VAR_BASE             0x1000
#define HMI_VAR_COUNT             10
#define VAR_INTENSITY             0
#define VAR_RUN                   1
#define VAR_SECONDS               3
#define VAR_PULSE                 5
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
#define ADDR_DURATION_MIN         0x1020
#define ADDR_PROG_ILLUS           0x1050
#define ADDR_PROG_TITLE           0x1051
#define ADDR_PROG_AXIS            0x1052
#define ADDR_PROG_MIN             0x1053
#define ADDR_PROG_SEC             0x1054
#define ADDR_PROG_LEVEL           0x1055
#define ADDR_PROG_X_ICON          0x1056
#define ADDR_PROG_PULSE_ICON      0x1057
#define ADDR_PROG_PLAY_ICON       0x1058
#define ADDR_PROG_BUTTON          0x1059
#define ADDR_PROG_PROGRESS        0x105A
#define ADDR_PROG_TITLE_SHORT     0x105B
#define ADDR_LANGUAGE             0x1030
#define ADDR_GRID_SLOT            0x1080
#define ADDR_PROG_PARAM_X         0x2020
#define ADDR_PROG_PARAM           0x2030
#define PROG_BTN_X                1
#define PROG_BTN_PULSE            2
#define PROG_BTN_PLAY             3
#define PROG_BTN_STOP             4
#define PROG_BTN_ARROW            5
#define PROG_BTN_BACK             6
#define ADDR_WAV                  0x700A
#define ADDR_VOLUME               0x700B
#define WAV_BEEP                  1

#define KEY_CLEAR                 10
#define KEY_EMPTY                 0xFFFF

#define LANG_COUNT                6
#define LANG_ICON(state, lang)    ((uint16_t)((state) * LANG_COUNT + (lang) + 1))
#define NET_ICON_OFFLINE          1
#define NET_ICON_ONLINE           2
#define NET_ICON_CONFIG_BASE      3

bool hmi_is_session_page(int page);
void hmi_page_name(int page, char *out, size_t n);
