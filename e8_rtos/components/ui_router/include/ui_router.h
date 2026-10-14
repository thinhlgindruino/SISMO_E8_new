/*
 * ui_router.h - Chia sự kiện màn hình cho từng trang (giống ui_router của mẫu SISMO_E8_new)
 *
 *   hmi (task hmi_rx) --sự kiện nút--> ui_router --> page_session_on_event()  (cường độ +/-)
 *                                                --> page_setting_on_event()  (Sismo, phím, nút WiFi)
 *   Trang nào nhận (trả về true) thì dừng. Thêm trang mới: viết page_xxx_on_event() rồi thêm 1 dòng ở đây.
 *
 * Kèm hook in "[Trang] <tên>" mỗi khi màn hình đổi trang.
 */
#pragma once

void ui_router_start(void);
