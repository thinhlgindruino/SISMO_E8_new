/*
 * page_session.h - Các trang buổi tập (Programmes 6, Perso 8, Manuel 10, Durée 11)
 *
 * Task "session" (ưu tiên cao nhất, chu kỳ 50 ms):
 *   lấy bản sao dữ liệu màn hình + trạng thái CM8 -> quyết định lệnh gửi CM8,
 *   cường độ / Xung trên đồng hồ, xử lý lỗi / mất kết nối (bắt màn hình tạm dừng).
 * Kèm hook in tiến trình buổi tập (bắt đầu / tạm dừng / còn lại / cường độ) trong task logger.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hmi.h"

typedef enum {
    SESSION_FAN_ONLY,   // vừa bật máy: lệnh 1, Mode 0 -> quạt quay, động cơ đứng
    SESSION_PREPARE,    // lần chạy đầu: ghi Mode/Ratio + lệnh 2, chờ CM8 về trạng thái 0
    SESSION_RUN,        // lệnh 1 + tốc độ theo cường độ
    SESSION_STOP        // lệnh 2: CM8 giảm tốc về 0, tắt quạt
} session_motor_t;

typedef struct {
    session_motor_t mode;
    uint16_t        speed_sent;     // tốc độ đang gửi CM8 (0..32)
    int             target_level;   // mức 1..32 (đã tính Xung), 0 = không chạy
    int             intensity;      // cường độ người dùng chọn 0..31 (chưa tính Xung)
    bool            run_allowed;
} session_status_t;

void             page_session_start(void);
bool             page_session_on_event(const hmi_event_t *e);   // ui_router gọi; true = trang này nhận
session_status_t page_session_get_status(void);
const char      *session_motor_name(session_motor_t m);
