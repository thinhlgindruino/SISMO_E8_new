/*
 * cloud.h - Kết nối E8+ với server ThingsBoard qua MQTT (task "cloud", core 0)
 *
 * Chỉ chạy khi đã cài địa chỉ server (web cấu hình, tab Server) và WiFi ONLINE. Không module nào phải gọi
 * cloud: task tự đọc trạng thái màn hình / buổi tập / CM8 / cài đặt mỗi CLOUD_LOOP_MS rồi báo khi có thay đổi.
 *
 * Đăng nhập: tên thiết bị = tên AP (SISMO_E8_XXXXXX), token = 20 ký tự đầu SHA-256(CLOUD_PROVISION_SECRET ":"
 * tên) -> không cần lưu token. Server từ chối token (máy mới) -> kết nối lại với username "provision", gửi
 * /provision/request (key + secret của device profile "E8+", token ở trên) -> server tạo thiết bị -> đăng nhập lại.
 *
 * Gửi lên server:
 *   client attributes  model, firmware, device_name, mac, ip, ssid, network (WiFi / LAN), cài đặt (access_mode, motor_mode, card_timer,
 *                      language, motor_ratio, control_mode, manual_minutes), remote_lock, sessions_total
 *                      -> lúc kết nối và mỗi khi cài đặt đổi (trên màn hình hay từ xa)
 *   telemetry status   state (IDLE / COUNTDOWN / RUN / PAUSE), kind (manual / duree / programme), remaining_s,
 *                      intensity, program, level, page, cm8, card, fault (0 / 2 / 3 / 4 = ERR.0x), rssi, uptime_s,
 *                      heap -> khi đổi (tối đa 1 lần / s; thời gian còn lại 5 s / lần) và CLOUD_HEARTBEAT_MS một lần
 *   telemetry event    session_start / session_end (bài, mức, thời gian) / error (error_code ERR.02..04)
 *   OTA                current_fw_title / current_fw_version, fw_state, fw_progress, fw_error
 *
 * RPC từ dashboard (v1/devices/me/rpc/request/+), trả lời {"ok": true / false, "error": ...}:
 *   getStatus          trạng thái + cài đặt
 *   play / pause / stop  điều khiển buổi tập (bị chặn khi remote_lock); play ở trang chính -> mở trang Manuel
 *                      rồi chạy; vẫn phải qua mọi điều kiện an toàn (thẻ, MODE WIFI, CM8) của task session
 *   setRemoteLock      {"locked": true / false}
 *   setSettings        {"access_mode", "motor_mode", "card_timer", "language", "motor_ratio"} (gửi khóa nào đổi
 *                      khóa đó; mode / ratio không đổi được khi động cơ đang chạy; cặp tràn số bị từ chối)
 *   reboot             chỉ khi máy đang rảnh
 *
 * Cập nhật firmware (OTA ThingsBoard): khách chọn phiên bản trên dashboard -> server gán gói OTA cho thiết bị
 * -> shared attributes fw_title / fw_version / fw_size / fw_checksum (SHA256) -> máy kéo từng khúc
 * CLOUD_FW_CHUNK byte qua v2/fw/request/{id}/chunk/{n}, ghi bằng component ota, kiểm SHA-256, chờ máy rảnh
 * rồi khởi động lại. Bản mới chạy ổn định (ota tự xác nhận) -> lần kết nối sau báo UPDATED; bị quay về bản cũ
 * -> báo FAILED và không tự tải lại gói đó (trừ khi khách chọn lại).
 */
#pragma once
#include <stdbool.h>

#define CLOUD_PROVISION_KEY       "e8plus-provision-key"
#define CLOUD_PROVISION_SECRET    "e8plus-provision-secret"
#define CLOUD_FW_TITLE            "E8+"
#define CLOUD_MODEL               "E8+"
#define CLOUD_LOOP_MS             200
#define CLOUD_HEARTBEAT_MS        30000
#define CLOUD_FW_CHUNK            4096

typedef enum {
    CLOUD_OFF = 0,
    CLOUD_WAIT_NETWORK,
    CLOUD_CONNECTING,
    CLOUD_PROVISIONING,
    CLOUD_ONLINE,
    CLOUD_RETRY_LATER,
} cloud_state_t;

void          cloud_start(void);
cloud_state_t cloud_get_state(void);
const char   *cloud_state_name(cloud_state_t s);
