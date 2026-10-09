/*
 * logger.c - Task in log (xem logger.h)
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "config.h"
#include "hmi.h"
#include "cm8.h"
#include "control.h"
#include "logger.h"

#define LOG_LINE_LEN   160
#define LOG_QUEUE_LEN   16

static QueueHandle_t log_queue;
static hmi_data_t d;              // bản sao dữ liệu màn hình, chỉ task nhật ký dùng

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

void log_send(const char *fmt, ...)
{
    if (!log_queue) return;
    char line[LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    xQueueSend(log_queue, line, 0);   // 0 = không chờ; đầy thì bỏ dòng này
}

// ---------------- Tên hiển thị ----------------
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

static int duree_page_minutes(int t)
{
    if (t == 2) return 1;
    if (t >= 12 && t <= 20) return t - 10;
    return 0;
}

static void page_name(int t, char *out, size_t n)
{
    int minutes = duree_page_minutes(t);
    if (minutes) { snprintf(out, n, "Durée Exercices - chọn %d phút", minutes); return; }
    const char *s;
    switch (t) {
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
    case 9:  s = "Mot de passe"; break;
    case 10: s = "Buổi tập Manuel"; break;
    case 11: s = "Buổi tập Durée"; break;
    case 26: s = "Mật khẩu cài đặt"; break;
    case 27: s = "Cài đặt (WiFi)"; break;
    default: snprintf(out, n, "Trang %d", t); return;
    }
    snprintf(out, n, "%s", s);
}

static bool is_session_page(int t) { return t == 6 || t == 8 || t == 10 || t == 11; }

static void mmss(uint16_t seconds, char *out, size_t n)
{
    snprintf(out, n, "%02u:%02u", seconds / 60, seconds % 60);
}

static void program_name(char *out, size_t n)
{
    switch (d.page) {
    case 6: {
        uint16_t id = d.vars[7], lvl = d.vars[9];
        const char *g = (id < 22) ? PROGRAM_TITLES[id] : "Programmes";
        if (lvl < 3) snprintf(out, n, "%s - %s", g, LEVEL_NAMES[lvl]);
        else        snprintf(out, n, "%s", g);
        return;
    }
    case 8:  snprintf(out, n, "Perso - %u", d.vars[6]); return;
    case 10: snprintf(out, n, "Manuel"); return;
    case 11: snprintf(out, n, "Durée Exercices"); return;
    default: out[0] = '\0';
    }
}


static void describe_cm8(char *out, size_t n, uint16_t speed)
{
    cm8_status_t t = cm8_get_status();
    if (!t.connected) { snprintf(out, n, "CM8 KHONG PHAN HOI"); return; }
    snprintf(out, n, "CM8 toc do %2u | DAC %4u mV | I %.2f A | Loi %u (%s)",
             speed, t.dac_mv, t.current_x100 / 100.0, t.error, cm8_error_name(t.error));
}

// ---------------- Tiến trình buổi tập (trước đây là cap_nhat_man_hinh) ----------------
static int  prev_page = -1, prev_running = -1, prev_seconds = -1;
static bool end_reported = false;

static void print_progress(void)
{
    if (!d.has_vars) return;
    bool page_known = (d.page >= 0);
    char t1[64], t2[16], t3[96];

    if (page_known && d.page != prev_page) {
        page_name(d.page, t1, sizeof(t1));
        printf("\n[Trang] %s\n", t1);
        if (is_session_page(d.page)) {
            program_name(t1, sizeof(t1));
            mmss(d.vars[3], t2, sizeof(t2));
            printf("  Chương trình: %s\n", t1);
            printf("  Thời gian: %s  -> bấm ▶ trên màn hình để bắt đầu\n", t2);
        }
        prev_page = d.page; prev_running = d.vars[1]; prev_seconds = d.vars[3]; end_reported = false;
        return;
    }
    if (page_known && !is_session_page(d.page)) return;

    int running = d.vars[1];
    int seconds = d.vars[3];

    if (running != prev_running) {
        mmss(seconds, t2, sizeof(t2));
        if (running == 1) {
            program_name(t1, sizeof(t1));
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
        control_status_t k = control_get_status();
        mmss(seconds, t2, sizeof(t2));
        describe_cm8(t3, sizeof(t3), k.speed_sent);
        if (d.vars[5]) {
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

#if DEBUG_TASK_STATS
// Stack còn trống ít nhất từ lúc chạy (ESP-IDF: tính bằng byte). Còn < ~500 byte là nên tăng stack.
static void print_task_stats(void)
{
    static const char *TASK_NAMES[] = {"control", "cm8", "hmi_rx", "hmi_poll", "logger", "settings"};
    printf("---- Stack con trong (byte):");
    for (int i = 0; i < 6; i++) {
        TaskHandle_t h = xTaskGetHandle(TASK_NAMES[i]);
        if (h) printf(" %s=%u", TASK_NAMES[i], (unsigned)uxTaskGetStackHighWaterMark(h));
    }
    printf(" | heap trong=%u ----\n", (unsigned)xPortGetFreeHeapSize());
}
#endif

static void task_logger(void *arg)
{
    (void)arg;
    char line[LOG_LINE_LEN];
    bool warned = false;
    int64_t last_stats_ms = ms_now();
    while (1) {
        // Chờ dòng log tối đa 100 ms, rồi in hết các dòng đang chờ
        if (xQueueReceive(log_queue, line, pdMS_TO_TICKS(100)) == pdTRUE) {
            printf("%s\n", line);
            while (xQueueReceive(log_queue, line, 0) == pdTRUE) printf("%s\n", line);
        }

        d = hmi_get();
        print_progress();

        if (!warned && ms_now() > 5000 && !d.has_vars) {
            printf("(Chưa nhận được dữ liệu từ màn hình - kiểm tra dây GPIO16/GPIO17/GND)\n");
            warned = true;
        }
#if DEBUG_TASK_STATS
        if (ms_now() - last_stats_ms >= 30000) { last_stats_ms = ms_now(); print_task_stats(); }
#else
        (void)last_stats_ms;
#endif
    }
}

void logger_start(void)
{
    log_queue = xQueueCreate(LOG_QUEUE_LEN, LOG_LINE_LEN);
    xTaskCreatePinnedToCore(task_logger, "logger", STACK_LOGGER, NULL, PRIO_LOGGER, NULL, APP_CORE);
}
