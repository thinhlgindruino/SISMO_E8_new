/*
 * device_settings.h - Cài đặt của máy lưu trong flash (file JSON trên SPIFFS), giữ nguyên khi mất điện
 *
 *   /spiffs/settings.json   cài đặt bên dưới                  <- module này
 *   /spiffs/wifi.json       {"ssid": ..., "password": ...}    <- network (dùng chung vùng SPIFFS)
 *
 * Cài đặt (khóa JSON | trang | giá trị | mặc định):
 *   control_mode    74710   CONTROL_MODE_WIFI (phải ONLINE mới cho chạy) / CONTROL_MODE_LOCAL   LOCAL
 *   access_mode     74766   ACCESS_FREE (libre-service = chế độ test, không quan tâm thẻ)
 *                           / ACCESS_CARD (badgeuse, phải có thẻ mới chạy)                    FREE
 *   card_timer      200816  CARD_TIMER_BADGEUSE (mất thẻ -> dừng + báo lỗi)
 *                           / CARD_TIMER_SISMO (mất thẻ -> chạy hết chu trình, không bắt đầu
 *                           chu trình mới) - chỉ có tác dụng khi access_mode = ACCESS_CARD        BADGEUSE
 *   motor_mode      200816  1..5 -> thanh ghi 8 (mode) của CM8                                   3
 *   motor_ratio     200816  2..17 -> thanh ghi 9 (ratio) của CM8                                 9
 *   manual_minutes  200816  5 / 90: thời gian buổi tập trang MANUAL                              5
 *
 * motor_combo_ok(mode, ratio): CM8 tính dải tốc độ v_min..v_max = 42..79 + step * (mode - 3)
 * + 3 * (ratio - 9) (step 12 / 15 / 20 cho mode <= 3 / 4 / 5) và lưu dạng số không dấu -> dải phải nằm
 * trong 0..180. Cặp không hợp lệ (Mode 1 + Ratio 2: v_min = -3, tràn số) bị từ chối khi lưu; đọc
 * file ra cặp không hợp lệ thì ratio về mặc định.
 *
 * device_settings_start() gắn (mount) phân vùng "storage" và đọc settings.json -> phải gọi TRƯỚC
 * net_start(). Mỗi setter lưu luôn cả file; getter đọc giá trị trong RAM (an toàn giữa các task).
 */
#pragma once
#include <stdbool.h>

#define STORAGE_BASE              "/spiffs"

typedef enum { CONTROL_MODE_WIFI = 0, CONTROL_MODE_LOCAL = 1 } control_mode_t;
typedef enum { ACCESS_FREE = 0, ACCESS_CARD = 1 } access_mode_t;
typedef enum { CARD_TIMER_BADGEUSE = 0, CARD_TIMER_SISMO = 1 } card_timer_t;

#define MOTOR_MODE_MIN            1
#define MOTOR_MODE_MAX            5
#define MOTOR_RATIO_MIN           2
#define MOTOR_RATIO_MAX           17

void           device_settings_start(void);
bool           device_storage_ok(void);
bool           storage_write_text(const char *path, const char *text);

control_mode_t device_get_control_mode(void);
bool           device_set_control_mode(control_mode_t m);
const char    *control_mode_name(control_mode_t m);

access_mode_t  device_get_access_mode(void);
bool           device_set_access_mode(access_mode_t m);
const char    *access_mode_name(access_mode_t m);

card_timer_t   device_get_card_timer(void);
bool           device_set_card_timer(card_timer_t t);
const char    *card_timer_name(card_timer_t t);

bool           motor_combo_ok(int mode, int ratio);
int            device_get_motor_mode(void);
bool           device_set_motor_mode(int mode);
int            device_get_motor_ratio(void);
bool           device_set_motor_ratio(int ratio);
int            device_get_manual_minutes(void);
bool           device_set_manual_minutes(int minutes);
