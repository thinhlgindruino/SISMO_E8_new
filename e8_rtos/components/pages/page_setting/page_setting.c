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
 * - Trang 0029: "E8 CM8 - Statut" là trạng thái THẺ (ghi mỗi 300 ms): Accès libre-service (chế độ
 *   test) -> luôn connecté; Accès badgeuse -> connecté khi 2 dây thẻ của CM8 đang chập (thanh ghi 18 = 1)
 *   và CM8 còn trả lời, ngược lại déconnecté.
 * - Trang 0030: bấm Mode / Motor ratio tăng 1 (hết thì quay về đầu), 2 nút gạt đổi qua lại. Cặp
 *   Mode + ratio làm CM8 tràn số (motor_combo_ok, vd. Mode 1 + Ratio 2) bị bỏ qua, nhảy sang giá trị kế.
 * - Ngôn ngữ (0x1030, 0..5): nút trang 0029 tự tăng vòng trên màn hình và báo về -> lưu settings.json,
 *   ghi lại icon chữ phụ thuộc trạng thái của trang đang mở. Mỗi lần nối được màn hình (khởi động / màn
 *   hình vừa bật lại) ghi 0x1030 = ngôn ngữ đã lưu. Icon chữ phụ thuộc trạng thái = LANG_ICON(trạng thái,
 *   ngôn ngữ): mạng 0027, Statut / Accès 0029, Mode / Timer 0030.
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
#include "motor_modbus.h"
#include "page_setting.h"

#define PASSWORD_MIN_LEN          5
#define PASSWORD_MAX_LEN          6
#define SISMO_HOLD_MS             2000
#define RELEASE_TIMEOUT_MS        1200
#define IGNORE_AFTER_RELEASE_MS   400
#define PAGE_STARTUP_SHOW_MS      3000

static QueueHandle_t event_queue;

bool page_setting_on_event(const hmi_event_t *e)
{
    switch (e->addr) {
    case ADDR_SISMO_BUTTON: case ADDR_PASSWORD_KEY: case ADDR_SETTINGS_BUTTON: case ADDR_MODE_BUTTON:
    case ADDR_ACCESS_BUTTON: case ADDR_MOTOR_MODE_BUTTON: case ADDR_CARD_TIMER_BUTTON:
    case ADDR_MOTOR_RATIO_BUTTON: case ADDR_MANUAL_BUTTON: case ADDR_LANGUAGE:
        break;
    default:
        return false;
    }
    if (event_queue) xQueueSend(event_queue, e, 0);
    return true;
}

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

static bool    holding = false;
static bool    entered_password = false;
static int64_t hold_start_ms = 0, last_repeat_ms = 0, ignore_until_ms = 0;

static char entered[PASSWORD_MAX_LEN + 1];
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
    case NET_CONFIG: return (uint16_t)(NET_ICON_CONFIG_BASE + device_get_language());
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
static void show_access_page(void);
static void show_factory_page(void);

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

static void open_access_settings(void)
{
    show_access_page();
    hmi_goto_page(PAGE_ACCESS);
}

static void open_factory_settings(void)
{
    show_factory_page();
    hmi_goto_page(PAGE_FACTORY);
}

typedef struct { const char *password; const char *name; void (*open)(void); } password_entry_t;
static const password_entry_t PASSWORD_TABLE[] = {
    { "74700",  "cai dat WiFi",                    open_wifi_settings },
    { "74710",  "che do WIFI / LOCAL",             open_mode_settings },
    { "74766",  "the + ngon ngu",                  open_access_settings },
    { "200816", "Mode / Timer / Ratio / Manual",   open_factory_settings },
};
#define PASSWORD_COUNT (sizeof(PASSWORD_TABLE) / sizeof(PASSWORD_TABLE[0]))

static bool is_prefix_of_password(void)
{
    for (size_t i = 0; i < PASSWORD_COUNT; i++)
        if (strlen(PASSWORD_TABLE[i].password) > (size_t)entered_len &&
            strncmp(PASSWORD_TABLE[i].password, entered, entered_len) == 0)
            return true;
    return false;
}

