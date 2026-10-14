/*
 * ota.c - Cập nhật firmware qua mạng + rollback (xem ota.h)
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "ota.h"

static const char *TAG = "OTA";

// ---------------- Thông tin firmware ----------------
void ota_get_info(ota_info_t *info)
{
    memset(info, 0, sizeof(*info));
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(info->version, sizeof(info->version), "%s", app->version);
    snprintf(info->project, sizeof(info->project), "%s", app->project_name);
    snprintf(info->build_date, sizeof(info->build_date), "%s", app->date);
    snprintf(info->build_time, sizeof(info->build_time), "%s", app->time);

    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run) {
        snprintf(info->running, sizeof(info->running), "%s", run->label);
        esp_ota_img_states_t st;
        if (esp_ota_get_state_partition(run, &st) == ESP_OK) info->pending_verify = (st == ESP_OTA_IMG_PENDING_VERIFY);
    }

    info->can_rollback = esp_ota_check_rollback_is_possible();
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t d;
    if (other && esp_ota_get_partition_description(other, &d) == ESP_OK)
        snprintf(info->other_version, sizeof(info->other_version), "%s", d.version);
}

// ---------------- Tự xác nhận firmware mới ----------------
static void confirm_app(void *arg)
{
    (void)arg;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        ESP_LOGI(TAG, "Firmware moi chay on dinh %d s -> da xac nhan (khong quay ve ban cu nua)",
                 OTA_SELFTEST_MS / 1000);
    else
        ESP_LOGE(TAG, "Khong xac nhan duoc firmware moi");
}

void ota_start(void)
{
    ota_info_t i;
    ota_get_info(&i);
    ESP_LOGI(TAG, "Firmware %s v%s (%s %s), dang chay o vung %s%s",
             i.project, i.version, i.build_date, i.build_time, i.running,
             i.other_version[0] ? "" : " | vung con lai: trong");
    if (i.other_version[0]) ESP_LOGI(TAG, "Vung con lai co firmware v%s", i.other_version);

    // Vừa bị quay về bản cũ? (firmware mới đã hỏng trước khi kịp xác nhận)
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    if (bad) ESP_LOGW(TAG, "Firmware o vung %s bi loi truoc khi xac nhan -> da quay ve ban dang chay", bad->label);

    if (i.pending_verify) {
        ESP_LOGW(TAG, "Firmware MOI, dang chay thu: on dinh %d s se tu xac nhan; bi reset truoc do -> quay ve ban cu",
                 OTA_SELFTEST_MS / 1000);
        static esp_timer_handle_t t;
        const esp_timer_create_args_t a = { .callback = confirm_app, .name = "ota_confirm" };
        if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, (uint64_t)OTA_SELFTEST_MS * 1000);
    }
}

// ---------------- Nạp firmware ----------------
// Giữ lại phần đầu file đến hết esp_app_desc_t để kiểm tra trước khi ghi gì vào flash
#define HEAD_NEED (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

static esp_ota_handle_t handle;
static const esp_partition_t *target;
static bool     active = false, head_checked = false;
static uint8_t  head[HEAD_NEED];
static size_t   head_len = 0, written = 0, expected = 0;

bool ota_upload_begin(size_t total_size, const char **err)
{
    if (active) ota_upload_abort();
    target = esp_ota_get_next_update_partition(NULL);
    if (!target) { *err = "No OTA partition."; return false; }
    if (total_size < HEAD_NEED || total_size > target->size) { *err = "Invalid file size."; return false; }

    esp_err_t e = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (e == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
        *err = "The current firmware is still being verified. Please wait 15 s and try again.";
        return false;
    }
    if (e != ESP_OK) { *err = "Cannot start update."; return false; }

    active = true; head_checked = false; head_len = 0; written = 0; expected = total_size;
    ESP_LOGI(TAG, "Bat dau nhan firmware %u byte -> vung %s", (unsigned)total_size, target->label);
    return true;
}

static bool check_head(const char **err)
{
    const esp_image_header_t *h = (const esp_image_header_t *)head;
    if (h->magic != ESP_IMAGE_HEADER_MAGIC) { *err = "Not an ESP32 firmware file (.bin from the build folder)."; return false; }
    const esp_app_desc_t *nd = (const esp_app_desc_t *)(head + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (nd->magic_word != ESP_APP_DESC_MAGIC_WORD) { *err = "Firmware file has no app description."; return false; }
    const esp_app_desc_t *cur = esp_app_get_description();
    if (strncmp(nd->project_name, cur->project_name, sizeof(nd->project_name)) != 0) {
        static char msg[128];
        snprintf(msg, sizeof(msg), "Wrong firmware: project \"%.32s\" (expected \"%.32s\").", nd->project_name, cur->project_name);
        *err = msg;
        return false;
    }
    ESP_LOGI(TAG, "File firmware hop le: %s v%s (%s %s)", nd->project_name, nd->version, nd->date, nd->time);
    return true;
}

bool ota_upload_write(const void *data, size_t len, const char **err)
{
    if (!active) { *err = "Update not started."; return false; }
    const uint8_t *p = (const uint8_t *)data;

    if (!head_checked) {                                    // gom đủ phần đầu file rồi mới kiểm tra
        size_t k = HEAD_NEED - head_len;
        if (k > len) k = len;
        memcpy(head + head_len, p, k);
        head_len += k; p += k; len -= k;
        if (head_len < HEAD_NEED) return true;
        if (!check_head(err)) { ota_upload_abort(); return false; }
        head_checked = true;
        if (esp_ota_write(handle, head, head_len) != ESP_OK) { *err = "Write failed."; ota_upload_abort(); return false; }
        written += head_len;
    }
    if (len) {
        if (esp_ota_write(handle, p, len) != ESP_OK) { *err = "Write failed."; ota_upload_abort(); return false; }
        written += len;
    }
    return true;
}

bool ota_upload_finish(const char **err)
{
    if (!active || !head_checked || written != expected) { *err = "Incomplete file."; ota_upload_abort(); return false; }
    active = false;
    esp_err_t e = esp_ota_end(handle);                      // kiểm tra checksum / SHA-256 toàn bộ ảnh
    if (e != ESP_OK) { *err = "Invalid firmware file (checksum)."; return false; }
    if (esp_ota_set_boot_partition(target) != ESP_OK) { *err = "Cannot set boot partition."; return false; }
    ESP_LOGI(TAG, "Nap xong %u byte vao %s -> khoi dong lai se chay firmware moi", (unsigned)written, target->label);
    return true;
}

void ota_upload_abort(void)
{
    if (active) esp_ota_abort(handle);
    active = false;
}

// ---------------- Quay về bản trước (bấm nút trên web) ----------------
bool ota_rollback(const char **err)
{
    if (!esp_ota_check_rollback_is_possible()) { *err = "No previous firmware to roll back to."; return false; }
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t d;
    if (!other || esp_ota_get_partition_description(other, &d) != ESP_OK) { *err = "No previous firmware."; return false; }
    if (esp_ota_set_boot_partition(other) != ESP_OK) { *err = "Cannot set boot partition."; return false; }
    ESP_LOGW(TAG, "Quay ve firmware v%s o vung %s", d.version, other->label);
    return true;
}
