/*
 * control.h - Task điều khiển (ưu tiên cao nhất, chu kỳ 50 ms)
 *
 * Lấy bản sao dữ liệu màn hình + trạng thái CM8 -> quyết định lệnh gửi CM8,
 * cập nhật đồng hồ cường độ trên màn hình, xử lý lỗi / mất kết nối.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CM8_FAN_ONLY,   // vừa bật máy: lệnh 1, Mode 0 -> quạt quay, động cơ đứng
    CM8_PREPARE,   // lần chạy đầu: ghi Mode/Ratio + lệnh 2, chờ CM8 về trạng thái 0
    CM8_RUN,       // lệnh 1 + tốc độ theo cường độ
    CM8_STOP        // lệnh 2: CM8 giảm tốc về 0, tắt quạt
} control_mode_t;

typedef struct {
    control_mode_t mode;
    uint16_t     speed_sent;     // tốc độ đang gửi CM8 (0..32)
    int          target_level;       // mức 1..32 (đã tính xung), 0 = không chạy
    int          intensity;       // cường độ người dùng chọn 0..31 (chưa tính xung)
    bool         run_allowed;
} control_status_t;

void control_start(void);
control_status_t control_get_status(void);
const char *control_mode_name(control_mode_t c);