static void on_password_key(uint16_t key, int page)
{
#if DEBUG_SISMO_EVENTS
    log_send("   [PHIM] %u | trang %d", key, page);
#endif
    if (page != PAGE_PASSWORD) return;
    if (key == KEY_CLEAR) { clear_password(); return; }
    if (key > 9) return;

    if (entered_len >= PASSWORD_MAX_LEN || (entered_len >= PASSWORD_MIN_LEN && !is_prefix_of_password())) {
        log_send("   [CAI DAT] Mat khau sai -> xoa, nhap lai");
        clear_password();
        return;
    }
    entered[entered_len++] = (char)('0' + key);
    entered[entered_len] = '\0';
    show_stars();

    if (entered_len < PASSWORD_MIN_LEN) return;
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
    hmi_write(ADDR_NETWORK_ICON, network_icon(NET_CONFIG));
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

static uint16_t card_status_icon(void)
{
    motor_status_t t = motor_get_status();
    bool on = device_get_access_mode() == ACCESS_FREE || (t.connected && t.card == 1);
    return LANG_ICON(on ? 1 : 0, device_get_language());
}

static void show_access_page(void)
{
    hmi_write(ADDR_ACCESS_ICON, LANG_ICON(device_get_access_mode() == ACCESS_CARD ? 1 : 0, device_get_language()));
    hmi_write(ADDR_CM8_STATUS_ICON, card_status_icon());
}

static void on_access_button(int page)
{
    if (page != PAGE_ACCESS) return;
    access_mode_t m = (device_get_access_mode() == ACCESS_CARD) ? ACCESS_FREE : ACCESS_CARD;
    device_set_access_mode(m);
    show_access_page();
    log_send("   [THE] -> %s%s", access_mode_name(m),
             m == ACCESS_CARD ? " (phai co the moi chay)" : " (khong quan tam the)");
}

static void show_factory_page(void)
{
    int lang = device_get_language();
    hmi_write(ADDR_MOTOR_MODE_ICON,  LANG_ICON(device_get_motor_mode() - MOTOR_MODE_MIN, lang));
    hmi_write(ADDR_CARD_TIMER_ICON,  LANG_ICON(device_get_card_timer() == CARD_TIMER_SISMO ? 1 : 0, lang));
    hmi_write(ADDR_MOTOR_RATIO_ICON, (uint16_t)(device_get_motor_ratio() - MOTOR_RATIO_MIN));
    hmi_write(ADDR_MANUAL_ICON,      device_get_manual_minutes() == 90 ? 1 : 0);
}

static void on_factory_button(uint16_t addr, int page)
{
    if (page != PAGE_FACTORY) return;
    if (page_session_get_status().mode == SESSION_RUN) {
        log_send("   [200816] Dong co dang chay -> khong doi cai dat");
        return;
    }
    if (addr == ADDR_MOTOR_MODE_BUTTON) {
        int m = device_get_motor_mode(), r = device_get_motor_ratio();
        do {
            if (++m > MOTOR_MODE_MAX) m = MOTOR_MODE_MIN;
        } while (!motor_combo_ok(m, r));
        device_set_motor_mode(m);
        log_send("   [200816] Mode %d (thanh ghi 8 cua CM8)", m);
    } else if (addr == ADDR_CARD_TIMER_BUTTON) {
        card_timer_t t = device_get_card_timer() == CARD_TIMER_SISMO ? CARD_TIMER_BADGEUSE : CARD_TIMER_SISMO;
        device_set_card_timer(t);
        log_send("   [200816] Timer %s%s", card_timer_name(t),
                 t == CARD_TIMER_SISMO ? " (mat the: chay het chu trinh, khong bat dau chu trinh moi)"
                                       : " (mat the: dung + bao loi)");
    } else if (addr == ADDR_MOTOR_RATIO_BUTTON) {
        int m = device_get_motor_mode(), r = device_get_motor_ratio();
        do {
            if (++r > MOTOR_RATIO_MAX) r = MOTOR_RATIO_MIN;
        } while (!motor_combo_ok(m, r));
        device_set_motor_ratio(r);
        log_send("   [200816] Motor ratio %d (thanh ghi 9 cua CM8)", r);
    } else if (addr == ADDR_MANUAL_BUTTON) {
        int min = device_get_manual_minutes() == 5 ? 90 : 5;
        device_set_manual_minutes(min);
        log_send("   [200816] MANUAL %d phut", min);
    }
    show_factory_page();
}

static void on_language(uint16_t value, int page)
{
    if (value >= LANGUAGE_COUNT || (int)value == device_get_language()) return;
    device_set_language(value);
    log_send("   [NGON NGU] -> %s", language_name(value));
    if (page == PAGE_ACCESS)  show_access_page();
    if (page == PAGE_FACTORY) show_factory_page();
    if (page == PAGE_SETTINGS) hmi_write(ADDR_NETWORK_ICON, network_icon(net_get_state()));
}

static void task_setting(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    int     prev_page = -1;
    int64_t last_icon_write_ms = 0, left_page_ms = 0, startup_page_since = 0, last_cm8_icon_ms = 0;
    int     icon_written = -1;
    net_state_t prev_net = (net_state_t)-1;
    bool    was_linked = false;

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
            else if (e.addr == ADDR_ACCESS_BUTTON)     on_access_button(page);
            else if (e.addr == ADDR_LANGUAGE)          on_language(e.value, page);
            else                                       on_factory_button(e.addr, page);
        }

        int64_t now = ms_now();

        bool linked = hmi_is_linked(&d);
        if (linked && !was_linked) {
            hmi_write(ADDR_LANGUAGE, (uint16_t)device_get_language());
            log_send("   [NGON NGU] %s", language_name(device_get_language()));
        }
        was_linked = linked;

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
            if (page == PAGE_ACCESS)    show_access_page();
            if (page == PAGE_FACTORY)   show_factory_page();
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

        if (page == PAGE_ACCESS && now - last_cm8_icon_ms >= 300) {
            hmi_write(ADDR_CM8_STATUS_ICON, card_status_icon());
            last_cm8_icon_ms = now;
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
