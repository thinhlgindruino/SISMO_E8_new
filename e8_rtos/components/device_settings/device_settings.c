/*
 * device_settings.c - Gắn phân vùng SPIFFS "storage" và quản lý /spiffs/settings.json
 *
 * - device_settings_start(): gắn SPIFFS vào /spiffs (lần đầu phân vùng trống thì tự format),
 *   rồi đọc settings.json; khóa nào chưa có / sai thì dùng giá trị mặc định.
 * - Mọi cài đặt nằm trong 1 bảng (SETTINGS): tên khóa JSON, giá trị nhỏ nhất / lớn nhất,
 *   mặc định, và tên hiển thị từng giá trị (dùng cho log và để lưu dạng chữ cho dễ đọc, vd.
 *   "control_mode": "local"); cài đặt dạng số (mode, ratio, phút) lưu dạng số.
 * - Giá trị trong RAM đọc / ghi có khóa (critical section) vì task session đọc liên tục còn
 *   task setting ghi. Mỗi lần ghi lưu lại cả file.
 * - storage_write_text(): ghi ra file tạm "<tên>.tmp" rồi đổi tên -> mất điện giữa chừng
 *   không làm hỏng file cũ. network dùng chung hàm này để ghi wifi.json.
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
#define CM8_V_MIN                 42
#define CM8_V_MAX                 79
#define CM8_BASE_MODE             3
#define CM8_BASE_RATIO            9
#define CM8_RATIO_STEP            3
#define CM8_V_LIMIT               180

typedef enum { S_CONTROL_MODE, S_ACCESS_MODE, S_CARD_TIMER, S_MOTOR_MODE, S_MOTOR_RATIO, S_MANUAL_MIN, S_LANGUAGE, S_COUNT } setting_id_t;

typedef struct {
    const char *key;
    int min, max, def;
    const char *names[2];
} setting_def_t;

static const setting_def_t SETTINGS[S_COUNT] = {
    [S_CONTROL_MODE] = { "control_mode",   0, 1, CONTROL_MODE_LOCAL,  { "wifi", "local" } },
    [S_ACCESS_MODE]  = { "access_mode",    0, 1, ACCESS_FREE,         { "libre-service", "badgeuse" } },
    [S_CARD_TIMER]   = { "card_timer",     0, 1, CARD_TIMER_BADGEUSE, { "badgeuse", "sismo" } },
    [S_MOTOR_MODE]   = { "motor_mode",     MOTOR_MODE_MIN,  MOTOR_MODE_MAX,  3, { NULL, NULL } },
    [S_MOTOR_RATIO]  = { "motor_ratio",    MOTOR_RATIO_MIN, MOTOR_RATIO_MAX, 9, { NULL, NULL } },
    [S_MANUAL_MIN]   = { "manual_minutes", 5, 90, 5, { NULL, NULL } },
    [S_LANGUAGE]     = { "language",       0, LANGUAGE_COUNT - 1, 0, { NULL, NULL } },
};

static bool fs_ok = false;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static int values[S_COUNT];

bool device_storage_ok(void) { return fs_ok; }

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

static bool valid(setting_id_t id, int v)
{
    if (id == S_MANUAL_MIN) return v == 5 || v == 90;
    return v >= SETTINGS[id].min && v <= SETTINGS[id].max;
}

bool motor_combo_ok(int mode, int ratio)
{
    int step = mode <= 3 ? 12 : (mode == 4 ? 15 : 20);
    int shift = step * (mode - CM8_BASE_MODE) + CM8_RATIO_STEP * (ratio - CM8_BASE_RATIO);
    return CM8_V_MIN + shift >= 0 && CM8_V_MAX + shift <= CM8_V_LIMIT;
}

static int get(setting_id_t id)
{
    portENTER_CRITICAL(&lock);
    int v = values[id];
    portEXIT_CRITICAL(&lock);
    return v;
}

static void load(void)
{
    for (int i = 0; i < S_COUNT; i++) values[i] = SETTINGS[i].def;
    FILE *f = fopen(FILE_SETTINGS, "r");
    if (!f) { ESP_LOGI(TAG, "Chua co %s -> dung mac dinh", FILE_SETTINGS); return; }
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *j = cJSON_Parse(buf);
    if (!j) { ESP_LOGW(TAG, "%s hong -> dung mac dinh", FILE_SETTINGS); return; }
    for (int i = 0; i < S_COUNT; i++) {
        const cJSON *it = cJSON_GetObjectItemCaseSensitive(j, SETTINGS[i].key);
        int v = SETTINGS[i].def;
        if (cJSON_IsString(it) && SETTINGS[i].names[0]) {
            for (int k = 0; k < 2; k++)
                if (strcmp(it->valuestring, SETTINGS[i].names[k]) == 0) v = k;
        } else if (cJSON_IsNumber(it) && valid(i, it->valueint)) {
            v = it->valueint;
        }
        values[i] = v;
    }
    cJSON_Delete(j);
    if (!motor_combo_ok(values[S_MOTOR_MODE], values[S_MOTOR_RATIO])) {
        ESP_LOGW(TAG, "Mode %d + ratio %d khong hop le -> ratio %d", values[S_MOTOR_MODE], values[S_MOTOR_RATIO], SETTINGS[S_MOTOR_RATIO].def);
        values[S_MOTOR_RATIO] = SETTINGS[S_MOTOR_RATIO].def;
    }
}

static bool save(void)
{
    cJSON *j = cJSON_CreateObject();
    for (int i = 0; i < S_COUNT; i++) {
        int v = get(i);
        if (SETTINGS[i].names[0]) cJSON_AddStringToObject(j, SETTINGS[i].key, SETTINGS[i].names[v]);
        else                      cJSON_AddNumberToObject(j, SETTINGS[i].key, v);
    }
    char *txt = cJSON_Print(j);
    cJSON_Delete(j);
    if (!txt) return false;
    bool ok = storage_write_text(FILE_SETTINGS, txt);
    cJSON_free(txt);
    return ok;
}

static bool set(setting_id_t id, int v)
{
    if (!valid(id, v)) return false;
    if (id == S_MOTOR_MODE  && !motor_combo_ok(v, get(S_MOTOR_RATIO))) return false;
    if (id == S_MOTOR_RATIO && !motor_combo_ok(get(S_MOTOR_MODE), v)) return false;
    portENTER_CRITICAL(&lock);
    values[id] = v;
    portEXIT_CRITICAL(&lock);
    bool ok = save();
    if (SETTINGS[id].names[0]) ESP_LOGI(TAG, "Luu %s = %s: %s", SETTINGS[id].key, SETTINGS[id].names[v], ok ? "OK" : "LOI");
    else                       ESP_LOGI(TAG, "Luu %s = %d: %s", SETTINGS[id].key, v, ok ? "OK" : "LOI");
    return ok;
}

void device_settings_start(void)
{
    for (int i = 0; i < S_COUNT; i++) values[i] = SETTINGS[i].def;
    const esp_vfs_spiffs_conf_t c = {
        .base_path = STORAGE_BASE, .partition_label = "storage",
        .max_files = 4, .format_if_mount_failed = true,
    };
    esp_err_t e = esp_vfs_spiffs_register(&c);
    fs_ok = (e == ESP_OK);
    if (!fs_ok) { ESP_LOGE(TAG, "Khong mo duoc SPIFFS (%s) -> khong luu duoc cai dat", esp_err_to_name(e)); return; }
    load();
    ESP_LOGI(TAG, "Cai dat: %s | %s | timer %s | mode %d | ratio %d | manual %d phut | %s",
             control_mode_name(get(S_CONTROL_MODE)), access_mode_name(get(S_ACCESS_MODE)),
             card_timer_name(get(S_CARD_TIMER)), get(S_MOTOR_MODE), get(S_MOTOR_RATIO), get(S_MANUAL_MIN),
             language_name(get(S_LANGUAGE)));
}

const char *control_mode_name(control_mode_t m) { return m == CONTROL_MODE_WIFI ? "MODE WIFI" : "MODE LOCAL"; }
const char *access_mode_name(access_mode_t m)   { return m == ACCESS_CARD ? "Acces badgeuse" : "Acces libre-service"; }
const char *card_timer_name(card_timer_t t)     { return t == CARD_TIMER_SISMO ? "Sismo" : "Badgeuse"; }

control_mode_t device_get_control_mode(void)          { return (control_mode_t)get(S_CONTROL_MODE); }
bool           device_set_control_mode(control_mode_t m) { return set(S_CONTROL_MODE, m); }
access_mode_t  device_get_access_mode(void)           { return (access_mode_t)get(S_ACCESS_MODE); }
bool           device_set_access_mode(access_mode_t m)   { return set(S_ACCESS_MODE, m); }
card_timer_t   device_get_card_timer(void)            { return (card_timer_t)get(S_CARD_TIMER); }
bool           device_set_card_timer(card_timer_t t)     { return set(S_CARD_TIMER, t); }
int            device_get_motor_mode(void)            { return get(S_MOTOR_MODE); }
bool           device_set_motor_mode(int mode)        { return set(S_MOTOR_MODE, mode); }
int            device_get_motor_ratio(void)           { return get(S_MOTOR_RATIO); }
bool           device_set_motor_ratio(int ratio)      { return set(S_MOTOR_RATIO, ratio); }
int            device_get_manual_minutes(void)        { return get(S_MANUAL_MIN); }
bool           device_set_manual_minutes(int minutes) { return set(S_MANUAL_MIN, minutes); }
int            device_get_language(void)              { return get(S_LANGUAGE); }
bool           device_set_language(int lang)          { return set(S_LANGUAGE, lang); }

const char *language_name(int lang)
{
    static const char *NAMES[LANGUAGE_COUNT] = { "Francais", "English", "Espanol", "Italiano", "Deutsch", "Portugues" };
    return (lang >= 0 && lang < LANGUAGE_COUNT) ? NAMES[lang] : "?";
}
