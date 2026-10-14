# e8_rtos — firmware ESP32 cho E8+

ESP-IDF v5.5 + FreeRTOS. Màn hình HMI (LT7689) qua UART2, mạch động cơ CM8 qua Modbus RTU (UART1), WiFi.

## Cấu trúc

```
e8_rtos/
├── CMakeLists.txt            project + PROJECT_VER (phiên bản firmware)
├── partitions.csv            nvs | otadata | ota_0 | ota_1 | storage (SPIFFS: wifi.json)
├── sdkconfig.defaults
├── main/main.c               khởi động: watchdog + gọi *_start() của từng module
└── components/
    ├── app_config/           bảng task (ưu tiên / stack / core), watchdog, cờ gỡ lỗi
    ├── device_settings/      cài đặt lưu flash /spiffs/settings.json (MODE WIFI / LOCAL ...), gắn SPIFFS
    ├── logger/               task logger: log_send() + hook in báo cáo định kỳ
    ├── hmi/                  UART màn hình (task hmi_rx, hmi_poll)
    │   └── include/hmi_map.h số trang, địa chỉ biến của UI — sửa UI thì sửa file này
    ├── motor_modbus/         Modbus RTU với CM8 (task motor)
    ├── ui_router/            chia sự kiện màn hình cho từng trang
    ├── pages/
    │   ├── page_session/     trang buổi tập 6/8/10/11: động cơ, cường độ, Xung, an toàn (task session)
    │   └── page_setting/     Sismo, mật khẩu, 74700 cài đặt WiFi, 74710 MODE WIFI/LOCAL, trang khởi động (task setting)
    ├── network/              WiFi STA + AP cấu hình, web 192.168.10.100, wifi.json
    └── ota/                  cập nhật firmware qua web + rollback
```

Phụ thuộc (mũi tên = "dùng"):

```
main ──► tất cả
ui_router ──► page_session, page_setting, hmi
page_setting ──► hmi, network, page_session
page_session ──► hmi, motor_modbus
network ──► ota
hmi, motor_modbus, logger, ota ──► (chỉ ESP-IDF + app_config)
```

## Luồng sự kiện màn hình

```
màn hình ──UART──► hmi_rx ──► ui_router ──► page_session_on_event()   (0x1000 cường độ +/-)
                                       └──► page_setting_on_event()   (0x1011 phím, 0x1012 Sismo, 0x1013 nút WiFi)
```
Mỗi page nhận sự kiện thì đưa vào hàng đợi của mình rồi trả về ngay; task của page xử lý sau.

## Trang cài đặt theo mật khẩu

| Mật khẩu | Trang | Nội dung |
|---|---|---|
| 74700 | 0027 | WiFi: OFFLINE / ONLINE → Configuration WIFI (AP + web) |
| 74710 | 0028 | MODE WIFI (phải ONLINE mới cho động cơ chạy) / MODE LOCAL (không cần mạng) |

Thêm mật khẩu mới: thêm 1 dòng vào `PASSWORD_TABLE` trong `page_setting.c`.

## Thêm 1 trang mới

1. Tạo `components/pages/page_xxx/` gồm `page_xxx.c`, `include/page_xxx.h`, `CMakeLists.txt`
   (chép từ `page_setting`).
2. Viết `bool page_xxx_on_event(const hmi_event_t *e)` — trả về `true` nếu là nút của trang này.
3. Thêm đường dẫn vào `EXTRA_COMPONENT_DIRS` trong `CMakeLists.txt` gốc.
4. Thêm 1 dòng `if (page_xxx_on_event(e)) return;` trong `ui_router.c` và `page_xxx` vào `PRIV_REQUIRES`
   của `components/ui_router/CMakeLists.txt`.
5. Địa chỉ / số trang mới thêm vào `hmi_map.h`.

## Cập nhật firmware qua web (OTA)

1. Tăng `PROJECT_VER` trong `CMakeLists.txt` gốc (vd. `1.1.0` → `1.1.1`), Build.
2. Trên màn hình: giữ Sismo → 74700 → bấm ONLINE/OFFLINE → "Configuration WIFI".
3. Máy tính / điện thoại vào WiFi `SISMO_E8_XXXXXX` (mật khẩu `12345678`), mở http://192.168.10.100 → tab **Upload**.
4. Chọn `build/e8_rtos.bin` → Upload. Xong ESP32 tự khởi động lại bằng firmware mới.
5. Firmware mới chạy ổn **15 s** thì tự xác nhận. Bị crash / watchdog / mất điện trước đó → tự quay về bản cũ.
6. Tab Upload có nút **Roll back** để quay về bản trước bất cứ lúc nào.

Chỉ nhận file `.bin` của project này (so tên project trong file), file khác bị từ chối trước khi ghi.

## Khi đổi bảng phân vùng / sdkconfig.defaults

Xóa file `sdkconfig` và thư mục `build` rồi build lại (không xóa thì ESP-IDF giữ cấu hình cũ).
Lần nạp đầu sau khi bật rollback phải nạp qua cáp USB (Flash) để ghi bootloader mới.
