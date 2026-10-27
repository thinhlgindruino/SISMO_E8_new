# E8+ và ThingsBoard: hướng dẫn cài server, kết nối máy, thử bằng MQTTX

Áp dụng cho firmware **1.13.0** trở lên và ThingsBoard **Community Edition 4.3.1.7** chạy bằng Docker trên Windows.

## 0. Tổng quan

```
 E8+ (ESP32) ──WiFi── MQTT :1883 ──► ThingsBoard (Docker trên laptop / server công ty)
   task "cloud"                         ├─ Device profile "E8+" (tự đăng ký máy mới)
                                        ├─ Dashboard "E8+ Sismo" (danh sách máy → bảng điều khiển 1 máy)
                                        └─ OTA: gói firmware "E8+" theo phiên bản, khách chọn trên dashboard
```

Các file trong thư mục `thingsboard/`:

| File | Dùng để |
|---|---|
| `docker-compose.yml` | Chạy ThingsBoard + PostgreSQL bằng Docker |
| `tb_setup.py` | Tạo sẵn device profile, widget, dashboard trên server (chạy 1 lệnh) |
| `widget/` | Mã nguồn widget "E8+ Control" (HTML / CSS / JS + logo) |
| `e8_sim.py` | Máy E8+ giả chạy trên máy tính, để demo khi chưa cắm ESP32 |
| `export/` | Widget và dashboard dạng JSON, để Import bằng tay nếu không chạy được `tb_setup.py` |

## 1. Cài Docker Desktop

1. Tải Docker Desktop for Windows tại docker.com, cài với tùy chọn **Use WSL 2** (mặc định).
2. Khởi động lại máy nếu được hỏi, mở Docker Desktop và chờ biểu tượng cá voi báo "Engine running".
3. Mở **PowerShell** và kiểm tra:
   ```powershell
   docker --version
   docker compose version
   ```
   Cần tối thiểu 4 GB RAM trống cho ThingsBoard.

## 2. Chạy ThingsBoard

```powershell
mkdir C:\thingsboard
copy C:\SISMO_E8_new\thingsboard\docker-compose.yml C:\thingsboard\
cd C:\thingsboard
docker compose run --rm -e INSTALL_TB=true -e LOAD_DEMO=true thingsboard-ce
docker compose up -d
docker compose logs -f thingsboard-ce
```

- Lệnh `run ... INSTALL_TB=true` chỉ chạy **1 lần đầu**: nó tạo cơ sở dữ liệu kèm dữ liệu mẫu, xong tự thoát.
- Khi log hiện `Started ThingsBoard` thì bấm Ctrl+C để thoát xem log (server vẫn chạy). Lần đầu mất khoảng 1–2 phút.
- Mở trình duyệt tại `http://localhost:8080`. Tài khoản mẫu:

| Vai trò | Email | Mật khẩu |
|---|---|---|
| Quản trị hệ thống | sysadmin@thingsboard.org | sysadmin |
| Quản trị công ty (tenant) | tenant@thingsboard.org | tenant |
| Khách hàng mẫu | customerA@thingsboard.org | customer |

- Tắt server: `docker compose down`. Bật lại: `docker compose up -d`. Dữ liệu vẫn còn vì nằm trong volume `tb-postgres-data`.
- Đem lên server thật thì phải đổi hết mật khẩu mẫu.

## 3. Mở cổng cho ESP32 vào được laptop

ESP32 và laptop phải cùng một mạng WiFi. Mở **PowerShell (Run as administrator)**:

```powershell
New-NetFirewallRule -DisplayName "ThingsBoard MQTT" -Direction Inbound -Protocol TCP -LocalPort 1883 -Action Allow
New-NetFirewallRule -DisplayName "ThingsBoard Web"  -Direction Inbound -Protocol TCP -LocalPort 8080 -Action Allow
ipconfig
```

Ghi lại **IPv4 Address** của card WiFi, ví dụ `192.168.1.20`. Đây là địa chỉ server sẽ nhập cho E8+.

## 4. Cài E8+ lên server

Cách 1 (nên dùng): chạy script, cần Python 3.

```powershell
cd C:\SISMO_E8_new\thingsboard
pip install requests paho-mqtt
python tb_setup.py --url http://localhost:8080 --user tenant@thingsboard.org --password tenant --customer "Customer A"
```

