/*
 * ota.h - Cập nhật firmware qua mạng (OTA) + tự quay về bản cũ khi bản mới lỗi (rollback)
 *
 * Bảng phân vùng có 2 vùng app: ota_0, ota_1. Đang chạy ở vùng nào thì ghi firmware mới vào vùng kia,
 * ghi xong + kiểm tra hợp lệ mới đổi vùng khởi động -> đang ghi mà mất điện / lỗi thì bản cũ vẫn chạy.
 *
 * Rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE trong sdkconfig.defaults):
 *   - Firmware mới khởi động lần đầu ở trạng thái "chờ xác nhận" (PENDING_VERIFY).
 *   - Chạy ổn định đủ OTA_SELFTEST_MS (không treo / crash / watchdog) -> ota tự xác nhận (VALID).
 *   - Bị reset trước khi xác nhận (crash, watchdog, mất điện) -> bootloader tự quay về bản cũ.
 *
 * Nạp firmware: web cấu hình http://192.168.10.100 -> tab Upload -> chọn build/e8_rtos.bin.
 * Chỉ nhận đúng firmware của project này (so tên project trong file .bin).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define OTA_SELFTEST_MS           15000

typedef struct {
    char version[32];
    char project[32];
    char build_date[16];
    char build_time[16];
    char running[17];
    bool pending_verify;
    bool can_rollback;
    char other_version[32];
} ota_info_t;

void ota_start(void);
void ota_get_info(ota_info_t *info);

bool ota_upload_begin(size_t total_size, const char **err);
bool ota_upload_write(const void *data, size_t len, const char **err);
bool ota_upload_finish(const char **err);
void ota_upload_abort(void);

bool ota_rollback(const char **err);
