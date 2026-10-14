/*
 * logger.c - Task in log (xem logger.h)
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "app_config.h"
#include "logger.h"

#define LOG_LINE_LEN              160
#define LOG_QUEUE_LEN             16

static QueueHandle_t log_queue;
static void (*hooks[LOGGER_MAX_HOOKS])(void);
static int hook_count = 0;

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

// Gọi lúc khởi động (trước khi các task chạy ổn định), theo thứ tự đăng ký
void logger_add_hook(void (*hook)(void))
{
    if (hook && hook_count < LOGGER_MAX_HOOKS) hooks[hook_count++] = hook;
}

#if DEBUG_TASK_STATS
// Stack còn trống ít nhất từ lúc chạy (ESP-IDF: tính bằng byte). Còn < ~500 byte là nên tăng stack.
static void print_task_stats(void)
{
    static const char *TASK_NAMES[] = {"session", "motor", "hmi_rx", "hmi_poll", "logger", "setting"};
    printf("---- Stack con trong (byte):");
    for (int i = 0; i < (int)(sizeof(TASK_NAMES) / sizeof(TASK_NAMES[0])); i++) {
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
    int64_t last_stats_ms = ms_now();
    while (1) {
        // Chờ dòng log tối đa 100 ms, rồi in hết các dòng đang chờ
        if (xQueueReceive(log_queue, line, pdMS_TO_TICKS(100)) == pdTRUE) {
            printf("%s\n", line);
            while (xQueueReceive(log_queue, line, 0) == pdTRUE) printf("%s\n", line);
        }
        for (int i = 0; i < hook_count; i++) hooks[i]();
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
