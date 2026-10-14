/*
 * page_session.c - Task "session": điều khiển động cơ CM8 theo màn hình + an toàn (xem page_session.h)
 *
 * Khởi động an toàn: chỉ ĐỌC CM8 trước. CM8 đã có Mode (ESP32 vừa reset giữa chừng) -> DỪNG,
 * không dùng "chỉ quạt" (CM8 nhớ Mode, lệnh 1 sẽ làm động cơ quay chậm nhất). ESP32 vừa khởi động
 * lại vì lỗi (watchdog / crash) hoặc mất liên lạc màn hình giữa buổi tập -> bắt màn hình tạm dừng,
 * người dùng phải bấm ▶ lại.
 *
 * Cho chạy khi: còn liên lạc màn hình, đang ở trang buổi tập, 0x1001 = 1, không bị bắt dừng, và
 * (MODE LOCAL hoặc ONLINE). MODE WIFI mà chưa có mạng -> ghi 0x1001 = 0 (tạm dừng).
 *
 * Cường độ / Xung: bình thường nút +/- và đồng hồ cùng dùng 0x1000, màn hình tự vẽ (nhấn giữ mượt),
 * ESP32 chỉ ĐỌC. Đang tập + bật Xung: ESP32 ghi mức dao động tam giác ±PULSE_AMPLITUDE (mỗi
 * PULSE_STEP_MS đổi 1 mức) vào 0x1000; lần bấm +/- lúc đó chỉ lấy chiều bấm để đổi cường độ gốc;
 * tắt Xung thì ghi trả cường độ gốc và chờ màn hình nhận (SCREEN_SYNC_MS).
 * Cường độ 1..32 -> tốc độ CM8 0..32 làm tròn xuống số CHẴN (CM8 chỉ đứng yên đúng ở số chẵn).
 *
 * Thông số: RUN_MODE 3 / RUN_RATIO 9 (tạm, chưa có giá trị chính thức), giới hạn dòng 3,70 A,
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

#define RUN_MODE                  3
#define RUN_RATIO                 9
#define CURRENT_LIMIT_X100        370
#define SPEED_MAX                 32
#define SPEED_EVEN_ONLY           1
#define PREPARE_TIMEOUT_MS        3000
#define PAUSE_SCREEN_ON_ERROR     1
#define PULSE_AMPLITUDE           2
#define PULSE_STEP_MS             300
#define SCREEN_SYNC_MS            600
#define SESSION_PERIOD_MS         50

static QueueHandle_t touch_queue;

static const char *MODE_NAMES[] = {"CHI QUAT", "CHUAN BI", "CHAY", "DUNG"};
const char *session_motor_name(session_motor_t c) { return (c <= SESSION_STOP) ? MODE_NAMES[c] : "?"; }

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static session_status_t shared_status = { .mode = SESSION_FAN_ONLY };

static session_motor_t mode = SESSION_FAN_ONLY;
static int64_t mode_changed_ms = 0;

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

session_status_t page_session_get_status(void)
{
    portENTER_CRITICAL(&lock);
    session_status_t t = shared_status;
    portEXIT_CRITICAL(&lock);
    return t;
}

bool page_session_on_event(const hmi_event_t *e)
{
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

        if (link_prev && !link_ok && running_prev) {
            pause_required = true;
            pause_reason = "Vua mat lien lac man hinh giua buoi tap";
            log_send("   [AN TOAN] MAT LIEN LAC man hinh -> dung dong co");
        }
        link_prev = link_ok;

        if (pause_required && link_ok) {
            if (d.vars[VAR_RUN] == 0) {
                pause_required = false;
                log_send("   [AN TOAN] Man hinh da tam dung, bam > de tap tiep");
            } else if (ms_now() - last_pause_write_ms >= 500) {
                last_pause_write_ms = ms_now();
                hmi_write(ADDR_RUN, 0);
                log_send("   [AN TOAN] %s -> tam dung buoi tap tren man hinh", pause_reason);
            }
        }

        bool on_session = (d.page < 0 || hmi_is_session_page(d.page));
        bool net_ok = device_get_control_mode() == CONTROL_MODE_LOCAL || net_get_state() == NET_ONLINE;
        static int64_t last_net_block_ms = 0;
        if (link_ok && on_session && d.vars[VAR_RUN] == 1 && !net_ok && ms_now() - last_net_block_ms >= 500) {
            last_net_block_ms = ms_now();
            hmi_write(ADDR_RUN, 0);
            log_send("   [CHE DO] MODE WIFI nhung chua co mang (%s) -> khong cho dong co chay, tam dung",
                     net_state_name(net_get_state()));
        }

        bool run_allowed = link_ok && !pause_required && on_session && net_ok && d.vars[VAR_RUN] == 1;

        update_intensity(&d);
        bool want_pulse = run_allowed && d.vars[VAR_PULSE] == 1;
        if (want_pulse && !pulse_active) {
            pulse_active = true;
            pulse_written = base_intensity;
            pulse_start_ms = ms_now();
        } else if (!want_pulse && pulse_active) {
            pulse_active = false;
            hmi_write(ADDR_INTENSITY, (uint16_t)base_intensity);
            screen_sync_until = ms_now() + SCREEN_SYNC_MS;
        }
        int target_level = run_allowed ? clamp(base_intensity + 1 + (pulse_active ? pulse_offset() : 0), 1, 32) : 0;
        if (pulse_active && target_level - 1 != pulse_written) {
            pulse_written = target_level - 1;
            hmi_write(ADDR_INTENSITY, (uint16_t)pulse_written);
        }
        running_prev = run_allowed;
        bool cm8_ok = t.connected && t.error == 0;

        switch (mode) {
        case SESSION_FAN_ONLY:
            if (run_allowed && cm8_ok) change_mode(SESSION_PREPARE);
            break;
        case SESSION_PREPARE:
            if (!run_allowed) change_mode(SESSION_STOP);
            else if ((t.cmd == MOTOR_CMD_STOP && t.mode == RUN_MODE && t.run_state == 0) ||
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
            motor_set(MOTOR_CMD_STOP, 0, CURRENT_LIMIT_X100, RUN_MODE, RUN_RATIO);
            break;
        case SESSION_RUN:
            speed = level_to_speed(target_level);
            motor_set(MOTOR_CMD_RUN, speed, CURRENT_LIMIT_X100, RUN_MODE, RUN_RATIO);
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
#if PAUSE_SCREEN_ON_ERROR
            hmi_write(ADDR_RUN, 0);
            log_send("   [CM8] -> tam dung buoi tap tren man hinh");
#endif
        }

        static int connected_prev = -1, error_prev = -1;
        if ((int)t.connected != connected_prev || (t.connected && (int)t.error != error_prev)) {
            if (t.connected) log_send("   [CM8] Ket noi OK | Loi %u (%s)", t.error, motor_error_name(t.error));
            else if (connected_prev != -1) log_send("   [CM8] MAT KET NOI");
            connected_prev = t.connected;
            error_prev = t.error;
        }
    }
}

static const char *PROGRAM_TITLES[22] = {
    "Tonification - Bras", "Tonification - Poitrine Bras", "Tonification - Sangle abdominale",
    "Tonification - Cuisses Fessiers", "Tonification - Complet",
    "Remodelage - Poitrine Bras", "Remodelage - Sangle abdominale", "Remodelage - Cuisses Fessiers 1",
    "Remodelage - Cuisses Fessiers 2", "Remodelage - Complet",
    "Anti-cellulite - Cuisses Fessiers", "Gainage - Complet 1", "Gainage - Complet 2",
    "Relaxation Drainage - Drainage", "Relaxation Drainage - Relax 1", "Relaxation Drainage - Relax 2",
    "Proprioception - 1", "Proprioception - 2", "Proprioception - 3",
    "Proprioception - 4", "Proprioception - 5", "Proprioception - 6"
};
static const char *LEVEL_NAMES[4] = {"Débutant", "Initié", "Confirmé", ""};

static void mmss(uint16_t seconds, char *out, size_t n)
{
    snprintf(out, n, "%02u:%02u", seconds / 60, seconds % 60);
}

static void program_name(const hmi_data_t *d, char *out, size_t n)
{
    switch (d->page) {
    case 6: {
        uint16_t id = d->vars[VAR_PROGRAM], lvl = d->vars[VAR_LEVEL];
        const char *g = (id < 22) ? PROGRAM_TITLES[id] : "Programmes";
        if (lvl < 3) snprintf(out, n, "%s - %s", g, LEVEL_NAMES[lvl]);
        else         snprintf(out, n, "%s", g);
        return;
    }
    case 8:  snprintf(out, n, "Perso - %u", d->vars[VAR_PERSO]); return;
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
