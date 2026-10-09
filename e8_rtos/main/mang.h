/*
 * mang.h - Kết nối mạng WiFi cho E8+
 *
 *   - Chế độ thường (STA): khởi động lên tự kết nối WiFi đã lưu trong NVS.
 *     Không có / sai thì báo OFFLINE (vẫn tự thử lại định kỳ phía sau).
 *   - Chế độ cấu hình (AP): ESP32 phát WiFi "SISMO_E8_XXXXXX" (XXXXXX = 6 ký tự cuối MAC,
 *     viết hoa), mật khẩu MANG_AP_MAT_KHAU, IP 192.168.10.100. Vào trình duyệt trang
 *     http://192.168.10.100 -> nhập SSID + mật khẩu -> Submit -> lưu NVS -> ESP32 khởi động lại.
 *
 * Viết theo esp_netif chung để sau này thêm Ethernet (LAN) không phải sửa lại phần web / NVS.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MANG_AP_MAT_KHAU   "12345678"
#define MANG_AP_IP         "192.168.10.100"
#define MANG_AP_KENH       1
#define MANG_AP_SO_KET_NOI 4

typedef enum {
    MANG_OFFLINE = 0,       // chưa cấu hình / không kết nối được
    MANG_DANG_KET_NOI,      // đang thử kết nối WiFi đã lưu
    MANG_ONLINE,            // đã có IP
    MANG_CAU_HINH,          // đang phát AP + web cấu hình
} mang_trang_thai_t;

void mang_khoi_dong(void);                 // gọi 1 lần lúc khởi động (khởi tạo NVS, WiFi)
void mang_bat_cau_hinh(void);              // chuyển sang chế độ AP + web cấu hình
mang_trang_thai_t mang_trang_thai(void);
const char *mang_ten_trang_thai(mang_trang_thai_t t);
const char *mang_ten_ap(void);             // "SISMO_E8_XXXXXX"
void mang_lay_thong_tin(char *ssid, size_t n_ssid, char *ip, size_t n_ip);

// Dùng bởi web.c
bool mang_luu_wifi(const char *ssid, const char *mat_khau);   // lưu NVS
void mang_xoa_wifi(void);                                     // xóa WiFi đã lưu
void mang_khoi_dong_lai_sau(int ms);                          // ESP32 reset sau ms
