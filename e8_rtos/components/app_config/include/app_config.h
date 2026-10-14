/*
 * app_config.h - Cấu hình chung của cả ứng dụng: bảng task (ưu tiên / stack / core), watchdog, gỡ lỗi
 *
 * Thông số riêng của từng module nằm trong module đó:
 *   chân + baud màn hình  -> components/hmi/include/hmi.h
 *   địa chỉ / số trang UI -> components/hmi/include/hmi_map.h
 *   chân + thanh ghi CM8  -> components/motor_modbus/include/motor_modbus.h
 *   cường độ / Xung / CM8 -> components/pages/page_session/page_session.c
 *   mật khẩu, giữ Sismo   -> components/pages/page_setting/page_setting.c
 *   WiFi AP / web         -> components/network/include/network.h
 */
#pragma once

// ---------------- Bảng task ----------------
// Ưu tiên: số càng lớn càng được chạy trước. Task an toàn (page_session) cao nhất,
// in log thấp nhất để không bao giờ làm chậm phần điều khiển.
#define PRIO_SESSION              6             // page_session: điều khiển động cơ, an toàn (50 ms)
#define PRIO_MOTOR                5             // motor_modbus: Modbus RTU với CM8
#define PRIO_HMI_RX               5             // hmi: nhận khung từ màn hình
#define PRIO_HMI_POLL             4             // hmi: hỏi màn hình định kỳ
#define PRIO_SETTING              3             // page_setting: Sismo, mật khẩu, WiFi
#define PRIO_LOGGER               2             // logger: in log

// Stack: trong ESP-IDF tính bằng BYTE (FreeRTOS gốc tính bằng word)
#define STACK_SESSION             4096
#define STACK_MOTOR               4096
#define STACK_HMI                 4096
#define STACK_SETTING             5120          // bật AP + web chạy trong task này -> cần nhiều stack hơn
#define STACK_LOGGER              4096

#define APP_CORE                  1             // các task ứng dụng chạy core 1 (core 0: WiFi, lwIP, web)

// ---------------- Watchdog ----------------
#define WDT_TIMEOUT_MS            3000          // task nào không báo "còn sống" quá 3 s -> khởi động lại

// ---------------- Gỡ lỗi ----------------
#define DEBUG_TASK_STATS          1             // 1: mỗi 30 s in stack còn trống của từng task
#define DEBUG_SISMO_EVENTS        0             // 1: in mỗi lần thấy nút Sismo / phím mật khẩu đổi
#define DEBUG_INTENSITY           0             // 1: in mỗi lần cường độ 0x1000 đổi (kèm thời gian)
#define DEBUG_UNHANDLED_EVENTS    0             // 1: in sự kiện màn hình không trang nào xử lý
#define TEST_TASK_HANG            0             // 1: sau 20 s cố tình làm task "hmi_poll" bị treo
                                                //    để thử watchdog (NHỚ ĐỂ 0 khi dùng thật)
