/*
 * config.h - Cấu hình chung: chân, thông số, ưu tiên và stack của các task
 */
#pragma once

// ---------------- Màn hình HMI ----------------
#define HMI_UART                  UART_NUM_2
#define HMI_RX_PIN                17            // nối TXA của màn hình
#define HMI_TX_PIN                16            // nối RXB của màn hình
#define HMI_BAUD                  115200
#define HMI_VAR_POLL_MS           100           // hỏi biến 0x1000..0x1009 mỗi 100 ms
#define HMI_PAGE_POLL_MS          250           // hỏi trang hiện tại (0x7000) mỗi 250 ms
#define HMI_LINK_TIMEOUT_MS       2000          // quá 2 s không nhận được dữ liệu -> coi như mất màn hình

// ---------------- CM8 (chân đặt trong cm8.h) ----------------
#define RUN_MODE                  3             // Mode khi động cơ chạy (CHƯA có giá trị chính thức -> tạm 3)
#define RUN_RATIO                 9             // Ratio (code CM8 lấy 9 làm chuẩn)
#define CURRENT_LIMIT_X100        370           // 3,70 A
#define SPEED_MAX                 32            // tốc độ CM8 0..32 (CM8 chỉ dừng đúng ở số CHẴN)
#define SPEED_EVEN_ONLY           1             // 1: chỉ gửi tốc độ chẵn (CM8 tăng/giảm 2 mức mỗi bước)
                                                // 0: gửi đủ 0..32, số lẻ thì CM8 dao động ±1 mức quanh giá trị đó
#define PREPARE_TIMEOUT_MS        3000          // chờ CM8 về trạng thái 0 trước lần chạy đầu (tối đa)
#define PAUSE_SCREEN_ON_ERROR     1             // 1: CM8 lỗi / mất kết nối khi đang tập -> ghi 0x1001 = 0

// ---------------- Xung ----------------
#define PULSE_AMPLITUDE           2             // dao động ± 2 mức
#define PULSE_STEP_MS             300           // mỗi 300 ms đổi 1 mức

#define ADDR_INTENSITY            0x1000        // nút +/- và đồng hồ cường độ cùng dùng biến này.
                                                // Đang tập + Xung: ESP32 ghi mức dao động vào đây, tắt Xung
                                                // thì ghi trả lại cường độ người dùng chọn.
#define SCREEN_SYNC_MS            600           // sau khi ghi trả cường độ, chờ màn hình cập nhật tối đa

// ---------------- Cài đặt / WiFi (giai đoạn 09/10) ----------------
#define PAGE_STARTUP              0             // trang 0000_Demarrage (màn hình về đây sau khi Submit WiFi)
#define PAGE_STARTUP_SHOW_MS      3000          // trang 0 hiện 3 s rồi ESP32 tự chuyển sang trang chính
#define PAGE_MAIN                 1             // trang menu 4 nút
#define PAGE_PASSWORD             26            // trang nhập mật khẩu cài đặt (0026)
#define PAGE_SETTINGS             27            // trang cài đặt OFFLINE / Configuration WIFI / ONLINE (0027)
#define SETTINGS_PASSWORD         "74700"
#define SETTINGS_PASSWORD_LEN     5             // đủ 5 số: đúng thì vào cài đặt, sai thì bấm số thứ 6 sẽ xóa

#define ADDR_PASSWORD_KEY         0x1011        // phím trang mật khẩu: 0..9 = số, 10 = C
#define ADDR_SISMO_BUTTON         0x1012        // chữ "Sismo" trên mọi trang (giữ = lặp)
#define ADDR_SETTINGS_BUTTON      0x1013        // nút OFFLINE / ONLINE trên trang cài đặt
#define ADDR_STAR_COUNT           0x1039        // số dấu * đang hiện (Icon 0403..0408 = 0..5 dấu)
#define ADDR_NETWORK_ICON         0x1040        // Icon nút: 0 OFFLINE, 1 Configuration WIFI, 2 ONLINE

#define KEY_EMPTY                 0xFFFF        // ESP32 ghi vào 0x1011 sau mỗi lần đọc được phím
#define KEY_CLEAR                 10
#define SISMO_HOLD_MS             2000          // giữ "Sismo" 2 s -> vào trang mật khẩu
#define RELEASE_TIMEOUT_MS        1200          // dự phòng: quá 1,2 s giá trị 0x1012 không đổi -> coi như đã thả tay
                                                // (bình thường biết thả tay nhờ màn hình báo 0x41 lúc thả)
#define IGNORE_AFTER_RELEASE_MS   400           // sau khi thả tay, bỏ qua thay đổi còn sót của 0x1012

#define PRIO_SETTINGS             3
#define STACK_SETTINGS            5120          // bật AP + web chạy trong task này -> cần nhiều stack hơn

// ---------------- Task ----------------
// Ưu tiên: số càng lớn càng được chạy trước. Task an toàn (điều khiển) cao nhất,
// in log thấp nhất để không bao giờ làm chậm phần điều khiển.
#define PRIO_CONTROL              6
#define PRIO_CM8                  5
#define PRIO_HMI_RX               5
#define PRIO_HMI_POLL             4
#define PRIO_LOGGER               2

// Stack: trong ESP-IDF tính bằng BYTE (FreeRTOS gốc tính bằng word)
#define STACK_CONTROL             4096
#define STACK_CM8                 4096
#define STACK_HMI                 4096
#define STACK_LOGGER              4096

#define APP_CORE                  1             // chạy các task trên core 1 (core 0 để hệ thống)

#define CONTROL_PERIOD_MS         50
#define WDT_TIMEOUT_MS            3000          // task nào không báo "còn sống" quá 3 s -> khởi động lại

// ---------------- Gỡ lỗi ----------------
#define DEBUG_TASK_STATS          1             // 1: mỗi 30 s in stack còn trống của từng task
#define DEBUG_SISMO_EVENTS        0             // 1: in mỗi lần thấy nút Sismo đổi (để đo lúc giữ) - chạy ổn thì đặt 0
#define DEBUG_INTENSITY           0             // 1: in mỗi lần cường độ 0x1000 đổi (kèm thời gian) để chẩn đoán
#define TEST_TASK_HANG            0             // 1: sau 20 s cố tình làm task "hmi_poll" bị treo
                                                //    để thử watchdog (NHỚ ĐỂ 0 khi dùng thật)
