/*
 * ui_router.c - Chia sự kiện màn hình cho từng trang (xem ui_router.h)
 */
#include <stdio.h>
#include "app_config.h"
#include "hmi.h"
#include "logger.h"
#include "page_session.h"
#include "page_setting.h"
#include "ui_router.h"

// Chạy trong task hmi_rx: các page chỉ đưa sự kiện vào hàng đợi của mình rồi trả về ngay
static void dispatch(const hmi_event_t *e)
{
    if (page_session_on_event(e)) return;
    if (page_setting_on_event(e)) return;
#if DEBUG_UNHANDLED_EVENTS
    log_send("   [ROUTER] Su kien khong trang nao xu ly: addr=0x%04X value=%u", e->addr, e->value);
#endif
}

// Hook trong task logger: in tên trang mỗi khi màn hình đổi trang
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
    logger_add_hook(report_page);        // đăng ký trước page_session để "[Trang]" in trước chi tiết buổi tập
    hmi_set_event_handler(dispatch);
}
