/*
 * hmi_map.c - Tên trang (để in log "[Trang] ...") và nhóm trang buổi tập của UI HMI_E8
 *
 * Trang buổi tập dùng page_session: 10 Manuel, 11 Durée (Programmes / Perso 6 / 14: page_program).
 * Trang "Durée Exercices": 0002 (chọn 1..10 phút bằng nút - / +).
 * Programmes: 0005 mức độ, 0006 / 0014 buổi tập (có / không nút X), 0013 bài tập con.
 * Perso: 0007 chọn 1..6 -> 0014 (cùng trang buổi tập với Gainage / Relaxation / Proprioception).
 * Trang trống (không widget, dùng chung ảnh nền 0002): 0008, 0009, 0012, 0015..0020.
 */
#include <stdio.h>
#include "hmi_map.h"

bool hmi_is_session_page(int page) { return page == 10 || page == 11; }

void hmi_page_name(int page, char *out, size_t n)
{
    if ((page >= 15 && page <= 20) || page == 8 || page == 9 || page == 12) { snprintf(out, n, "Trang trống %d", page); return; }
    const char *s;
    switch (page) {
    case 0:  s = "Khởi động"; break;
    case 1:  s = "Menu chính"; break;
    case 2:  s = "Durée Exercices - chọn số phút"; break;
    case 3:  s = "Programmes - chọn mục tiêu"; break;
    case 4:  s = "Tonification - chọn nhóm cơ"; break;
    case 21: s = "Remodelage - chọn nhóm cơ"; break;
    case 22: s = "Anti-cellulite - chọn nhóm cơ"; break;
    case 23: s = "Gainage - chọn nhóm cơ"; break;
    case 24: s = "Relaxation Drainage - chọn nhóm cơ"; break;
    case 25: s = "Proprioception - chọn 1..6"; break;
    case 5:  s = "Chọn cấp độ"; break;
    case 6:  s = "Buổi tập Programmes (có nút X)"; break;
    case 13: s = "Programmes - bài tập con"; break;
    case 14: s = "Buổi tập Programmes / Perso"; break;
    case 7:  s = "Perso - chọn 1..6"; break;
    case 10: s = "Buổi tập Manuel"; break;
    case 11: s = "Buổi tập Durée"; break;
    case 26: s = "Mật khẩu cài đặt"; break;
    case 27: s = "Cài đặt WiFi (74700)"; break;
    case 28: s = "Chế độ điều khiển (74710)"; break;
    case 29: s = "Thẻ / ngôn ngữ (74766)"; break;
    case 30: s = "Mode / Timer / Ratio / Manual (200816)"; break;
    default: snprintf(out, n, "Trang %d", page); return;
    }
    snprintf(out, n, "%s", s);
}
