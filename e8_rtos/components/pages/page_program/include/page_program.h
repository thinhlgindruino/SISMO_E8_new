/*
 * page_program.h - Programmes + Perso: chuyển trang, hiện bài, chạy bài (bíp, biểu đồ, Xung, động cơ), task "program" (ưu tiên 3)
 *
 * Luồng trang: 0003 Programmes -> 0004 / 0021..0024 chọn bài (ghi 0x1007 = 0..15) -> 0005 chọn mức độ
 * (3 nút 0x1009 = 0 / 1 / 2, báo về ESP32) -> ESP32 mở 0006 (bài có nút X: Tonification, Remodelage,
 * Anti-cellulite) hoặc 0014 (Gainage, Relaxation). Proprioception: 0025 ghi 0x1007 = 16..21 rồi tự
 * sang 0014 (không chọn mức độ). Perso: 0007 ghi 0x1007 = 22..27 rồi tự sang 0014 (giống Proprioception).
 * Trang buổi tập: hình (0x1050), tên bài (0x1051), trục 10 / 2 phút (0x1052), thời gian MM:SS
 * (0x1053 / 0x1054), vòng cường độ (0x1055), biểu đồ = Progress Bar 0x105A (ảnh đổi theo bài).
 * Nút 0x1059: 1 X -> trang 0013 xem bài tập con (15 ô 0x1080..0x108E + mũi tên 0x108F, bảng ô trong
 * grid_data.c; Remodelage Complet có 2 trang, đổi bằng nút mũi tên = 5; chỉ dùng được khi chưa chạy); 6 nút < -> về 0005
 * (Proprioception về 0025, Perso về 0007; đang chạy thì dừng bài); 2 Xung bật / tắt; 3 Play: bíp 5 lần rồi chạy /
 * tạm dừng / chạy tiếp; 4 Stop: dừng, vẽ lại trang như lúc mới vào (hết bài cũng vậy).
 * Động cơ chạy theo mức cột hiện tại qua task session (page_session_program).
 * page_program_get_info(): bản sao trạng thái bài đang mở (cho cloud báo lên ThingsBoard); điều khiển từ xa
 * gửi nút Play / Stop qua page_program_on_event() như màn hình báo.
 */
#pragma once
#include <stdbool.h>
#include "hmi.h"

typedef enum { PROGRAM_IDLE = 0, PROGRAM_COUNTDOWN, PROGRAM_RUN, PROGRAM_PAUSE } program_state_t;

typedef struct {
    program_state_t state;
    int             program;
    int             level;
    int             remaining_s;
    int             total_s;
    const char     *name;
} page_program_info_t;

void page_program_start(void);
void page_program_get_info(page_program_info_t *out);
bool page_program_on_event(const hmi_event_t *e);
