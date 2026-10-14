/*
 * page_setting.c - Task "setting": Sismo, mật khẩu, trang cài đặt WiFi / MODE, trang khởi động
 *
 * - Sự kiện nút từ ui_router vào hàng đợi riêng; task chờ sự kiện tối đa 50 ms mỗi vòng.
 * - Giữ Sismo: lần đổi giá trị đầu tiên = bắt đầu giữ (ở trang khác thì về trang chính ngay);
 *   màn hình báo 0x41 = thả tay (bỏ qua thay đổi còn sót IGNORE_AFTER_RELEASE_MS ngay sau đó);
 *   dự phòng: quá RELEASE_TIMEOUT_MS không thấy đổi -> coi như đã thả. Giữ đủ SISMO_HOLD_MS -> 0026.
 * - PASSWORD_TABLE: mỗi mật khẩu mở 1 trang cài đặt; thêm mật khẩu mới chỉ cần thêm 1 dòng.
 * - Trang 0027: icon mạng ghi lại khi đổi + làm mới mỗi 2 s; không bật AP khi động cơ đang chạy;
 *   rời trang quá 1 s mà vẫn đang cấu hình -> tắt AP.
 * - Trang 0028: bấm nút đổi MODE WIFI / MODE LOCAL, lưu settings.json, ghi icon 0x1041.
 * - Trước khi khởi động lại (Submit WiFi / OTA): đưa màn hình về trang 0.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/queue.h"
#include "app_config.h"
#include "hmi.h"
#include "network.h"
#include "page_session.h"
#include "logger.h"
#include "device_settings.h"
#include "page_setting.h"

#define SETTINGS_PASSWORD_LEN     5
#define SISMO_HOLD_MS             2000
#define RELEASE_TIMEOUT_MS        1200
#define IGNORE_AFTER_RELEASE_MS   400
#define PAGE_STARTUP_SHOW_MS      3000

static QueueHandle_t event_queue;

bool page_setting_on_event(const hmi_event_t *e)
{
    if (e->addr != ADDR_SISMO_BUTTON && e->addr != ADDR_PASSWORD_KEY &&
        e->addr != ADDR_SETTINGS_BUTTON && e->addr != ADDR_MODE_BUTTON)
        return false;
    if (event_queue) xQueueSend(event_queue, e, 0);
    return true;
}

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

static bool    holding = false;
static bool    entered_password = false;
static int64_t hold_start_ms = 0, last_repeat_ms = 0, ignore_until_ms = 0;

static char entered[SETTINGS_PASSWORD_LEN + 1];
static int  entered_len = 0;

static void show_stars(void) { hmi_write(ADDR_STAR_COUNT, (uint16_t)entered_len); }

static void clear_password(void)
{
    entered_len = 0;
    entered[0] = '\0';
    show_stars();
}

static uint16_t network_icon(net_state_t t)
{
    switch (t) {
    case NET_CONFIG: return NET_ICON_CONFIG;
    case NET_ONLINE: return NET_ICON_ONLINE;
    default:         return NET_ICON_OFFLINE;
    }
}

static void on_sismo(int page, const hmi_event_t *e)
{
#if DEBUG_SISMO_EVENTS
    static int64_t prev_ms = 0;
    int64_t t = ms_now();
    log_send("   [SISMO] %s gia tri %u | trang %d | cach lan truoc %lld ms",
                e->source == EV_SRC_POLL ? "doc thay doi" : "man hinh bao 0x41",
                e->value, page, (long long)(t - prev_ms));
    prev_ms = t;
#endif
    int64_t now_ms = ms_now();
    if (now_ms < ignore_until_ms) return;

    if (e->source == EV_SRC_REPORT && holding && now_ms - hold_start_ms > 300) {
        holding = false;
        ignore_until_ms = now_ms + IGNORE_AFTER_RELEASE_MS;
        return;
    }

    last_repeat_ms = now_ms;
    if (holding) return;
    holding = true;
    entered_password = false;
    hold_start_ms = last_repeat_ms;
    if (page != PAGE_MAIN) {
        hmi_goto_page(PAGE_MAIN);
        log_send("   [CAI DAT] Cham Sismo -> ve trang chinh");
    }
}

static void start_config(const char *why);
static void show_mode(void);

static void open_wifi_settings(void)
{
    hmi_write(ADDR_NETWORK_ICON, network_icon(net_get_state()));
    hmi_goto_page(PAGE_SETTINGS);
}

static void open_mode_settings(void)
{
    show_mode();
    hmi_goto_page(PAGE_MODE);
}

typedef struct { const char *password; const char *name; void (*open)(void); } password_entry_t;
static const password_entry_t PASSWORD_TABLE[] = {
    { "74700", "cai dat WiFi",          open_wifi_settings },
    { "74710", "che do WIFI / LOCAL",   open_mode_settings },
};
#define PASSWORD_COUNT (sizeof(PASSWORD_TABLE) / sizeof(PASSWORD_TABLE[0]))

static void on_password_key(uint16_t key, int page)
{
#if DEBUG_SISMO_EVENTS
    log_send("   [PHIM] %u | trang %d", key, page);
#endif
    if (page != PAGE_PASSWORD) return;
    if (key == KEY_CLEAR) { clear_password(); return; }
    if (key > 9) return;

    if (entered_len >= SETTINGS_PASSWORD_LEN) {
        log_send("   [CAI DAT] Mat khau sai -> xoa, nhap lai");
        clear_password();
        return;
    }
    entered[entered_len++] = (char)('0' + key);
    entered[entered_len] = '\0';
    show_stars();

    if (entered_len < SETTINGS_PASSWORD_LEN) return;
    for (size_t i = 0; i < PASSWORD_COUNT; i++) {
        if (strcmp(entered, PASSWORD_TABLE[i].password) == 0) {
            log_send("   [CAI DAT] Mat khau %s dung -> trang %s", PASSWORD_TABLE[i].password, PASSWORD_TABLE[i].name);
            clear_password();
            PASSWORD_TABLE[i].open();
            return;
        }
    }
}

static void start_config(const char *why)
{
    if (net_get_state() == NET_CONFIG) return;
    if (page_session_get_status().mode == SESSION_RUN) {
        log_send("   [CAI DAT] Dong co dang chay -> khong vao che do cau hinh");
        return;
    }
    hmi_write(ADDR_NETWORK_ICON, NET_ICON_CONFIG);
    log_send("   [CAI DAT] Bat che do cau hinh (%s): AP %s, mat khau %s, http://%s",
                why, net_ap_name(), NET_AP_PASSWORD, NET_AP_IP);
    net_config_start();
}

static void stop_config(const char *why)
{
    if (net_get_state() != NET_CONFIG) return;
    net_config_stop();
    hmi_write(ADDR_NETWORK_ICON, network_icon(net_get_state()));
    log_send("   [CAI DAT] Tat che do cau hinh (%s) -> tat AP, hien %s",
                why, net_state_name(net_get_state()));
}

static void on_settings_button(int page)
{
    if (page != PAGE_SETTINGS) return;
    if (net_get_state() == NET_CONFIG) stop_config("bam Configuration WIFI");
    else                                    start_config("bam OFFLINE");
}

static void show_mode(void)
{
    hmi_write(ADDR_MODE_ICON, device_get_control_mode() == CONTROL_MODE_WIFI ? 0 : 1);
}

static void on_mode_button(int page)
{
    if (page != PAGE_MODE) return;
    control_mode_t m = (device_get_control_mode() == CONTROL_MODE_WIFI) ? CONTROL_MODE_LOCAL : CONTROL_MODE_WIFI;
    device_set_control_mode(m);
    show_mode();
    if (m == CONTROL_MODE_WIFI)
        log_send("   [CHE DO] -> MODE WIFI: chi cho dong co chay khi ONLINE (hien tai: %s)",
                 net_state_name(net_get_state()));
    else
        log_send("   [CHE DO] -> MODE LOCAL: dong co chay khong can mang");
}

static void task_setting(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    int     prev_page = -1;
    int64_t last_icon_write_ms = 0, left_page_ms = 0, startup_page_since = 0;
    int     icon_written = -1;
    net_state_t prev_net = (net_state_t)-1;

    while (1) {
        hmi_event_t e;
        bool got = xQueueReceive(event_queue, &e, pdMS_TO_TICKS(50)) == pdTRUE;
        esp_task_wdt_reset();

        hmi_data_t d = hmi_get();
        int page = d.page;

        if (got) {
            if      (e.addr == ADDR_SISMO_BUTTON)     on_sismo(page, &e);
            else if (e.addr == ADDR_PASSWORD_KEY) on_password_key(e.value, page);
            else if (e.addr == ADDR_SETTINGS_BUTTON)   on_settings_button(page);
            else if (e.addr == ADDR_MODE_BUTTON)       on_mode_button(page);
        }

        int64_t now = ms_now();

        if (holding) {
            if (now - last_repeat_ms > RELEASE_TIMEOUT_MS) {
                holding = false;
            } else if (!entered_password && now - hold_start_ms >= SISMO_HOLD_MS) {
                entered_password = true;
                clear_password();
                hmi_goto_page(PAGE_PASSWORD);
                log_send("   [CAI DAT] Giu Sismo %d ms -> trang mat khau", SISMO_HOLD_MS);
            }
        }

        if (page == PAGE_STARTUP) {
            if (!startup_page_since) startup_page_since = now;
            else if (now - startup_page_since >= PAGE_STARTUP_SHOW_MS) {
                log_send("   [CAI DAT] Trang khoi dong %d ms -> trang chinh", PAGE_STARTUP_SHOW_MS);
                hmi_goto_page(PAGE_MAIN);
                startup_page_since = now;
            }
        } else {
            startup_page_since = 0;
        }

        if (page != prev_page) {
            if (page == PAGE_PASSWORD) clear_password();
            if (page == PAGE_SETTINGS)  icon_written = -1;
            if (page == PAGE_MODE)      show_mode();
            prev_page = page;
        }

        net_state_t m = net_get_state();
        if (m != prev_net) {
            log_send("   [MANG] %s", net_state_name(m));
            prev_net = m;
        }
        int icon = network_icon(m);
        if (icon != icon_written || now - last_icon_write_ms >= 2000) {
            hmi_write(ADDR_NETWORK_ICON, (uint16_t)icon);
            icon_written = icon;
            last_icon_write_ms = now;
        }

        if (m == NET_CONFIG && page >= 0 && page != PAGE_SETTINGS) {
            if (!left_page_ms) left_page_ms = now;
            else if (now - left_page_ms > 1000) {
                stop_config("roi trang cai dat");
                left_page_ms = 0;
            }
        } else {
            left_page_ms = 0;
        }
    }
}

static void before_restart(void)
{
    hmi_goto_page(PAGE_STARTUP);
    vTaskDelay(pdMS_TO_TICKS(30));
}

void page_setting_start(void)
{
    event_queue = xQueueCreate(16, sizeof(hmi_event_t));
    net_set_before_restart_hook(before_restart);
    xTaskCreatePinnedToCore(task_setting, "setting", STACK_SETTING, NULL,
                            PRIO_SETTING, NULL, APP_CORE);
}