Script tạo hoặc cập nhật 4 thứ (chạy lại nhiều lần không sao):
- **Device profile `E8+`**: bật "Allow to create new devices", provision key `e8plus-provision-key`, secret `e8plus-provision-secret`. Hai giá trị này phải trùng `CLOUD_PROVISION_KEY / SECRET` trong `components/cloud/include/cloud.h`.
- **Widget `E8+ Control`**, nằm trong bundle "E8+ Sismo".
- **Dashboard `E8+ Sismo`**: trang "Machines E8+" là danh sách máy. Bấm một dòng để mở trang điều khiển máy đó, kèm bảng lịch sử 7 ngày.
- `--customer`: gán dashboard cho khách đó.

Cách 2 (bằng tay):
1. Profiles → Device profiles → `+` → tên `E8+`, Transport MQTT, tab **Device provisioning**: "Allow to create new devices", nhập key và secret như trên.
2. Resources → Widgets library → `+` → Import widgets bundle → chọn `export/e8plus_widgets_bundle.json`.
3. Dashboards → `+` → Import dashboard → chọn `export/e8plus_dashboard.json`.

## 5. Kết nối E8+ thật

1. Nạp firmware 1.13.0 (VS Code ESP-IDF: Build, Flash).
2. Trên màn hình: giữ chữ **Sismo** 2 s → nhập **74700** → bấm ONLINE / OFFLINE để vào **Configuration WIFI**.
3. Điện thoại vào WiFi `SISMO_E8_XXXXXX` (mật khẩu `12345678`) → mở `http://192.168.10.100`:
   - Tab **Settings**: nhập WiFi nếu chưa có (máy sẽ khởi động lại).
   - Tab **Server** (mới): nhập IP laptop (bước 3), cổng `1883` → **Save**. Không cần khởi động lại.
4. Thoát cấu hình. Monitor sẽ in:
   ```
   [CLOUD] Ket noi 192.168.1.20:1883, thiet bi SISMO_E8_XXXXXX
   [CLOUD] Server chua co thiet bi SISMO_E8_XXXXXX -> dang ky (provision)
   [CLOUD] Dang ky: SUCCESS
   [CLOUD] -> ONLINE
   ```
   Lần sau máy đăng nhập thẳng, không phải đăng ký lại. Token tính từ tên máy, nên không cần lưu. Máy bị xóa trên server thì lần kết nối sau tự đăng ký lại.
5. Trên ThingsBoard (tài khoản tenant): **Entities → Devices** sẽ thấy `SISMO_E8_XXXXXX` loại `E8+`. Muốn khách xem được máy thì bấm **Assign to customer**.

Để trống ô Server trên web thì máy ngắt khỏi ThingsBoard (trạng thái `OFF`).

## 6. Dashboard: khách dùng như thế nào

Phần **bảng điều khiển** làm giống mẫu E8 cũ:

| Thành phần | Ý nghĩa |
|---|---|
| Tên máy, giờ `HH:MM:SS` | Đang tập: thời gian còn lại (tự đếm lùi). Đang ở trang buổi tập mà chưa chạy: thời gian dự kiến |
| Chấm tròn lớn | Xanh = online, xám = offline, đỏ = máy đang báo lỗi (ERR.02 / 03 / 04) |
| ▶ ❚❚ ■ | Play / Pause / Stop. Nút **hồng** là trạng thái hiện tại (■ hồng = đang dừng) |
| 🔒 🔓 | Khóa / mở **điều khiển từ xa**. Khi khóa, Play / Pause / Stop / đổi cài đặt / khởi động lại từ dashboard đều bị máy từ chối |
| 👤 | Bảng thông tin: tên khách, thông tin, ghi chú (server attributes), firmware, số buổi tập, WiFi, IP, MAC; **chọn phiên bản firmware và cập nhật** |
| 5 dòng cài đặt | Accès, Mode, Timer, Langue, Ratio. Bấm để đổi giá trị (chấm **cam** = chưa gửi), bấm **OK** để gửi. Chấm **xanh** = máy đã nhận |

