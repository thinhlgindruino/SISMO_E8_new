/*
 * page_session.h - Các trang buổi tập Manuel 10, Durée 11 + động cơ cho Programmes / Perso (6, 14)
 *
 * Task "session" (ưu tiên cao nhất, chu kỳ 50 ms):
 *   lấy bản sao dữ liệu màn hình + trạng thái CM8 -> quyết định lệnh gửi CM8,
 *   cường độ / Xung trên đồng hồ, xử lý lỗi / mất kết nối (bắt màn hình tạm dừng).
 * Trang Programmes 0006 / 0014: task program gọi page_session_program(chạy, mức 0..32) mỗi chu kỳ;
 * khi an toàn / thẻ / mạng / CM8 bắt dừng thì page_session_program_take_pause() trả về true một lần.
 * CM8 mất kết nối / báo lỗi 2 khi đang tập (hoặc bấm ▶): page_session_take_cm8_lost() trả về true một lần ->
 * task setting đưa màn hình về trang khởi động và chờ CM8.
 * Kèm hook in tiến trình buổi tập (bắt đầu / tạm dừng / còn lại / cường độ) trong task logger.
 *
 * Trạng thái động cơ (session_motor_t):
 *   SESSION_FAN_ONLY  vừa bật máy: lệnh 1, Mode 0 -> quạt quay, động cơ đứng
 *   SESSION_PREPARE   lần chạy đầu: ghi Mode/Ratio + lệnh 2, chờ CM8 về trạng thái 0
 *   SESSION_RUN       lệnh 1 + tốc độ theo cường độ
 *   SESSION_STOP      lệnh 2: CM8 giảm tốc về 0, tắt quạt
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hmi.h"

typedef enum {
    SESSION_FAN_ONLY,
    SESSION_PREPARE,
    SESSION_RUN,
    SESSION_STOP
} session_motor_t;

typedef struct {
    session_motor_t mode;
    uint16_t        speed_sent;
    int             target_level;
    int             intensity;
    bool            run_allowed;
} session_status_t;

void             page_session_start(void);
bool             page_session_on_event(const hmi_event_t *e);
session_status_t page_session_get_status(void);
const char      *session_motor_name(session_motor_t m);
void             page_session_program(bool run, int level);
bool             page_session_program_take_pause(void);
bool             page_session_take_cm8_lost(void);
