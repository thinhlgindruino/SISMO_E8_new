/*
 * control.c - Task điều khiển (xem control.h)
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "config.h"
#include "hmi.h"
#include "cm8.h"
#include "logger.h"
#include "control.h"

static const char *MODE_NAMES[] = {"CHI QUAT", "CHUAN BI", "CHAY", "DUNG"};
const char *control_mode_name(control_mode_t c) { return (c <= CM8_STOP) ? MODE_NAMES[c] : "?"; }

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static control_status_t shared_status = { .mode = CM8_FAN_ONLY };

static control_mode_t mode = CM8_FAN_ONLY;
static int64_t mode_changed_ms = 0;

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

control_status_t control_get_status(void)
{
    portENTER_CRITICAL(&lock);
    control_status_t t = shared_status;
    portEXIT_CRITICAL(&lock);
    return t;
}

static bool is_session_page(int t) { return t == 6 || t == 8 || t == 10 || t == 11; }

// Cường độ 1..32 -> tốc độ CM8 0..32, làm tròn xuống số CHẴN
static uint16_t level_to_speed(int level)
{
    if (level <= 1) return 0;
    int v = (level - 1) * SPEED_MAX / 31;
    if (v > SPEED_MAX) v = SPEED_MAX;
#if SPEED_EVEN_ONLY
    return (uint16_t)(v & ~1);     // CM8 đứng yên đúng mức, nhưng chỉ có 17 mức 0,2,4..32
#else
    return (uint16_t)v;            // đủ 33 mức; số lẻ thì CM8 dao động ±1 mức mỗi 50 ms quanh giá trị đó
#endif
}

static void change_mode(control_mode_t next)
{
    if (next == mode) return;
    mode = next;
    mode_changed_ms = ms_now();
    log_send("   [CM8] -> %s", MODE_NAMES[next]);
}

// Độ lệch Xung tính từ lúc bật Xung: tam giác 0,+1,+2,+1,0,-1,-2,-1 (biên độ 2)
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

// ---- Cường độ / Xung ----
// Bình thường: nút +/- và đồng hồ cùng dùng 0x1000, màn hình tự vẽ -> nhấn giữ mượt,
// ESP32 chỉ ĐỌC 0x1000. Khi đang tập + bật Xung: ESP32 ghi mức dao động vào 0x1000 (đồng hồ
// dao động theo), còn cường độ người dùng chọn giữ riêng trong base_intensity; tắt Xung thì ghi trả lại.
static int     base_intensity = 0;            // cường độ người dùng chọn 0..31
static bool    pulse_active = false;     // ESP32 đang ghi dao động vào 0x1000
static int     pulse_written = -1;        // giá trị dao động ghi gần nhất
static int64_t screen_sync_until = 0;  // đang chờ màn hình nhận giá trị trả lại

static void update_intensity(const hmi_data_t *d)
{
    uint16_t v;
    while (hmi_get_intensity_touch(&v)) {
        if (pulse_active) {
            // Màn hình cộng/trừ trên giá trị dao động đang hiện -> chỉ lấy chiều bấm
            int delta = (v > pulse_written) ? 1 : ((int)v < pulse_written ? -1 : 0);
            base_intensity = clamp(base_intensity + delta, 0, 31);
        }
    }
    if (!pulse_active) {
        if (screen_sync_until && ((int)d->vars[0] == base_intensity || ms_now() >= screen_sync_until))
            screen_sync_until = 0;
        if (!screen_sync_until) base_intensity = clamp(d->vars[0], 0, 31);
    }
}

static void task_control(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    // ---- 1. Khởi động an toàn ----
    // Chỉ ĐỌC CM8 trước (chưa gửi lệnh) để biết CM8 vừa bật hay đã chạy từ trước.
    // Nếu CM8 đã có Mode (ESP32 vừa bị reset giữa chừng) thì KHÔNG dùng chế độ "chỉ quạt",
    // vì CM8 nhớ Mode -> lệnh 1 sẽ làm động cơ quay ở tốc độ thấp nhất.
    int64_t t0 = ms_now();
    cm8_status_t t = cm8_get_status();
    while (t.ok_count == 0 && ms_now() - t0 < 1500) {
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_task_wdt_reset();
        t = cm8_get_status();
    }
    if (t.ok_count > 0 && t.mode != 0) {
        mode = CM8_STOP;
        log_send("   [CM8] CM8 da co Mode=%u tu truoc (ESP32 vua khoi dong lai) -> DUNG cho an toan", t.mode);
    } else {
        mode = CM8_FAN_ONLY;
        log_send("   [CM8] -> CHI QUAT (bat len la quat quay)");
    }
    mode_changed_ms = ms_now();

    // Nếu ESP32 vừa khởi động lại vì lỗi (watchdog, crash) thì bắt màn hình tạm dừng buổi tập,
    // người dùng phải bấm ▶ lại -> động cơ không tự chạy lại khi chưa ai để ý.
    esp_reset_reason_t reset_reason = esp_reset_reason();
    bool pause_required = (reset_reason == ESP_RST_TASK_WDT || reset_reason == ESP_RST_INT_WDT ||
                             reset_reason == ESP_RST_WDT || reset_reason == ESP_RST_PANIC);
    const char *pause_reason = "ESP32 vua khoi dong lai do loi";
    int64_t last_pause_write_ms = 0;
    bool link_prev = false, running_prev = false;

    // ---- 2. Vòng điều khiển, chu kỳ đều 50 ms ----
    TickType_t wake = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
        esp_task_wdt_reset();

        hmi_data_t d = hmi_get();
        t = cm8_get_status();
        bool link_ok = hmi_is_linked(&d);

        // Mất liên lạc màn hình giữa buổi tập: khi có lại liên lạc cũng bắt tạm dừng,
        // không để động cơ tự chạy lại
        if (link_prev && !link_ok && running_prev) {
            pause_required = true;
            pause_reason = "Vua mat lien lac man hinh giua buoi tap";
            log_send("   [AN TOAN] MAT LIEN LAC man hinh -> dung dong co");
        }
        link_prev = link_ok;

        if (pause_required && link_ok) {
            if (d.vars[1] == 0) {
                pause_required = false;
                log_send("   [AN TOAN] Man hinh da tam dung, bam > de tap tiep");
            } else if (ms_now() - last_pause_write_ms >= 500) {
                last_pause_write_ms = ms_now();
                hmi_write(0x1001, 0);
                log_send("   [AN TOAN] %s -> tam dung buoi tap tren man hinh", pause_reason);
            }
        }

        bool run_allowed = link_ok && !pause_required &&
                        (d.page < 0 || is_session_page(d.page)) && d.vars[1] == 1;

        // ---- Cường độ + Xung ----
        update_intensity(&d);
        bool want_pulse = run_allowed && d.vars[5] == 1;
        if (want_pulse && !pulse_active) {
            pulse_active = true;
            pulse_written = base_intensity;             // màn hình đang hiện đúng base_intensity
            pulse_start_ms = ms_now();
        } else if (!want_pulse && pulse_active) {
            pulse_active = false;
            hmi_write(ADDR_INTENSITY, (uint16_t)base_intensity);          // trả lại cường độ đã chọn
            screen_sync_until = ms_now() + SCREEN_SYNC_MS;
        }
        int target_level = run_allowed ? clamp(base_intensity + 1 + (pulse_active ? pulse_offset() : 0), 1, 32) : 0;
        if (pulse_active && target_level - 1 != pulse_written) {
            pulse_written = target_level - 1;
            hmi_write(ADDR_INTENSITY, (uint16_t)pulse_written);       // đồng hồ dao động theo
        }
        running_prev = run_allowed;
        bool cm8_ok = t.connected && t.error == 0;

        // ---- Chuyển chế độ ----
        switch (mode) {
        case CM8_FAN_ONLY:
            if (run_allowed && cm8_ok) change_mode(CM8_PREPARE);
            break;
        case CM8_PREPARE:
            if (!run_allowed) change_mode(CM8_STOP);
            else if ((t.cmd == CM8_CMD_STOP && t.mode == RUN_MODE && t.run_state == 0) ||
                     ms_now() - mode_changed_ms > PREPARE_TIMEOUT_MS)
                change_mode(CM8_RUN);
            break;
        case CM8_RUN:
            if (!run_allowed) change_mode(CM8_STOP);
            break;
        case CM8_STOP:
            if (run_allowed && cm8_ok) change_mode(CM8_RUN);
            break;
        }

        // ---- Giá trị gửi CM8 (task cm8 tự gửi đi) ----
        uint16_t speed = 0;
        switch (mode) {
        case CM8_FAN_ONLY:
            cm8_set(CM8_CMD_RUN, 0, CURRENT_LIMIT_X100, 0, 0);
            break;
        case CM8_PREPARE:
        case CM8_STOP:
            cm8_set(CM8_CMD_STOP, 0, CURRENT_LIMIT_X100, RUN_MODE, RUN_RATIO);
            break;
        case CM8_RUN:
            speed = level_to_speed(target_level);
            cm8_set(CM8_CMD_RUN, speed, CURRENT_LIMIT_X100, RUN_MODE, RUN_RATIO);
            break;
        }

        portENTER_CRITICAL(&lock);
        shared_status.mode = mode;
        shared_status.speed_sent = speed;
        shared_status.target_level = target_level;
        shared_status.run_allowed = run_allowed;
        shared_status.intensity = base_intensity;
        portEXIT_CRITICAL(&lock);

        // ---- CM8 lỗi / mất kết nối khi đang tập -> màn hình tạm dừng ----
        static int64_t last_error_log_ms = 0;
        if (run_allowed && !cm8_ok && ms_now() - last_error_log_ms >= 1000) {
            last_error_log_ms = ms_now();
            if (!t.connected) log_send("   [CM8] KHONG PHAN HOI - kiem tra nguon 220V, day, module SP3232");
            else            log_send("   [CM8] LOI %u (%s) - tat/bat lai nguon CM8", t.error, cm8_error_name(t.error));
#if PAUSE_SCREEN_ON_ERROR
            hmi_write(0x1001, 0);
            log_send("   [CM8] -> tam dung buoi tap tren man hinh");
#endif
        }

        // ---- Báo khi kết nối / lỗi CM8 thay đổi ----
        static int connected_prev = -1, error_prev = -1;
        if ((int)t.connected != connected_prev || (t.connected && (int)t.error != error_prev)) {
            if (t.connected) log_send("   [CM8] Ket noi OK | Loi %u (%s)", t.error, cm8_error_name(t.error));
            else if (connected_prev != -1) log_send("   [CM8] MAT KET NOI");
            connected_prev = t.connected;
            error_prev = t.error;
        }
    }
}

void control_start(void)
{
    xTaskCreatePinnedToCore(task_control, "control", STACK_CONTROL, NULL,
                            PRIO_CONTROL, NULL, APP_CORE);
}