Nguyên tắc an toàn (máy tự kiểm tra, không phụ thuộc dashboard):
- Play từ xa vẫn phải qua mọi điều kiện như bấm trên màn hình (thẻ badgeuse, MODE WIFI, CM8).
- Play khi máy đang ở **menu chính** → máy mở trang **Manuel** rồi chạy. Đang ở trang khác (cài đặt, mật khẩu…) thì báo "La machine n'est pas sur le menu ou une séance".
- Không đổi được Mode / Ratio khi động cơ đang chạy. Cặp Mode + Ratio làm CM8 tràn số thì bị từ chối (giống trang 200816).

## 7. Cập nhật firmware từ xa (OTA)

**Công ty upload phiên bản** (tài khoản tenant):
1. Build firmware. Kiểm tra `set(PROJECT_VER "x.y.z")` trong `e8_rtos/CMakeLists.txt`.
2. **Advanced features → OTA updates → `+`**:
   - Title: **`E8+`** (đúng chữ này)
   - Version: **đúng bằng `PROJECT_VER`** (ví dụ `1.13.1`)
   - Device profile: `E8+`, Type: Firmware
   - Upload file `build/e8_rtos.bin`, checksum để mặc định (SHA-256)

**Khách chọn bản** (trên dashboard): 👤 → chọn phiên bản → **Mettre à jour** → **Confirmer ?**

Máy làm theo thứ tự:
1. Chờ máy rảnh (không có buổi tập nào đang chạy hoặc tạm dừng).
2. Tải từng khúc 4 KB. Dòng trạng thái hiện `Téléchargement xx %`.
3. Kiểm SHA-256 → ghi vào vùng OTA còn trống → chờ rảnh → khởi động lại (màn hình về trang 0).
4. Bản mới chạy ổn 15 s thì tự xác nhận. Lần kết nối sau máy báo `UPDATED` (À jour).
5. Bản mới lỗi (treo, crash trước 15 s) → bootloader quay về bản cũ → máy báo `FAILED` và **không tự tải lại** gói đó. Muốn thử lại thì khách chọn lại phiên bản trên dashboard.

Lưu ý:
- Version trên server **phải trùng** `PROJECT_VER` trong file `.bin`. Nếu lệch, máy cài xong vẫn thấy số phiên bản khác nên sẽ báo FAILED.
- File không phải firmware `e8_rtos` (sai project, không phải ảnh ESP32) bị từ chối ngay, máy vẫn chạy bản đang có.
- Tab **Upload** trên web 192.168.10.100 vẫn dùng được như cũ.

## 8. Kịch bản thử bằng MQTTX (không cần ESP32)

MQTTX là phần mềm MQTT client miễn phí (mqttx.app). Mỗi bước có **Kết quả đúng** để đối chiếu.

### 8.1. Tự đăng ký thiết bị (provision)
1. MQTTX → **New Connection**: Name `provision`, Host `mqtt://localhost`, Port `1883`, Client ID `mqttx-test`, Username **`provision`**, Password để trống, MQTT Version 3.1.1 → **Connect**.
2. **New Subscription**: `/provision/response`, QoS 1.
3. Publish lên topic `/provision/request`, kiểu JSON:
   ```json
   {"deviceName":"MQTTX_TEST","provisionDeviceKey":"e8plus-provision-key","provisionDeviceSecret":"e8plus-provision-secret","credentialsType":"ACCESS_TOKEN","token":"mqttxtesttoken000001"}
   ```
   **Kết quả đúng:** nhận `{"credentialsValue":"mqttxtesttoken000001","credentialsType":"ACCESS_TOKEN","status":"SUCCESS"}`. Devices có thêm `MQTTX_TEST` loại `E8+`. Gửi lại lần nữa thì nhận `FAILURE` (tên đã tồn tại), đây là bình thường.
4. Disconnect.

### 8.2. Đăng nhập bằng token, gửi dữ liệu
1. New Connection `e8`: Host / Port như trên, Client ID `MQTTX_TEST`, Username **`mqttxtesttoken000001`** → Connect.
   **Kết quả đúng:** kết nối được. Nếu nhập sai token: "Not authorized" (mã 5).
2. Publish `v1/devices/me/attributes`:
   ```json
   {"model":"E8+","firmware":"1.13.0","access_mode":1,"motor_mode":3,"card_timer":1,"language":0,"motor_ratio":8,"remote_lock":false,"sessions_total":12}
   ```
   **Kết quả đúng:** Devices → MQTTX_TEST → Attributes (Client attributes) có các khóa trên.
