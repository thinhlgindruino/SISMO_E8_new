/*
 * hmi_map.h - Bản đồ giao diện (UI_Editor-II project HMI_E8): số trang, địa chỉ biến, giá trị đặc biệt
 *
 * Mọi con số "của UI" nằm ở đây. Sửa UI (thêm trang, đổi địa chỉ) thì chỉ sửa file này.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------------- Thanh ghi hệ thống của màn hình ----------------
#define ADDR_PAGE                 0x7000        // ghi = chuyển trang, đọc = trang hiện tại

// ---------------- Số trang ----------------
#define PAGE_STARTUP              0             // 0000_Demarrage
#define PAGE_MAIN                 1             // 0001_Menu_principal (4 nút)
#define PAGE_PASSWORD             26            // 0026 nhập mật khẩu cài đặt
#define PAGE_SETTINGS             27            // 0027 cài đặt: OFFLINE / Configuration WIFI / ONLINE

// ---------------- Biến 0x1000..0x1009 (ESP32 đọc định kỳ) ----------------
#define ADDR_VAR_BASE             0x1000
#define HMI_VAR_COUNT             10
#define VAR_INTENSITY             0             // 0x1000 cường độ 0..31 (nút +/- và đồng hồ cùng dùng)
#define VAR_RUN                   1             // 0x1001 1 = đang tập, 0 = tạm dừng
#define VAR_SECONDS               3             // 0x1003 thời gian còn lại (giây)
#define VAR_PULSE                 5             // 0x1005 1 = bật Xung
#define VAR_PERSO                 6             // 0x1006 chương trình Perso 1..6
#define VAR_PROGRAM               7             // 0x1007 chương trình Programmes 0..21
#define VAR_LEVEL                 9             // 0x1009 cấp độ 0..2
#define ADDR_INTENSITY            (ADDR_VAR_BASE + VAR_INTENSITY)
#define ADDR_RUN                  (ADDR_VAR_BASE + VAR_RUN)

// ---------------- Trang mật khẩu / cài đặt ----------------
#define ADDR_PASSWORD_KEY         0x1011        // phím trang mật khẩu: 0..9 = số, 10 = C
#define ADDR_SISMO_BUTTON         0x1012        // chữ "Sismo" trên mọi trang (giữ = giá trị tăng liên tục)
#define ADDR_SETTINGS_BUTTON      0x1013        // nút OFFLINE / ONLINE / Configuration WIFI (trang 27)
#define ADDR_STAR_COUNT           0x1039        // số dấu * đang hiện (Icon 0403..0408 = 0..5 dấu)
#define ADDR_NETWORK_ICON         0x1040        // Icon nút: 0 OFFLINE, 1 Configuration WIFI, 2 ONLINE

#define KEY_CLEAR                 10            // phím C
#define KEY_EMPTY                 0xFFFF        // ESP32 ghi vào 0x1011 sau mỗi lần đọc được phím

#define NET_ICON_OFFLINE          0
#define NET_ICON_CONFIG           1
#define NET_ICON_ONLINE           2

// ---------------- Hàm tiện ích ----------------
bool hmi_is_session_page(int page);                       // trang buổi tập (6, 8, 10, 11)
void hmi_page_name(int page, char *out, size_t n);        // tên trang để in log
