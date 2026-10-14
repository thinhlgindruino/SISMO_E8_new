/*
 * ui_router.c - Chia sự kiện màn hình cho từng trang + in tên trang khi đổi trang (xem ui_router.h)
 *
 * dispatch() chạy trong task hmi_rx: hỏi lần lượt page_session, page_setting; trang nào nhận thì dừng.
 * Các page chỉ đưa sự kiện vào hàng đợi của mình rồi trả về ngay.
 * report_page() là hook trong task logger: in "[Trang] <tên>" mỗi khi màn hình đổi trang; đăng ký
 * trước page_session để dòng tên trang in trước chi tiết buổi tập.
 */
#include <stdio.h>
#include "app_config.h"
#include "hmi.h"
#include "logger.h"
#include "page_session.h"
#include "page_setting.h"
#include "ui_router.h"

static void dispatch(const hmi_event_t *e)
{
    if (page_session_on_event(e)) return;
    if (page_setting_on_event(e)) return;
#if DEBUG_UNHANDLED_EVENTS
    log_send("   [ROUTER] Su kien khong trang nao xu ly: addr=0x%04X value=%u", e->addr, e->value);
#endif
}

static void report_page(void)
{
    static int prev_page = -1;
    hmi_data_t d = hmi_get();
    if (!d.has_vars || d.page < 0 || d.page == prev_page) return;
    char name[64];
    hmi_page_name(d.page, name, sizeof(name));
    printf("\n[Trang] %s\n", name);
    prev_page = d.page;
}

void ui_router_start(void)
{
    logger_add_hook(report_page);
    hmi_set_event_handler(dispatch);
}
