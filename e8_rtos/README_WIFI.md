# e8_rtos + WiFi

| File | Nội dung |
|---|---|
| `main.c` | Khởi động: watchdog, tạo các task |
| `config.h` | Chân, thông số, ưu tiên / stack của task |
| `hmi.c/h` | Task `hmi_rx` + `hmi_poll`: giao tiếp màn hình (UART2) |
| `cm8.c/h` | Task `cm8`: Modbus RTU với mạch CM8 (UART1) |
| `control.c/h` | Task `control`: quyết định lệnh CM8, cường độ, Xung, an toàn |
| `settings.c/h` | Task `settings`: chữ Sismo, mật khẩu 74700, trang cài đặt WiFi, trang khởi động |
| `network.c/h` | WiFi STA + AP cấu hình, lưu WiFi vào `/spiffs/wifi.json` |
| `web.c/h` | Web cấu hình http://192.168.10.100 (Settings, Upload, /wifi.json) |
| `logger.c/h` | Task `logger`: in log ra Monitor |
| `partitions.csv` | 2 vùng OTA + vùng `storage` (SPIFFS 128 KB) chứa wifi.json |

## Khi đổi bảng phân vùng / sdkconfig.defaults
Xóa file **`sdkconfig`** và thư mục **`build`** trong `e8_rtos` rồi build lại
(không xóa `sdkconfig` thì ESP-IDF giữ cấu hình cũ, không đọc `sdkconfig.defaults` mới).
