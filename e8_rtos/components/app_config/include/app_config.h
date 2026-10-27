/*
 * app_config.h - Cấu hình chung của cả ứng dụng: bảng task, watchdog, cờ gỡ lỗi
 *
 * Bảng task (ưu tiên càng lớn càng được chạy trước; stack tính bằng BYTE; tất cả chạy core 1,
 * core 0 để WiFi / lwIP / web):
 *   session  6   điều khiển động cơ + an toàn, chu kỳ 50 ms
 *   motor    5   Modbus RTU với CM8
 *   hmi_rx   5   nhận khung từ màn hình
 *   hmi_poll 4   hỏi màn hình định kỳ
 *   setting  3   Sismo, mật khẩu, trang cài đặt (bật AP + web chạy trong task này -> stack 5120)
 *   program  3   Programmes: chọn mức độ, trang buổi tập, bài tập con
 *   logger   2   in log (thấp nhất để không làm chậm điều khiển)
 *   cloud    3   ThingsBoard (MQTT, RPC, OTA) - chạy core 0 cùng WiFi, không chung core với task điều khiển
 *
 * WDT_TIMEOUT_MS: task nào không báo "còn sống" quá 3 s -> ESP32 khởi động lại.
 *
 * Cờ gỡ lỗi (1 = bật):
 *   DEBUG_TASK_STATS        mỗi 30 s in stack còn trống của từng task
 *   DEBUG_SISMO_EVENTS      in mỗi lần nút Sismo / phím mật khẩu đổi
 *   DEBUG_INTENSITY         in mỗi lần cường độ 0x1000 đổi (kèm thời gian)
 *   DEBUG_UNHANDLED_EVENTS  in sự kiện màn hình không trang nào nhận
 *   TEST_TASK_HANG          sau 20 s cố tình treo task hmi_poll để thử watchdog (để 0 khi dùng thật)
 *   STARTUP_REQUIRE_CM8     1: trang khởi động 0000 chờ CM8 trả lời bình thường mới vào menu (thử bàn không
 *                           có CM8 thì để 0)
 *   DEBUG_FAKE_CM8_ERROR    thử thông báo lỗi khi không làm hỏng được CM8: 3 hoặc 4 -> sau
 *                           DEBUG_FAKE_CM8_ERROR_AFTER_MS kể từ khi ESP32 bật, coi như CM8 báo lỗi đó (tới
 *                           khi ESP32 khởi động lại); 0 = tắt (dùng thật)
 *
 * Thông số riêng của từng module nằm trong module đó:
 *   chân + baud màn hình   -> components/hmi/include/hmi.h
 *   địa chỉ / số trang UI  -> components/hmi/include/hmi_map.h
 *   chân + thanh ghi CM8   -> components/motor_modbus/include/motor_modbus.h
 *   cường độ / Xung / CM8  -> components/pages/page_session/page_session.c
 *   mật khẩu, giữ Sismo    -> components/pages/page_setting/page_setting.c
 *   WiFi AP / web          -> components/network/include/network.h
 *   ThingsBoard            -> components/cloud/include/cloud.h
 */
#pragma once

#define PRIO_SESSION              6
#define PRIO_MOTOR                5
#define PRIO_HMI_RX               5
#define PRIO_HMI_POLL             4
#define PRIO_SETTING              3
#define PRIO_PROGRAM              3
#define PRIO_LOGGER               2
#define PRIO_CLOUD                3

#define STACK_SESSION             4096
#define STACK_MOTOR               4096
#define STACK_HMI                 4096
#define STACK_SETTING             5120
#define STACK_PROGRAM             4096
#define STACK_LOGGER              4096
#define STACK_CLOUD               6144

#define APP_CORE                  1
#define NET_CORE                  0

#define WDT_TIMEOUT_MS            3000

#define DEBUG_TASK_STATS          1
#define DEBUG_SISMO_EVENTS        0
#define DEBUG_INTENSITY           0
#define DEBUG_UNHANDLED_EVENTS    0
#define TEST_TASK_HANG            0
#define STARTUP_REQUIRE_CM8       1
#define DEBUG_FAKE_CM8_ERROR      0
#define DEBUG_FAKE_CM8_ERROR_AFTER_MS 60000
