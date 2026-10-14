/*
 * hmi_map.c - Tên trang (để in log "[Trang] ...") và nhóm trang buổi tập của UI HMI_E8
 *
 * Trang buổi tập: 6 Programmes, 8 Perso, 10 Manuel, 11 Durée.
 * Trang "Durée Exercices - chọn N phút": 0002 = 1 phút, 0012..0020 = 2..10 phút.
 */
#include <stdio.h>
#include "hmi_map.h"

bool hmi_is_session_page(int page) { return page == 6 || page == 8 || page == 10 || page == 11; }

static int duree_page_minutes(int page)
{
    if (page == 2) return 1;
    if (page >= 12 && page <= 20) return page - 10;
    return 0;
}

void hmi_page_name(int page, char *out, size_t n)
{
    int minutes = duree_page_minutes(page);
    if (minutes) { snprintf(out, n, "Durée Exercices - chọn %d phút", minutes); return; }
    const char *s;
    switch (page) {
    case 0:  s = "Khởi động"; break;
    case 1:  s = "Menu chính"; break;
    case 3:  s = "Programmes - chọn mục tiêu"; break;
    case 4:  s = "Tonification - chọn nhóm cơ"; break;
    case 21: s = "Remodelage - chọn nhóm cơ"; break;
    case 22: s = "Anti-cellulite - chọn nhóm cơ"; break;
    case 23: s = "Gainage - chọn nhóm cơ"; break;
    case 24: s = "Relaxation Drainage - chọn nhóm cơ"; break;
    case 25: s = "Proprioception - chọn 1..6"; break;
    case 5:  s = "Chọn cấp độ"; break;
    case 6:  s = "Buổi tập Programmes"; break;
    case 7:  s = "Perso - chọn 1..6"; break;
    case 8:  s = "Buổi tập Perso"; break;
    case 9:  s = "Mot de passe (cũ, không dùng)"; break;
    case 10: s = "Buổi tập Manuel"; break;
    case 11: s = "Buổi tập Durée"; break;
    case 26: s = "Mật khẩu cài đặt"; break;
    case 27: s = "Cài đặt WiFi (74700)"; break;
    case 28: s = "Chế độ điều khiển (74710)"; break;
    default: snprintf(out, n, "Trang %d", page); return;
    }
    snprintf(out, n, "%s", s);
}
