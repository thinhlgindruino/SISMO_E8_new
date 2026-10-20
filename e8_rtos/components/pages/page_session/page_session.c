/*
 * page_session.c - Task "session": điều khiển động cơ CM8 theo màn hình + an toàn (xem page_session.h)
 *
 * Khởi động an toàn: chỉ ĐỌC CM8 trước. CM8 đã có Mode (ESP32 vừa reset giữa chừng) -> DỪNG,
 * không dùng "chỉ quạt" (CM8 nhớ Mode, lệnh 1 sẽ làm động cơ quay chậm nhất). ESP32 vừa khởi động
 * lại vì lỗi (watchdog / crash) hoặc mất liên lạc màn hình giữa buổi tập -> bắt màn hình tạm dừng,
 * người dùng phải bấm ▶ lại.
 *
 * Cho chạy khi: còn liên lạc màn hình, đang ở trang buổi tập, 0x1001 = 1, không bị bắt dừng,
 * (MODE LOCAL hoặc ONLINE), và (Accès libre-service hoặc có thẻ). Không đủ -> ghi 0x1001 = 0 (tạm dừng).
 * Trang Programmes (0006 / 0014): task program báo "đang chạy + mức 0..32" qua page_session_program()
 * thay cho 0x1001 / 0x1000 (mức 0 = lúc bíp đếm ngược: động cơ chuẩn bị, tốc độ 0); cần tạm dừng thì
 * đặt cờ cho task program đọc bằng page_session_program_take_pause() thay vì ghi 0x1001.
 *
 * Thẻ (Accès badgeuse, trang 74766): từ CM8 ra 2 dây thẻ (chân IN1 / A3 và GND); 2 dây chập nhau
 * = có thẻ, CM8 ghi thanh ghi 18 = 1;
 * task motor đọc thanh ghi này khoảng mỗi 100 ms. Không có thẻ -> không cho bắt đầu. Mất thẻ giữa buổi tập:
 *   Timer badgeuse -> dừng ngay + báo lỗi;
 *   Timer sismo    -> chạy tiếp hết chu trình; tạm dừng / hết giờ thì không cho bắt đầu lại khi chưa có thẻ.
 * Mất kết nối CM8 thì coi như không có thẻ.
 *
 * Mode / ratio gửi CM8 (thanh ghi 8 / 9) lấy từ cài đặt trang 200816. Vào trang MANUAL (0010) khi
 * chưa chạy -> ghi thời gian buổi tập 5 / 90 phút (cài đặt 200816) vào 0x1003.
 * Trang Durée Exercices (0002): mỗi lần vào trang ghi 0x1020 = 1 phút; nút - / + (reportToHost) báo số
 * phút mới; vào trang buổi tập Durée (0011) khi chưa chạy -> ghi số phút * 60 vào 0x1003.
 * Hiện thời gian dạng MM:SS tới 99:59: widget Timer (đếm 0x1003) để ẩn, ESP32 ghi phút * 100 + giây
 * vào 0x1048 cho 1 Graphics Number Display (dấu thập phân là icon dấu ":") mỗi khi 0x1003 đổi.
 *
 * Cường độ / Xung: bình thường nút +/- và đồng hồ cùng dùng 0x1000, màn hình tự vẽ (nhấn giữ mượt),
 * ESP32 chỉ ĐỌC. Đang tập + bật Xung: ESP32 ghi mức dao động tam giác ±PULSE_AMPLITUDE (mỗi
 * PULSE_STEP_MS đổi 1 mức) vào 0x1000; lần bấm +/- lúc đó chỉ lấy chiều bấm để đổi cường độ gốc;
 * tắt Xung thì ghi trả cường độ gốc và chờ màn hình nhận (SCREEN_SYNC_MS).
 * Cường độ 1..32 -> tốc độ CM8 0..32 làm tròn xuống số CHẴN (CM8 chỉ đứng yên đúng ở số chẵn).
 *
 * Thông báo trên trang buổi tập (Icon 0x1031, dải dưới hàng nút, theo ngôn ngữ): bị chặn vì CM8 mất kết nối /
 * CM8 báo lỗi 2 -> tạm dừng và báo task setting đưa về trang khởi động 0000 (page_session_take_cm8_lost, giống
 * lúc mới bật: ERR.02, chờ CM8); CM8 báo lỗi 3 / 4 -> ERR.03 / 04 tại trang, tới khi CM8 hết lỗi; MODE WIFI chưa
 * có mạng -> ERR.01 tới khi có mạng; Accès badgeuse chưa có thẻ / mất thẻ -> "Veuillez badger svp"
 * BADGE_MESSAGE_MS. Rời trang thì xóa thông báo. Thông báo trang khởi động 0000: task setting.
 *
 * Thông số: giới hạn dòng 3,70 A,
 * PREPARE_TIMEOUT_MS chờ CM8 về 0 trước lần chạy đầu, PAUSE_SCREEN_ON_ERROR: CM8 lỗi / mất kết nối
 * khi đang tập -> tạm dừng màn hình.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "freertos/queue.h"
#include "app_config.h"
#include "hmi.h"
#include "motor_modbus.h"
#include "logger.h"
#include "device_settings.h"
#include "network.h"
#include "page_session.h"

#define CURRENT_LIMIT_X100        370
#define SPEED_MAX                 32
#define SPEED_EVEN_ONLY           1
#define PREPARE_TIMEOUT_MS        3000
#define PAUSE_SCREEN_ON_ERROR     1
#define PULSE_AMPLITUDE           2
#define PULSE_STEP_MS             300
#define SCREEN_SYNC_MS            600
#define SESSION_PERIOD_MS         50
#define TIME_SETTLE_MS            300
#define DURATION_MIN_MIN          1
#define DURATION_MIN_MAX          10
#define BADGE_MESSAGE_MS          3000

static QueueHandle_t touch_queue;

static const char *MODE_NAMES[] = {"CHI QUAT", "CHUAN BI", "CHAY", "DUNG"};
const char *session_motor_name(session_motor_t c) { return (c <= SESSION_STOP) ? MODE_NAMES[c] : "?"; }

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static session_status_t shared_status = { .mode = SESSION_FAN_ONLY };

static session_motor_t mode = SESSION_FAN_ONLY;
static int64_t mode_changed_ms = 0;

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

static bool prog_run_req = false;
static int  prog_level_req = 0;
static bool prog_pause_req = false;

void page_session_program(bool run, int level)
{
    portENTER_CRITICAL(&lock);
    prog_run_req = run;
    prog_level_req = level;
    portEXIT_CRITICAL(&lock);
}

bool page_session_program_take_pause(void)
{
    portENTER_CRITICAL(&lock);
    bool r = prog_pause_req;
    prog_pause_req = false;
    portEXIT_CRITICAL(&lock);
    return r;
}

static void request_pause(bool on_program)
{
    if (on_program) {
        portENTER_CRITICAL(&lock);
        prog_pause_req = true;
        prog_run_req = false;
        portEXIT_CRITICAL(&lock);
    } else {
        hmi_write(ADDR_RUN, 0);
    }
}

static int64_t last_time_write_ms = 0;

static void show_time(int seconds)
{
    if (seconds < 0) seconds = 0;
    int minutes = seconds / 60;
    if (minutes > 99) minutes = 99;
    hmi_write(ADDR_TIME_DISPLAY, (uint16_t)(minutes * 100 + seconds % 60));
}

session_status_t page_session_get_status(void)
{
    portENTER_CRITICAL(&lock);
    session_status_t t = shared_status;
    portEXIT_CRITICAL(&lock);
    return t;
}

static volatile int duration_minutes = 1;

bool page_session_on_event(const hmi_event_t *e)
{
    if (e->addr == ADDR_DURATION_MIN) {
        if (e->value >= DURATION_MIN_MIN && e->value <= DURATION_MIN_MAX) duration_minutes = e->value;
        return true;
    }
    if (e->addr != ADDR_INTENSITY) return false;
    if (e->source == EV_SRC_REPORT && touch_queue) xQueueSend(touch_queue, &e->value, 0);
    return true;
}

static uint16_t level_to_speed(int level)
{
    if (level <= 1) return 0;
    int v = (level - 1) * SPEED_MAX / 31;
    if (v > SPEED_MAX) v = SPEED_MAX;
#if SPEED_EVEN_ONLY
    return (uint16_t)(v & ~1);
#else
    return (uint16_t)v;
#endif
}

static void change_mode(session_motor_t next)
{
    if (next == mode) return;
    mode = next;
    mode_changed_ms = ms_now();
    log_send("   [CM8] -> %s", MODE_NAMES[next]);
}

static int64_t pulse_start_ms = 0;
static int pulse_offset(void)
{
    const int steps = 4 * PULSE_AMPLITUDE;
    int k = (int)(((ms_now() - pulse_start_ms) / PULSE_STEP_MS) % steps);
    if (k <= PULSE_AMPLITUDE)     return k;
    if (k <= 3 * PULSE_AMPLITUDE) return 2 * PULSE_AMPLITUDE - k;
    return k - 4 * PULSE_AMPLITUDE;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int     base_intensity = 0;
static bool    pulse_active = false;
static int     pulse_written = -1;
static int64_t screen_sync_until = 0;

static void update_intensity(const hmi_data_t *d)
{
    uint16_t v;
    while (xQueueReceive(touch_queue, &v, 0) == pdTRUE) {
        if (pulse_active) {
            int delta = (v > pulse_written) ? 1 : ((int)v < pulse_written ? -1 : 0);
            base_intensity = clamp(base_intensity + delta, 0, 31);
        }
    }
    if (!pulse_active) {
        if (screen_sync_until && ((int)d->vars[VAR_INTENSITY] == base_intensity || ms_now() >= screen_sync_until))
            screen_sync_until = 0;
        if (!screen_sync_until) base_intensity = clamp(d->vars[VAR_INTENSITY], 0, 31);
    }
}

static bool    net_alert = false, cm8_alert = false;
static volatile bool cm8_lost = false;

bool page_session_take_cm8_lost(void)
{
    bool r = cm8_lost;
    cm8_lost = false;
    return r;
}
static int64_t badge_until = 0;

static int cm8_message(const motor_status_t *t)
{
    if (!t->connected || t->error == 2) return MSG_ERR02;
    if (t->error == 3) return MSG_ERR03;
    if (t->error == 4) return MSG_ERR04;
    return MSG_NONE;
}

static void update_message(const hmi_data_t *d, bool session_page, const motor_status_t *t)
{
    static int page_prev = -1;
    static uint16_t written = 0xFFFF;
    if (d->page != page_prev) {
        net_alert = cm8_alert = false;
        badge_until = 0;
        bool leaving_session = page_prev >= 0 && page_prev != PAGE_STARTUP;
        page_prev = d->page;
        if (!session_page) {
            if (leaving_session && d->page != PAGE_STARTUP) hmi_write(ADDR_MESSAGE, MESSAGE_HIDDEN);
            written = 0xFFFF;
            return;
        }
        written = 0xFFFF;
    }
    if (!session_page || !hmi_is_linked(d)) return;
    int msg = MSG_NONE;
    if (cm8_alert)                    msg = cm8_message(t);
    if (msg == MSG_NONE && net_alert) msg = MSG_ERR01;
    if (msg == MSG_NONE && ms_now() < badge_until) msg = MSG_BADGE;
    uint16_t v = msg == MSG_NONE ? MESSAGE_HIDDEN : LANG_ICON(msg, device_get_language());
    if (v != written) {
        hmi_write(ADDR_MESSAGE, v);
        written = v;
    }
}

static void task_session(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    int64_t t0 = ms_now();
    motor_status_t t = motor_get_status();
    while (t.ok_count == 0 && ms_now() - t0 < 1500) {
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_task_wdt_reset();
        t = motor_get_status();
    }
    if (t.ok_count > 0 && t.mode != 0) {
        mode = SESSION_STOP;
        log_send("   [CM8] CM8 da co Mode=%u tu truoc (ESP32 vua khoi dong lai) -> DUNG cho an toan", t.mode);
    } else {
        mode = SESSION_FAN_ONLY;
        log_send("   [CM8] -> CHI QUAT (bat len la quat quay)");
    }
    mode_changed_ms = ms_now();

    esp_reset_reason_t reset_reason = esp_reset_reason();
    bool pause_required = (reset_reason == ESP_RST_TASK_WDT || reset_reason == ESP_RST_INT_WDT ||
                             reset_reason == ESP_RST_WDT || reset_reason == ESP_RST_PANIC);
    const char *pause_reason = "ESP32 vua khoi dong lai do loi";
    int64_t last_pause_write_ms = 0;
    bool link_prev = false, running_prev = false;

    TickType_t wake = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SESSION_PERIOD_MS));
        esp_task_wdt_reset();

        hmi_data_t d = hmi_get();
        t = motor_get_status();
        bool link_ok = hmi_is_linked(&d);
        bool on_program = d.page == PAGE_PROGRAM_X || d.page == PAGE_PROGRAM;
        portENTER_CRITICAL(&lock);
        bool prog_run = prog_run_req;
        int  prog_level = prog_level_req;
        portEXIT_CRITICAL(&lock);
        bool run_req = on_program ? prog_run : d.vars[VAR_RUN] == 1;

        if (link_prev && !link_ok && running_prev) {
            pause_required = true;
            pause_reason = "Vua mat lien lac man hinh giua buoi tap";
            log_send("   [AN TOAN] MAT LIEN LAC man hinh -> dung dong co");
        }
        link_prev = link_ok;

        if (pause_required && link_ok) {
            if (!run_req) {
                pause_required = false;
                log_send("   [AN TOAN] Man hinh da tam dung, bam > de tap tiep");
            } else if (ms_now() - last_pause_write_ms >= 500) {
                last_pause_write_ms = ms_now();
                request_pause(on_program);
                log_send("   [AN TOAN] %s -> tam dung buoi tap tren man hinh", pause_reason);
            }
        }

        bool on_session = (d.page < 0 || hmi_is_session_page(d.page) || on_program);
        bool net_ok = device_get_control_mode() == CONTROL_MODE_LOCAL || net_get_state() == NET_ONLINE;
        static int64_t last_net_block_ms = 0;
        if (link_ok && on_session && run_req && !net_ok && ms_now() - last_net_block_ms >= 500) {
            last_net_block_ms = ms_now();
            request_pause(on_program);
            net_alert = true;
            log_send("   [CHE DO] MODE WIFI nhung chua co mang (%s) -> khong cho dong co chay, tam dung",
                     net_state_name(net_get_state()));
        }

        bool card_required = device_get_access_mode() == ACCESS_CARD;
        bool card_present  = t.connected && t.card == 1;
        static bool card_grace = false;
        static int64_t last_card_block_ms = 0;
        if (!card_required || card_present) card_grace = false;
        if (card_required && running_prev && !card_present && !card_grace) {
            if (device_get_card_timer() == CARD_TIMER_SISMO) {
                card_grace = true;
                log_send("   [THE] Mat the giua buoi tap (Timer Sismo) -> chay het chu trinh, khong cho bat dau chu trinh moi");
            } else {
                log_send("   [THE] LOI: mat the giua buoi tap (Timer badgeuse) -> DUNG dong co");
            }
        }
        bool card_ok = !card_required || card_present || (card_grace && running_prev);
        if (link_ok && on_session && run_req && !card_ok && ms_now() - last_card_block_ms >= 500) {
            last_card_block_ms = ms_now();
            request_pause(on_program);
            badge_until = ms_now() + BADGE_MESSAGE_MS;
            if (!running_prev) log_send("   [THE] Acces badgeuse: chua co the (o 18 = %u) -> khong cho bat dau", t.card);
        }

        static int prev_page = -1;
        static int shown_seconds = -1;
        if (d.page == PAGE_MANUEL && prev_page != PAGE_MANUEL && d.vars[VAR_RUN] == 0) {
            int minutes = device_get_manual_minutes();
            hmi_write(ADDR_SECONDS, (uint16_t)(minutes * 60));
            show_time(minutes * 60);
            shown_seconds = minutes * 60;
            last_time_write_ms = ms_now();
            log_send("   [MANUAL] Thoi gian buoi tap %d phut", minutes);
        } else if (link_ok && on_session && !on_program) {
            int s = d.vars[VAR_SECONDS];
            if ((s != shown_seconds && ms_now() - last_time_write_ms >= TIME_SETTLE_MS) || d.page != prev_page) {
                show_time(s);
                shown_seconds = s;
            }
        } else {
            shown_seconds = -1;
        }
        if (d.page == PAGE_DUREE && prev_page != PAGE_DUREE) {
            duration_minutes = DURATION_MIN_MIN;
            hmi_write(ADDR_DURATION_MIN, DURATION_MIN_MIN);
        }
        if (d.page == PAGE_DUREE_SESSION && prev_page != PAGE_DUREE_SESSION && d.vars[VAR_RUN] == 0) {
            int minutes = duration_minutes;
            hmi_write(ADDR_SECONDS, (uint16_t)(minutes * 60));
            last_time_write_ms = ms_now();
            log_send("   [DUREE] Thoi gian buoi tap %d phut", minutes);
        }
        prev_page = d.page;

        bool run_allowed = link_ok && !pause_required && on_session && net_ok && card_ok && run_req;

        update_intensity(&d);
        bool want_pulse = run_allowed && !on_program && d.vars[VAR_PULSE] == 1;
        if (want_pulse && !pulse_active) {
            pulse_active = true;
            pulse_written = base_intensity;
            pulse_start_ms = ms_now();
        } else if (!want_pulse && pulse_active) {
            pulse_active = false;
            hmi_write(ADDR_INTENSITY, (uint16_t)base_intensity);
            screen_sync_until = ms_now() + SCREEN_SYNC_MS;
        }
        int target_level = !run_allowed ? 0 : on_program ? clamp(prog_level, 0, 32) :
                           clamp(base_intensity + 1 + (pulse_active ? pulse_offset() : 0), 1, 32);
        if (pulse_active && target_level - 1 != pulse_written) {
            pulse_written = target_level - 1;
            hmi_write(ADDR_INTENSITY, (uint16_t)pulse_written);
        }
        running_prev = run_allowed;
        bool cm8_ok = t.connected && t.error == 0;
        uint16_t run_mode  = (uint16_t)device_get_motor_mode();
        uint16_t run_ratio = (uint16_t)device_get_motor_ratio();

        switch (mode) {
        case SESSION_FAN_ONLY:
            if (run_allowed && cm8_ok) change_mode(SESSION_PREPARE);
            break;
        case SESSION_PREPARE:
            if (!run_allowed) change_mode(SESSION_STOP);
            else if ((t.cmd == MOTOR_CMD_STOP && t.mode == run_mode && t.run_state == 0) ||
                     ms_now() - mode_changed_ms > PREPARE_TIMEOUT_MS)
                change_mode(SESSION_RUN);
            break;
        case SESSION_RUN:
            if (!run_allowed) change_mode(SESSION_STOP);
            break;
        case SESSION_STOP:
            if (run_allowed && cm8_ok) change_mode(SESSION_RUN);
            break;
        }

        uint16_t speed = 0;
        switch (mode) {
        case SESSION_FAN_ONLY:
            motor_set(MOTOR_CMD_RUN, 0, CURRENT_LIMIT_X100, 0, 0);
            break;
        case SESSION_PREPARE:
        case SESSION_STOP:
            motor_set(MOTOR_CMD_STOP, 0, CURRENT_LIMIT_X100, run_mode, run_ratio);
            break;
        case SESSION_RUN:
            speed = level_to_speed(target_level);
            motor_set(MOTOR_CMD_RUN, speed, CURRENT_LIMIT_X100, run_mode, run_ratio);
            break;
        }

        portENTER_CRITICAL(&lock);
        shared_status.mode = mode;
        shared_status.speed_sent = speed;
        shared_status.target_level = target_level;
        shared_status.run_allowed = run_allowed;
        shared_status.intensity = base_intensity;
        portEXIT_CRITICAL(&lock);

        static int64_t last_error_log_ms = 0;
        if (run_allowed && !cm8_ok && ms_now() - last_error_log_ms >= 1000) {
            last_error_log_ms = ms_now();
            if (!t.connected) log_send("   [CM8] KHONG PHAN HOI - kiem tra nguon 220V, day, module SP3232");
            else            log_send("   [CM8] LOI %u (%s) - tat/bat lai nguon CM8", t.error, motor_error_name(t.error));
            if (cm8_message(&t) == MSG_ERR02) cm8_lost = true;
            else cm8_alert = true;
#if PAUSE_SCREEN_ON_ERROR
            request_pause(on_program);
            log_send("   [CM8] -> tam dung buoi tap tren man hinh");
#endif
        }

        if (net_ok) net_alert = false;
        if (cm8_ok) cm8_alert = false;
        update_message(&d, on_session && d.page >= 0, &t);

        static int connected_prev = -1, error_prev = -1;
        if ((int)t.connected != connected_prev || (t.connected && (int)t.error != error_prev)) {
            if (t.connected) log_send("   [CM8] Ket noi OK | Loi %u (%s)", t.error, motor_error_name(t.error));
            else if (connected_prev != -1) log_send("   [CM8] MAT KET NOI");
            connected_prev = t.connected;
            error_prev = t.error;
        }
    }
}

static void mmss(uint16_t seconds, char *out, size_t n)
{
    snprintf(out, n, "%02u:%02u", seconds / 60, seconds % 60);
}

static void program_name(const hmi_data_t *d, char *out, size_t n)
{
    switch (d->page) {
    case 10: snprintf(out, n, "Manuel"); return;
    case 11: snprintf(out, n, "Durée Exercices"); return;
    default: out[0] = '\0';
    }
}

static void describe_motor(char *out, size_t n, uint16_t speed)
{
    motor_status_t t = motor_get_status();
    if (!t.connected) { snprintf(out, n, "CM8 KHONG PHAN HOI"); return; }
    snprintf(out, n, "CM8 toc do %2u | DAC %4u mV | I %.2f A | Loi %u (%s)",
             speed, t.dac_mv, t.current_x100 / 100.0, t.error, motor_error_name(t.error));
}

static void report_progress(void)
{
    static int  prev_page = -1, prev_running = -1, prev_seconds = -1;
    static bool end_reported = false;
    hmi_data_t d = hmi_get();
    if (!d.has_vars) return;
    bool page_known = (d.page >= 0);
    char t1[64], t2[16], t3[96];

    if (page_known && d.page != prev_page) {
        if (hmi_is_session_page(d.page)) {
            program_name(&d, t1, sizeof(t1));
            mmss(d.vars[VAR_SECONDS], t2, sizeof(t2));
            printf("  Chương trình: %s\n", t1);
            printf("  Thời gian: %s  -> bấm ▶ trên màn hình để bắt đầu\n", t2);
        }
        prev_page = d.page; prev_running = d.vars[VAR_RUN]; prev_seconds = d.vars[VAR_SECONDS];
        end_reported = false;
        return;
    }
    if (page_known && !hmi_is_session_page(d.page)) return;

    int running = d.vars[VAR_RUN];
    int seconds = d.vars[VAR_SECONDS];

    if (running != prev_running) {
        mmss(seconds, t2, sizeof(t2));
        if (running == 1) {
            program_name(&d, t1, sizeof(t1));
            printf(">> BẮT ĐẦU - %s - còn %s\n", t1, t2);
            end_reported = false;
        } else if (seconds == 0) {
            if (!end_reported) { printf(">> KẾT THÚC buổi tập\n"); end_reported = true; }
        } else if (prev_running == 1) {
            printf(">> TẠM DỪNG tại %s\n", t2);
        }
        prev_running = running;
    }

    if (running == 1 && seconds != prev_seconds) {
        session_status_t k = page_session_get_status();
        mmss(seconds, t2, sizeof(t2));
        describe_motor(t3, sizeof(t3), k.speed_sent);
        if (d.vars[VAR_PULSE]) {
            int lo = k.intensity + 1 - PULSE_AMPLITUDE, hi = k.intensity + 1 + PULSE_AMPLITUDE;
            if (lo < 1) lo = 1;
            if (hi > 32) hi = 32;
            printf("   Còn lại %s | Cường độ %2d | Xung bật (%d..%d, đang %2d) | %s\n",
                   t2, k.intensity + 1, lo, hi, k.target_level, t3);
        } else {
            printf("   Còn lại %s | Cường độ %2d | Xung tắt | %s\n", t2, k.intensity + 1, t3);
        }
    }
    if (running == 1 && seconds == 0 && !end_reported) {
        printf(">> KẾT THÚC buổi tập\n");
        end_reported = true;
    }
    prev_seconds = seconds;
}

void page_session_start(void)
{
    touch_queue = xQueueCreate(16, sizeof(uint16_t));
    logger_add_hook(report_progress);
    xTaskCreatePinnedToCore(task_session, "session", STACK_SESSION, NULL,
                            PRIO_SESSION, NULL, APP_CORE);
}