3. Publish `v1/devices/me/telemetry`:
   ```json
   {"state":"RUN","kind":"manual","remaining_s":1000,"planned_s":1200,"fault":0,"rssi":-55}
   ```
   **Kết quả đúng:** tab Latest telemetry có `state = RUN`. Gán thiết bị cho Customer A thì trên dashboard thấy giờ `00:16:40` đếm lùi, nút ▶ hồng, 5 dòng hiện "Accès Badgeuse / Mode: 3 / Timer Sismo / Français / Ratio: 8".

### 8.3. Nhận lệnh từ dashboard (RPC)
1. Subscribe `v1/devices/me/rpc/request/+`.
2. Trên dashboard bấm ■ (Stop).
   **Kết quả đúng:** MQTTX nhận trên topic `v1/devices/me/rpc/request/<số>` nội dung `{"method":"stop","params":{}}`.
3. Trong vòng 10 s, publish lên `v1/devices/me/rpc/response/<số đó>`:
   ```json
   {"ok":true,"state":"IDLE"}
   ```
   **Kết quả đúng:** dashboard hết vòng chờ, không báo lỗi. Không trả lời → dashboard báo "Machine hors ligne" (504).
4. Trả lời `{"ok":false,"error":"locked"}`.
   **Kết quả đúng:** dashboard hiện "Commande à distance verrouillée".

### 8.4. Shared attributes và yêu cầu attributes
1. Subscribe `v1/devices/me/attributes`. Trên ThingsBoard: Attributes → Shared attributes → `+` thêm `test_key = 1`.
   **Kết quả đúng:** MQTTX nhận `{"test_key":1}` ngay lập tức.
2. Subscribe `v1/devices/me/attributes/response/+`, publish `v1/devices/me/attributes/request/1`:
   ```json
   {"clientKeys":"firmware","sharedKeys":"test_key,fw_version"}
   ```
   **Kết quả đúng:** nhận trên `.../response/1` dạng `{"client":{"firmware":"1.13.0"},"shared":{"test_key":1}}`.

### 8.5. OTA (tải firmware theo khúc)
1. Tạo 1 gói OTA bất kỳ (mục 7) và gán cho MQTTX_TEST (Devices → thiết bị → Firmware, hoặc khách chọn trên dashboard).
   **Kết quả đúng:** MQTTX (đang subscribe `v1/devices/me/attributes`) nhận `fw_title, fw_version, fw_size, fw_checksum, fw_checksum_algorithm`.
2. Subscribe `v2/fw/response/+/chunk/+`, publish `v2/fw/request/1/chunk/0` với nội dung `4096` (dạng Plaintext).
   **Kết quả đúng:** nhận 4096 byte trên `v2/fw/response/1/chunk/0`. Xin khúc vượt quá cuối file thì nhận 0 byte.
3. Publish `v1/devices/me/telemetry` `{"fw_state":"UPDATED","current_fw_title":"E8+","current_fw_version":"<version gói>"}`.
   **Kết quả đúng:** OTA updates / Firmware của thiết bị báo đã cập nhật.

### 8.6. Online / offline
- Disconnect MQTTX.
  **Kết quả đúng:** trong vài giây, Server attributes `lastDisconnectTime` mới hơn `lastConnectTime`, dashboard chuyển chấm xám, các nút mờ.
- Rút điện ESP32 (không ngắt kết nối "đàng hoàng"): server nhận ra sau khoảng 45 s (1,5 × keepalive 30 s).

Xong thì xóa thiết bị `MQTTX_TEST` cho gọn.

## 9. Máy giả `e8_sim.py`

Chạy giống firmware: tự đăng ký, có buổi tập Manuel giả, trả lời đủ RPC, tải OTA (kiểm SHA-256 rồi "cài" bằng cách đổi số phiên bản).

```powershell
python e8_sim.py --host localhost --name SISMO_E8_SIM001 --version 1.13.0
```

Gõ `error 3` để giả CM8 báo quá dòng, `error 0` để hết lỗi, `quit` để thoát. Muốn khách thấy thì gán thiết bị cho khách như mục 5.

## 10. Bảng dữ liệu E8+ ↔ ThingsBoard

