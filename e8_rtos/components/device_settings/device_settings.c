/*
 * device_settings.c - Gắn phân vùng SPIFFS "storage" và quản lý /spiffs/settings.json
 *
 * - device_settings_start(): gắn SPIFFS vào /spiffs (lần đầu phân vùng trống thì tự format),
 *   rồi đọc settings.json; chưa có file hoặc file hỏng thì dùng giá trị mặc định.
 * - storage_write_text(): ghi ra file tạm "<tên>.tmp" rồi đổi tên -> mất điện giữa chừng
 *   không làm hỏng file cũ. network dùng chung hàm này để ghi wifi.json.
 * - control_mode: lưu dạng chuỗi "wifi" / "local"; đọc / ghi giá trị trong RAM có khóa
 *   (critical section) vì task session đọc liên tục còn task setting ghi.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "cJSON.h"
#include "device_settings.h"

static const char *TAG = "SETTINGS";

#define FILE_SETTINGS             STORAGE_BASE "/settings.json"

static bool fs_ok = false;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static control_mode_t control_mode = CONTROL_MODE_DEFAULT;

bool device_storage_ok(void) { return fs_ok; }

const char *control_mode_name(control_mode_t m) { return m == CONTROL_MODE_WIFI ? "MODE WIFI" : "MODE LOCAL"; }

bool storage_write_text(const char *path, const char *text)
{
    if (!fs_ok) return false;
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    bool ok = fputs(text, f) >= 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok) return false;
    remove(path);
    return rename(tmp, path) == 0;
}

static void load(void)
{
    FILE *f = fopen(FILE_SETTINGS, "r");
    if (!f) { ESP_LOGI(TAG, "Chua co %s -> dung mac dinh: %s", FILE_SETTINGS, control_mode_name(control_mode)); return; }
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *j = cJSON_Parse(buf);
    if (!j) { ESP_LOGW(TAG, "%s hong -> dung mac dinh", FILE_SETTINGS); return; }
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(j, "control_mode");
    if (cJSON_IsString(m)) control_mode = (strcmp(m->valuestring, "wifi") == 0) ? CONTROL_MODE_WIFI : CONTROL_MODE_LOCAL;
    cJSON_Delete(j);
    ESP_LOGI(TAG, "Doc %s: %s", FILE_SETTINGS, control_mode_name(control_mode));
}

static bool save(void)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "control_mode", device_get_control_mode() == CONTROL_MODE_WIFI ? "wifi" : "local");
    char *txt = cJSON_Print(j);
    cJSON_Delete(j);
    if (!txt) return false;
    bool ok = storage_write_text(FILE_SETTINGS, txt);
    cJSON_free(txt);
    return ok;
}

void device_settings_start(void)
{
    const esp_vfs_spiffs_conf_t c = {
        .base_path = STORAGE_BASE, .partition_label = "storage",
        .max_files = 4, .format_if_mount_failed = true,
    };
    esp_err_t e = esp_vfs_spiffs_register(&c);
    fs_ok = (e == ESP_OK);
    if (!fs_ok) { ESP_LOGE(TAG, "Khong mo duoc SPIFFS (%s) -> khong luu duoc cai dat", esp_err_to_name(e)); return; }
    load();
}

control_mode_t device_get_control_mode(void)
{
    portENTER_CRITICAL(&lock);
    control_mode_t m = control_mode;
    portEXIT_CRITICAL(&lock);
    return m;
}

bool device_set_control_mode(control_mode_t m)
{
    portENTER_CRITICAL(&lock);
    control_mode = m;
    portEXIT_CRITICAL(&lock);
    bool ok = save();
    ESP_LOGI(TAG, "Luu %s = %s: %s", FILE_SETTINGS, control_mode_name(m), ok ? "OK" : "LOI");
    return ok;
}
