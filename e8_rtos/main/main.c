/*
 * main.c - E8+ (ESP-IDF + FreeRTOS): màn hình E8+, mạch động cơ CM8, WiFi
 *
 * Cấu trúc (mỗi thư mục trong components/ là 1 module):
 *   app_config          cấu hình chung: bảng task, watchdog, gỡ lỗi
 *   device_settings     gắn SPIFFS, cài đặt lưu flash (/spiffs/settings.json)
 *   hmi                 UART màn hình + hmi_map (số trang, địa chỉ UI)       task hmi_rx (5), hmi_poll (4)
 *   motor_modbus        Modbus RTU với mạch CM8                               task motor (5)
 *   ui_router           chia sự kiện màn hình cho từng trang
 *   pages/page_session  trang buổi tập: điều khiển động cơ, cường độ, Xung    task session (6)
 *   pages/page_setting  Sismo, mật khẩu, trang 74700 / 74710 / 74766 / 200816  task setting (3)
 *   pages/page_program  Programmes + Perso: trang 0006 / 0014 / 0013, chạy bài     task program (3)
 *   network             WiFi STA/AP, web 192.168.10.100, wifi.json             (core 0)
 *   ota                 cập nhật firmware qua web (tab Upload) + tự quay về bản cũ khi bản mới lỗi
 *   logger              in log ra Monitor                                      task logger (2)
 *
 * app_main: in lý do khởi động, cấu hình task watchdog 3 s (giám sát cả task Idle của 2 core,
 * quá hạn -> khởi động lại), rồi khởi động các module theo thứ tự: logger (để module khác gửi log
 * được) -> device_settings (SPIFFS, trước network) -> CM8 -> màn hình -> router -> các trang
 * -> mạng -> OTA. app_main kết thúc, các task tiếp tục chạy độc lập.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "app_config.h"
#include "logger.h"
#include "hmi.h"
#include "motor_modbus.h"
#include "ui_router.h"
#include "page_session.h"
#include "page_setting.h"
#include "page_program.h"
#include "network.h"
#include "ota.h"
#include "device_settings.h"

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
    printf("\n=== E8+ (FreeRTOS) | khoi dong vi: %s ===\n", reset_reason_name(esp_reset_reason()));

    esp_task_wdt_config_t wdt = {
        .timeout_ms     = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic  = true,
    };
    if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);

    logger_start();
    device_settings_start();
    motor_start();
    hmi_start();
    ui_router_start();
    page_session_start();
    net_start();
    page_setting_start();
    page_program_start();
    ota_start();

    printf("Da tao 7 task: session, motor, hmi_rx, hmi_poll, setting, program, logger | AP cau hinh: %s\n",
           net_ap_name());
}