| Loại | Topic | Khóa |
|---|---|---|
| Client attributes (máy gửi lúc kết nối và khi cài đặt đổi) | `v1/devices/me/attributes` | model, firmware, device_name, mac, ip, ssid, access_mode (0 libre / 1 badgeuse), motor_mode (1..5), card_timer (0 badgeuse / 1 sismo), language (0..5 FR EN ES IT DE PT), motor_ratio (2..17), control_mode (0 wifi / 1 local), manual_minutes, remote_lock, sessions_total |
| Telemetry trạng thái (khi đổi; thời gian còn lại 5 s/lần; 30 s/lần dù không đổi) | `v1/devices/me/telemetry` | state (IDLE / COUNTDOWN / RUN / PAUSE), kind (manual / duree / programme), remaining_s, planned_s, intensity, program, level, page, cm8, card, fault (0 / 2 / 3 / 4), rssi, uptime_s, heap |
| Telemetry sự kiện | `v1/devices/me/telemetry` | event = session_start / session_end (session_kind, session_program, session_level, session_planned_s, session_done_s, session_completed), event = error (error_code ERR.0x) |
| OTA | `v1/devices/me/telemetry` | current_fw_title, current_fw_version, fw_state, fw_progress, fw_error |
| Server attributes (nhập trên dashboard) | (REST) | customer_name, customer_info, customer_notes |
| RPC (dashboard → máy) | `v1/devices/me/rpc/request/+` | getStatus, play, pause, stop, setRemoteLock {locked}, setSettings {access_mode, motor_mode, card_timer, language, motor_ratio}, reboot |
| Trả lời RPC | `v1/devices/me/rpc/response/<id>` | ok, error (locked, not_ready, not_running, motor_running, bad_mode_ratio, out_of_range, busy, save_failed, unknown_method) + trạng thái + cài đặt |

## 11. Lỗi thường gặp

| Hiện tượng | Nguyên nhân / cách xử lý |
|---|---|
| Monitor đứng ở `[CLOUD] -> DANG KET NOI` | Sai IP, hoặc firewall chặn cổng 1883 (mục 3), hoặc ESP32 khác mạng với laptop. Thử `Test-NetConnection 192.168.1.20 -Port 1883` từ máy khác |
| `Server tu choi ket noi (ma 5)` liên tục | Device profile chưa bật provisioning hoặc sai key / secret (mục 4) |
| Máy online nhưng dashboard không thấy | Thiết bị chưa gán cho khách (Assign to customer), hoặc profile không phải `E8+` |
| OTA báo `FAILED: Wrong firmware...` | File `.bin` không phải project `e8_rtos` |
| OTA cài xong báo FAILED "did not start" | Version trên server khác `PROJECT_VER`, hoặc bản mới crash nên đã quay về bản cũ |
| Hai máy cùng tên cứ rớt mạng luân phiên | Hai thiết bị dùng chung một tên / token (ví dụ chạy `e8_sim.py` trùng tên máy thật) |
| `docker compose up` lỗi cổng 8080 / 1883 đã bị dùng | Tắt chương trình khác đang dùng cổng đó (một MQTT broker khác như Mosquitto…) |

## 12. Giới hạn hiện tại, cần anh giám sát góp ý

- **Chưa mã hóa (TLS 8883)**: hiện chạy MQTT thường trong mạng nội bộ. Lên server thật nên bật TLS.
- **Token** được tính từ tên máy và secret nằm trong firmware. Đủ cho demo, nhưng ai có firmware thì giả được máy khác. Bản thật nên đổi secret và dùng TLS, hoặc chứng chỉ X.509 riêng cho từng máy.
- **Giấy phép**: ThingsBoard CE 4.3 vẫn là Apache 2.0, còn được vá bảo mật tới 20/07/2027. Từ 4.4 (29/09/2026) đổi sang BSL: miễn phí thương mại tối đa 100 thiết bị và phải giữ logo ThingsBoard.
- Chỗ giờ `HH:MM:SS` trên mẫu cũ: đang hiểu là **thời gian còn lại của buổi tập**.
- Nút 👤 đang hiện thông tin khách + máy (mẫu cũ không rõ, sẽ sửa theo góp ý).
- MODE WIFI vẫn giữ nghĩa cũ: chỉ cần WiFi ONLINE, **chưa** bắt buộc phải kết nối được ThingsBoard.
