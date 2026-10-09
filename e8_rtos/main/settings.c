/*
 * settings.c - Task cài đặt (xem settings.h)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "config.h"
#include "hmi.h"
#include "network.h"
#include "control.h"
#include "logger.h"
#include "settings.h"

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

// ---- Giữ "Sismo" ----
static bool    holding = false;
static bool    entered_password = false;
static int64_t hold_start_ms = 0, last_repeat_ms = 0, ignore_until_ms = 0;

// ---- Mật khẩu ----
static char entered[SETTINGS_PASSWORD_LEN + 1];
static int  entered_len = 0;

static void show_stars(void) { hmi_write(ADDR_STAR_COUNT, (uint16_t)entered_len); }

static void clear_password(void)
{
    entered_len = 0;
    entered[0] = '\0';
    show_stars();
}

static void goto_page(int page)
{
    hmi_write(0x7000, (uint16_t)page);
}

static uint16_t network_icon(net_state_t t)
{
    switch (t) {
    case NET_CONFIG: return 1;
    case NET_ONLINE:   return 2;
    default:            return 0;   // OFFLINE và đang kết nối đều hiện OFFLINE
    }
}

// ---------------- Xử lý từng loại nút ----------------
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
    if (now_ms < ignore_until_ms) return;           // thay đổi còn sót ngay sau lúc thả tay

    // Màn hình báo 0x41 lúc THẢ TAY -> kết thúc lần giữ ngay (không chờ hết RELEASE_TIMEOUT_MS).
    // (Nếu 0x41 tới ngay đầu lần bấm thì hold_start_ms mới vài chục ms -> vẫn coi là đang bấm.)
    if (e->source == EV_SRC_REPORT && holding && now_ms - hold_start_ms > 300) {
        holding = false;
        ignore_until_ms = now_ms + IGNORE_AFTER_RELEASE_MS;
        return;
    }

    last_repeat_ms = now_ms;
    if (holding) return;                       // đang giữ: chỉ cập nhật thời điểm lặp
    holding = true;
    entered_password = false;
    hold_start_ms = last_repeat_ms;
    if (page != PAGE_MAIN) {
        goto_page(PAGE_MAIN);              // chạm ở trang khác -> về trang chính ngay
        log_send("   [CAI DAT] Cham Sismo -> ve trang chinh");
    }
}

static void start_config(const char *why);

// Giống E8 cũ: không có nút xác nhận.
//   - Đủ 5 số mà đúng 74700 -> tự sang trang cài đặt
//   - Đủ 5 số mà sai -> đứng yên, bấm thêm 1 số (số thứ 6) -> xóa hết, nhập lại từ đầu
//   - C -> xóa hết
static void on_password_key(uint16_t key, int page)
{
#if DEBUG_SISMO_EVENTS
    log_send("   [PHIM] %u | trang %d", key, page);
#endif
    if (page != PAGE_PASSWORD) return;
    if (key == KEY_CLEAR) { clear_password(); return; }
    if (key > 9) return;

    if (entered_len >= SETTINGS_PASSWORD_LEN) {        // số thứ 6 -> reset
        log_send("   [CAI DAT] Mat khau sai -> xoa, nhap lai");
        clear_password();
        return;
    }
    entered[entered_len++] = (char)('0' + key);
    entered[entered_len] = '\0';
    show_stars();

    if (entered_len == SETTINGS_PASSWORD_LEN && strcmp(entered, SETTINGS_PASSWORD) == 0) {
        log_send("   [CAI DAT] Mat khau dung -> trang cai dat");
        clear_password();
        // Vào trang cài đặt: hiện trạng thái hiện tại (ONLINE / OFFLINE), bấm vào mới bật cấu hình
        hmi_write(ADDR_NETWORK_ICON, network_icon(net_get_state()));
        goto_page(PAGE_SETTINGS);
    }
}

static void start_config(const char *why)
{
    if (net_get_state() == NET_CONFIG) return;             // đang cấu hình rồi
    if (control_get_status().mode == CM8_RUN) {  // phòng hờ: không bật AP khi động cơ còn chạy
        log_send("   [CAI DAT] Dong co dang chay -> khong vao che do cau hinh");
        return;
    }
    hmi_write(ADDR_NETWORK_ICON, 1);              // hiện "Configuration WIFI" ngay
    log_send("   [CAI DAT] Bat che do cau hinh (%s): AP %s, mat khau %s, http://%s",
                why, net_ap_name(), NET_AP_PASSWORD, NET_AP_IP);
    net_config_start();
}

static void stop_config(const char *why)
{
    if (net_get_state() != NET_CONFIG) return;
    net_config_stop();                         // tắt AP, về trạng thái hiện tại (ONLINE vẫn là ONLINE)
    hmi_write(ADDR_NETWORK_ICON, network_icon(net_get_state()));
    log_send("   [CAI DAT] Tat che do cau hinh (%s) -> tat AP, hien %s",
                why, net_state_name(net_get_state()));
}

// Nút trên trang cài đặt: OFFLINE/ONLINE -> bấm -> Configuration WIFI; Configuration WIFI -> bấm -> về trạng thái hiện tại
static void on_settings_button(int page)
{
    if (page != PAGE_SETTINGS) return;
    if (net_get_state() == NET_CONFIG) stop_config("bam Configuration WIFI");
    else                                    start_config("bam OFFLINE");
}

// ---------------- Task ----------------
static void task_settings(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    int     prev_page = -1;
    int64_t last_icon_write_ms = 0, left_page_ms = 0, startup_page_since = 0;
    int     icon_written = -1;
    net_state_t prev_net = (net_state_t)-1;

    while (1) {
        hmi_event_t e;
        bool got = hmi_get_event(&e, pdMS_TO_TICKS(50));
        esp_task_wdt_reset();

        hmi_data_t d = hmi_get();
        int page = d.page;

        if (got) {
            if      (e.addr == ADDR_SISMO_BUTTON)     on_sismo(page, &e);
            else if (e.addr == ADDR_PASSWORD_KEY) on_password_key(e.value, page);
            else if (e.addr == ADDR_SETTINGS_BUTTON)   on_settings_button(page);
        }

        int64_t now = ms_now();

        // ---- Giữ "Sismo": thả tay hay đủ 3 s? ----
        if (holding) {
            if (now - last_repeat_ms > RELEASE_TIMEOUT_MS) {
                holding = false;                                   // đã thả tay
            } else if (!entered_password && now - hold_start_ms >= SISMO_HOLD_MS) {
                entered_password = true;
                clear_password();
                goto_page(PAGE_PASSWORD);
                log_send("   [CAI DAT] Giu Sismo %d ms -> trang mat khau", SISMO_HOLD_MS);
            }
        }

        // ---- Trang 0 (khởi động) không tự chuyển: hiện đủ PAGE_STARTUP_SHOW_MS thì ESP32 đưa sang trang chính ----
        if (page == PAGE_STARTUP) {
            if (!startup_page_since) startup_page_since = now;
            else if (now - startup_page_since >= PAGE_STARTUP_SHOW_MS) {
                log_send("   [CAI DAT] Trang khoi dong %d ms -> trang chinh", PAGE_STARTUP_SHOW_MS);
                goto_page(PAGE_MAIN);
                startup_page_since = now;                                  // chưa đổi kịp thì 3 s sau ghi lại
            }
        } else {
            startup_page_since = 0;
        }

        // ---- Vừa vào trang mật khẩu (kể cả bằng nút thường) -> xóa ô nhập ----
        if (page != prev_page) {
            if (page == PAGE_PASSWORD) clear_password();
            if (page == PAGE_SETTINGS)  icon_written = -1;          // ghi lại icon trạng thái
            prev_page = page;
        }

        // ---- Icon trạng thái mạng: ghi khi đổi + làm mới mỗi 2 s ----
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

        // ---- Rời trang cài đặt khi đang cấu hình (quá 1 s) -> tắt AP, về chế độ thường ----
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

// Submit WiFi trên web: trước khi ESP32 khởi động lại, đưa màn hình về trang 0 (như lúc mới bật máy)
static void before_restart(void)
{
    hmi_write(0x7000, PAGE_STARTUP);
    vTaskDelay(pdMS_TO_TICKS(30));               // chờ lệnh gửi xong qua UART
}

void settings_start(void)
{
    net_set_before_restart_hook(before_restart);
    xTaskCreatePinnedToCore(task_settings, "settings", STACK_SETTINGS, NULL,
                            PRIO_SETTINGS, NULL, APP_CORE);
}
