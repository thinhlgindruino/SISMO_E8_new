# e8_rtos + WiFi (09/10)

Thêm vào e8_rtos: `mang.c/h` (WiFi STA + AP cấu hình + NVS), `web.c/h` (trang 192.168.10.100),
`cai_dat.c/h` (task chữ Sismo, mật khẩu 74700, nút OFFLINE), `partitions.csv` (2 vùng OTA).

## BẮT BUỘC trước khi build lần đầu
Bảng phân vùng đã đổi → xóa cấu hình cũ: xóa file **`sdkconfig`** và thư mục **`build`** trong `e8_rtos`
(hoặc VS Code: Ctrl+Shift+P → *ESP-IDF: Full Clean*, rồi vẫn phải xóa `sdkconfig`).
Không xóa `sdkconfig` thì ESP-IDF giữ bảng phân vùng cũ (1 MB) và không dùng `sdkconfig.defaults` mới.

WiFi đã lưu bằng project e8_wifi vẫn còn (cùng vị trí NVS) → nạp xong sẽ tự vào lại mạng cũ.
