/*
 * e8_rtos (ESP-IDF) - Màn hình E8+ + mạch động cơ CM8 + WiFi, chia thành nhiều task FreeRTOS
 *
 *   Task        Ưu tiên  Core  Việc
 *   control        6      1    50 ms/lần: đọc dữ liệu màn hình + CM8 -> quyết định lệnh, an toàn
 *   cm8            5      1    Modbus RTU với CM8 (UART1, GPIO32/33, 9600)
 *   hmi_rx         5      1    nhận + giải mã khung từ màn hình (UART2, GPIO16/17, 115200)
 *   hmi_poll       4      1    hỏi màn hình: 0x1011/0x1012 mỗi 100 ms, biến 0x1000.., trang 0x7000
 *   settings       3      1    chữ Sismo (chạm / giữ), mật khẩu, trang cài đặt WiFi, trang khởi động
 *   logger         2      1    in log ra Monitor (các task khác gửi chuỗi qua hàng đợi)
 *   (WiFi, lwIP, web server cấu hình 192.168.10.100 chạy ở core 0 - xem network.c, web.c)
 *
 *   Dữ liệu dùng chung: mutex (màn hình), critical section (CM8, control, network),
 *   queue (log, sự kiện nút, lần bấm +/-).
 *   Task watchdog: task nào không báo "còn sống" quá 3 s -> ESP32 khởi động lại,
 *   sau đó màn hình bị cho tạm dừng, người dùng phải bấm ▶ lại.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "config.h"
#include "cm8.h"
#include "hmi.h"
#include "control.h"
#include "logger.h"
#include "network.h"
#include "settings.h"

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "cap dien";
    case ESP_RST_SW:       return "khoi dong lai bang phan mem";
    case ESP_RST_PANIC:    return "LOI CHUONG TRINH (panic)";
    case ESP_RST_INT_WDT:  return "WATCHDOG ngat";
    case ESP_RST_TASK_WDT: return "WATCHDOG task (co task bi treo)";
    case ESP_RST_WDT:      return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "SUT AP nguon";
    case ESP_RST_EXT:      return "nut EN / chan reset";
    default:               return "khac";
    }
}

void app_main(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    printf("\n=== E8 + CM8 (FreeRTOS) | khoi dong vi: %s ===\n", reset_reason_name(r));

    // Task watchdog: cấu hình lại ngay trong code (không phụ thuộc file sdkconfig)
    esp_task_wdt_config_t wdt = {
        .timeout_ms     = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0) | (1 << 1),   // giám sát cả task Idle của 2 core
        .trigger_panic  = true,                  // quá hạn -> khởi động lại
    };
    if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);

    // Thứ tự: log trước (để task khác gửi log được), rồi CM8, màn hình, điều khiển
    logger_start();
    cm8_begin();
    hmi_start();
    control_start();
    net_start();               // NVS + WiFi: có WiFi đã lưu thì tự kết nối
    settings_start();

    printf("Da tao 6 task: control, cm8, hmi_rx, hmi_poll, settings, logger | AP cau hinh: %s\n",
           net_ap_name());
    // app_main kết thúc ở đây; các task tiếp tục chạy độc lập
}
