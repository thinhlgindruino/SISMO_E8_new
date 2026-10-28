/*
 * cloud.c - Task "cloud": đăng nhập / tự đăng ký ThingsBoard, báo trạng thái, nhận RPC, OTA (xem cloud.h)
 *
 * - Sự kiện MQTT (cloud_link) được chép vào hàng đợi; mọi xử lý chạy trong task cloud (core 0, ưu tiên thấp)
 *   -> task điều khiển (session, motor, hmi) không bao giờ phải chờ mạng.
 * - Trạng thái kết nối: OFF (chưa cài server) -> WAIT_NETWORK (chưa ONLINE) -> CONNECTING (token) ->
 *   ONLINE. Bị từ chối token -> PROVISIONING (username "provision") -> CONNECTING lại; vẫn bị từ chối / hết giờ
 *   -> RETRY_LATER (RETRY_LATER_MS). Đổi server trên web -> đóng kết nối, làm lại từ đầu.
 * - Trạng thái buổi tập đọc từ màn hình: trang Programmes / Perso (6 / 14) lấy từ page_program; trang Manuel /
 *   Durée (10 / 11): 0x1001 = 1 là RUN, = 0 mà thời gian còn lại nhỏ hơn lúc vào trang là PAUSE, còn lại IDLE.
 * - Điều khiển từ xa = làm giống người bấm: trang 6 / 14 gửi nút Play / Stop cho page_program; trang 10 / 11 ghi
 *   0x1001 (Play / Pause) hoặc làm như nút Stop của trang (0x1000 = 0, 0x1001 = 0, về trang trước).
 * - cloud.json (SPIFFS): sessions_total, gói OTA đang chờ khởi động lại (ota_target / ota_checksum), gói OTA đã
 *   lỗi (failed_checksum) để không tải lại mãi, gói OTA đã cài xong lần gần nhất (done_checksum).
 * - Gói đang gán trên ThingsBoard = gói đã cài xong (done_checksum) mà máy đang chạy bản khác -> máy đã được nạp tay
 *   (USB / web) sau lần OTA đó -> không tự tải lại gói cũ đè lên; khách chọn gói khác (hoặc chọn lại lúc máy đang
 *   online) thì vẫn cập nhật như thường.
 * - Buổi tập chỉ được tính (event session_start / session_end, sessions_total) khi đồng hồ đã thật sự đếm xuống ít
 *   nhất 1 s lúc đang RUN; bấm ▶ mà bị chặn (ERR.01, thẻ, CM8) rồi thoát ra thì không tính.
 * - Đổi mạng (LAN <-> WiFi, đổi IP) khi chưa ONLINE -> kết nối lại ngay, không chờ esp-mqtt hết hạn thử lại.
 *   "network" / "ip" = mạng thật sự dùng để tới server (net_link_to: server cùng dải IP với WiFi thì là WiFi dù LAN
 *   đang cắm).
 * - Mỗi lần đổi trạng thái báo network (net_set_server_online): trang 74700 chỉ hiện ONLINE khi cloud ONLINE.
 * - OTA: mỗi khúc chờ trả lời FW_CHUNK_TIMEOUT_MS, gửi lại tối đa FW_CHUNK_TRIES lần; mất kết nối giữa chừng
 *   -> kết nối lại thì xin tiếp khúc đang dở.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include "app_config.h"
#include "logger.h"
#include "hmi.h"
#include "network.h"
#include "device_settings.h"
#include "motor_modbus.h"
#include "ota.h"
#include "page_session.h"
#include "page_program.h"
#include "page_setting.h"
#include "cloud_link.h"
#include "cloud.h"

#define FILE_CLOUD                STORAGE_BASE "/cloud.json"
#define TOKEN_LEN                 20
#define QUEUE_LEN                 16
#define STATUS_MIN_GAP_MS         1000
#define PROVISION_TIMEOUT_MS      15000
#define RETRY_LATER_MS            60000
#define FW_CHUNK_TIMEOUT_MS       15000
#define FW_CHUNK_TRIES            4
#define FW_BEGIN_RETRY_MS         5000
#define REBOOT_DELAY_MS           1500
#define FW_STATE_GAP_MS           300
#define REMOTE_PLAY_DELAY_MS      800
#define REMAINING_REPORT_MS       5000

#define T_ATTR                    "v1/devices/me/attributes"
#define T_ATTR_REQ                "v1/devices/me/attributes/request/"
#define T_ATTR_RES                "v1/devices/me/attributes/response/"
#define T_TELEMETRY               "v1/devices/me/telemetry"
#define T_RPC_REQ                 "v1/devices/me/rpc/request/"
#define T_RPC_RES                 "v1/devices/me/rpc/response/"
#define T_FW_REQ                  "v2/fw/request/"
#define T_FW_RES                  "v2/fw/response/"
#define T_PROV_REQ                "/provision/request"
#define T_PROV_RES                "/provision/response"
#define FW_KEYS                   "fw_title,fw_version,fw_size,fw_checksum,fw_checksum_algorithm"

typedef struct {
    link_event_type_t type;
    int               code;
    char              topic[LINK_TOPIC_MAX];
    uint8_t          *data;
    int               len;
} queued_event_t;

typedef struct {
    program_state_t state;
    const char *kind;
    int         page;
    int         remaining_s;
    int         planned_s;
    int         intensity;
    int         program;
    const char *program_name;
    int         level;
    bool        cm8;
    bool        card;
    int         error;
} status_t;

typedef struct {
    int  access_mode, motor_mode, card_timer, language, motor_ratio, control_mode, manual_minutes;
    bool remote_lock;
} settings_t;

typedef enum { FW_NONE, FW_WAIT_IDLE, FW_DOWNLOADING, FW_DOWNLOADED, FW_WAIT_APPLY } fw_phase_t;

static const char *STATE_NAMES[] = { "IDLE", "COUNTDOWN", "RUN", "PAUSE" };

static QueueHandle_t queue;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static cloud_state_t cstate = CLOUD_OFF;
static char device_name[33];
static char token[TOKEN_LEN + 1];
static char host[SERVER_HOST_MAX + 1];
static int  port = 0;
static int64_t state_since = 0;
static bool provisioned_once = false;
static int  request_id = 0;
static int  fw_live_request = -1;

static int  sessions_total = 0;
static char ota_target[32] = "", ota_checksum[65] = "", failed_checksum[65] = "", done_checksum[65] = "";

static status_t   last_status;
static bool       status_valid = false;
static int64_t    last_status_ms = 0;
static settings_t last_settings;
static char       last_net[24] = "";
static bool       settings_valid = false;
static int        manual_page = -1, manual_start_s = 0;
static int        prev_error = 0;
static int64_t    pending_play_at = 0;

static struct {
    const char *kind;
    char        program[40];
    int         level;
    int         planned_s;
    int         last_remaining_s;
    int         start_remaining_s;
    bool        active;
    bool        confirmed;
} session;

static struct {
    fw_phase_t phase;
    char       title[32], version[32], checksum[65];
    int        size;
    int        req_id, chunk, received, tries, last_pct;
    int64_t    sent_ms, retry_at;
    mbedtls_sha256_context sha;
    bool       sha_active;
} fw;

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

static void set_state(cloud_state_t s)
{
    portENTER_CRITICAL(&state_lock);
    bool changed = cstate != s;
    cstate = s;
    portEXIT_CRITICAL(&state_lock);
    net_set_server_online(s == CLOUD_ONLINE);
    if (changed) {
        state_since = ms_now();
        log_send("   [CLOUD] -> %s", cloud_state_name(s));
    }
}

cloud_state_t cloud_get_state(void)
{
    portENTER_CRITICAL(&state_lock);
    cloud_state_t s = cstate;
    portEXIT_CRITICAL(&state_lock);
    return s;
}

const char *cloud_state_name(cloud_state_t s)
{
    switch (s) {
    case CLOUD_OFF:          return "OFF (chua cai server)";
    case CLOUD_WAIT_NETWORK: return "CHO MANG";
    case CLOUD_CONNECTING:   return "DANG KET NOI";
    case CLOUD_PROVISIONING: return "DANG DANG KY THIET BI";
    case CLOUD_ONLINE:       return "ONLINE";
    case CLOUD_RETRY_LATER:  return "THU LAI SAU";
    }
    return "?";
}

static void hex(const uint8_t *in, int n, char *out)
{
    static const char *H = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[2 * i] = H[in[i] >> 4]; out[2 * i + 1] = H[in[i] & 15]; }
    out[2 * n] = '\0';
}

static void make_token(void)
{
    char text[96];
    uint8_t d[32];
    char h[65];
    snprintf(text, sizeof(text), "%s:%s", CLOUD_PROVISION_SECRET, device_name);
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    mbedtls_sha256_update(&c, (const unsigned char *)text, strlen(text));
    mbedtls_sha256_finish(&c, d);
    mbedtls_sha256_free(&c);
    hex(d, 32, h);
    memcpy(token, h, TOKEN_LEN);
    token[TOKEN_LEN] = '\0';
}

static void load_file(void)
{
    FILE *f = fopen(FILE_CLOUD, "r");
    if (!f) return;
    char buf[400];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *j = cJSON_Parse(buf);
    if (!j) return;
    const cJSON *it;
    if (cJSON_IsNumber(it = cJSON_GetObjectItemCaseSensitive(j, "sessions_total"))) sessions_total = it->valueint;
    if (cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(j, "ota_target")))      snprintf(ota_target, sizeof(ota_target), "%s", it->valuestring);
    if (cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(j, "ota_checksum")))    snprintf(ota_checksum, sizeof(ota_checksum), "%s", it->valuestring);
    if (cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(j, "failed_checksum"))) snprintf(failed_checksum, sizeof(failed_checksum), "%s", it->valuestring);
    if (cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(j, "done_checksum")))   snprintf(done_checksum, sizeof(done_checksum), "%s", it->valuestring);
    cJSON_Delete(j);
}

static void save_file(void)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "sessions_total", sessions_total);
    cJSON_AddStringToObject(j, "ota_target", ota_target);
    cJSON_AddStringToObject(j, "ota_checksum", ota_checksum);
    cJSON_AddStringToObject(j, "failed_checksum", failed_checksum);
    cJSON_AddStringToObject(j, "done_checksum", done_checksum);
    char *txt = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (txt) { storage_write_text(FILE_CLOUD, txt); cJSON_free(txt); }
}

static void on_link_event(const link_event_t *e)
{
    queued_event_t q = { .type = e->type, .code = e->code, .data = NULL, .len = 0 };
    q.topic[0] = '\0';
    if (e->topic) snprintf(q.topic, sizeof(q.topic), "%s", e->topic);
    if (e->type == LINK_EV_MESSAGE) {
        q.data = malloc(e->len + 1);
        if (!q.data) return;
        memcpy(q.data, e->data, e->len);
        q.data[e->len] = '\0';
        q.len = e->len;
    }
    if (xQueueSend(queue, &q, 0) != pdTRUE) free(q.data);
}

static void send_json(const char *topic, cJSON *j, int qos)
{
    char *txt = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!txt) return;
    link_publish(topic, txt, strlen(txt), qos);
    cJSON_free(txt);
}

static void send_text(const char *topic, const char *txt, int qos) { link_publish(topic, txt, strlen(txt), qos); }

static void read_settings(settings_t *s)
{
    s->access_mode = device_get_access_mode();
    s->motor_mode = device_get_motor_mode();
    s->card_timer = device_get_card_timer();
    s->language = device_get_language();
    s->motor_ratio = device_get_motor_ratio();
    s->control_mode = device_get_control_mode();
    s->manual_minutes = device_get_manual_minutes();
    s->remote_lock = device_get_remote_lock();
}

static void add_settings(cJSON *j, const settings_t *s)
{
    cJSON_AddNumberToObject(j, "access_mode", s->access_mode);
    cJSON_AddNumberToObject(j, "motor_mode", s->motor_mode);
    cJSON_AddNumberToObject(j, "card_timer", s->card_timer);
    cJSON_AddNumberToObject(j, "language", s->language);
    cJSON_AddNumberToObject(j, "motor_ratio", s->motor_ratio);
    cJSON_AddNumberToObject(j, "control_mode", s->control_mode);
    cJSON_AddNumberToObject(j, "manual_minutes", s->manual_minutes);
    cJSON_AddBoolToObject(j, "remote_lock", s->remote_lock);
}

static bool is_program_page(int page) { return page == PAGE_PROGRAM_X || page == PAGE_PROGRAM; }
static bool is_manual_page(int page)  { return page == PAGE_MANUEL || page == PAGE_DUREE_SESSION; }

static void read_status(status_t *s)
{
    hmi_data_t d = hmi_get();
    session_status_t ss = page_session_get_status();
    motor_status_t t = motor_get_status();
    memset(s, 0, sizeof(*s));
    s->state = PROGRAM_IDLE;
    s->kind = "";
    s->program = -1;
    s->program_name = "";
    s->page = d.page;
    s->intensity = ss.intensity;
    s->cm8 = t.connected;
    s->card = device_get_access_mode() == ACCESS_FREE || (t.connected && t.card == 1);
    s->error = !t.connected ? 2 : (t.error >= 2 && t.error <= 4 ? t.error : 0);

    if (is_program_page(d.page)) {
        page_program_info_t p;
        page_program_get_info(&p);
        s->state = p.state;
        s->kind = "programme";
        s->remaining_s = p.remaining_s;
        s->planned_s = p.total_s;
        s->program = p.program;
        s->program_name = p.name ? p.name : "";
        s->level = p.level;
        manual_page = -1;
    } else if (is_manual_page(d.page) && d.has_vars) {
        int seconds = d.vars[VAR_SECONDS];
        bool run = d.vars[VAR_RUN] == 1;
        if (d.page != manual_page) { manual_page = d.page; manual_start_s = 0; }
        if (!run && seconds > manual_start_s) manual_start_s = seconds;
        s->kind = d.page == PAGE_MANUEL ? "manual" : "duree";
        s->remaining_s = seconds;
        s->planned_s = manual_start_s;
        s->state = run ? PROGRAM_RUN : (seconds > 0 && seconds < manual_start_s ? PROGRAM_PAUSE : PROGRAM_IDLE);
    } else {
        manual_page = -1;
    }
}

static bool busy(const status_t *s) { return s->state != PROGRAM_IDLE; }

static void publish_attributes(void)
{
    settings_t s;
    read_settings(&s);
    ota_info_t o;
    ota_get_info(&o);
    char ssid[33] = "", ip[16] = "";
    net_get_info(ssid, sizeof(ssid), NULL, 0);
    net_link_t link = net_link_to(host, ip, sizeof(ip));
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "model", CLOUD_MODEL);
    cJSON_AddStringToObject(j, "firmware", o.version);
    cJSON_AddStringToObject(j, "device_name", device_name);
    cJSON_AddStringToObject(j, "mac", net_mac_text());
    cJSON_AddStringToObject(j, "ip", ip);
    cJSON_AddStringToObject(j, "ssid", ssid);
    cJSON_AddStringToObject(j, "network", net_link_name(link));
    add_settings(j, &s);
    cJSON_AddNumberToObject(j, "sessions_total", sessions_total);
    send_json(T_ATTR, j, 1);
    snprintf(last_net, sizeof(last_net), "%s %s", net_link_name(link), ip);
    last_settings = s;
    settings_valid = true;
}

static void publish_status(const status_t *s)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "state", STATE_NAMES[s->state]);
    cJSON_AddStringToObject(j, "kind", s->kind);
    cJSON_AddNumberToObject(j, "remaining_s", s->remaining_s);
    cJSON_AddNumberToObject(j, "planned_s", s->planned_s);
    cJSON_AddNumberToObject(j, "intensity", s->intensity);
    cJSON_AddStringToObject(j, "program", s->program_name);
    cJSON_AddNumberToObject(j, "level", s->level);
    cJSON_AddNumberToObject(j, "page", s->page);
    cJSON_AddBoolToObject(j, "cm8", s->cm8);
    cJSON_AddBoolToObject(j, "card", s->card);
    cJSON_AddNumberToObject(j, "fault", s->error);
    cJSON_AddNumberToObject(j, "rssi", net_get_rssi());
    cJSON_AddNumberToObject(j, "uptime_s", (double)(ms_now() / 1000));
    cJSON_AddNumberToObject(j, "heap", esp_get_free_heap_size());
    send_json(T_TELEMETRY, j, 0);
    last_status = *s;
    status_valid = true;
    last_status_ms = ms_now();
}

static void send_fw_state(const char *st, const char *err)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "fw_state", st);
    if (err) cJSON_AddStringToObject(j, "fw_error", err);
    if (strcmp(st, "UPDATED") == 0) {
        ota_info_t o;
        ota_get_info(&o);
        cJSON_AddStringToObject(j, "current_fw_title", CLOUD_FW_TITLE);
        cJSON_AddStringToObject(j, "current_fw_version", o.version);
    }
    send_json(T_TELEMETRY, j, 1);
    log_send("   [CLOUD] OTA %s%s%s", st, err ? ": " : "", err ? err : "");
}

static void send_event_session(bool start)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "event", start ? "session_start" : "session_end");
    cJSON_AddStringToObject(j, "session_kind", session.kind);
    if (session.program[0]) {
        cJSON_AddStringToObject(j, "session_program", session.program);
        cJSON_AddNumberToObject(j, "session_level", session.level + 1);
    }
    cJSON_AddNumberToObject(j, "session_planned_s", session.planned_s);
    if (!start) {
        int done = session.planned_s - session.last_remaining_s;
        cJSON_AddNumberToObject(j, "session_done_s", done > 0 ? done : 0);
        cJSON_AddBoolToObject(j, "session_completed", session.last_remaining_s <= 1);
    }
    send_json(T_TELEMETRY, j, 1);
}

static void track_session(const status_t *s, bool online)
{
    bool running = s->state == PROGRAM_RUN || s->state == PROGRAM_COUNTDOWN;
    if (!session.active && running) {
        session.active = true;
        session.confirmed = false;
        session.kind = s->kind;
        snprintf(session.program, sizeof(session.program), "%s", s->program_name);
        session.level = s->level;
        session.planned_s = s->planned_s;
        session.last_remaining_s = s->remaining_s;
        session.start_remaining_s = s->remaining_s;
    } else if (session.active) {
        bool same = strcmp(s->kind, session.kind) == 0 && s->state != PROGRAM_IDLE;
        if (same) {
            session.last_remaining_s = s->remaining_s;
            if (!session.confirmed && s->state == PROGRAM_RUN && s->remaining_s < session.start_remaining_s) {
                session.confirmed = true;
                if (online) send_event_session(true);
                log_send("   [CLOUD] Bat dau buoi tap %s %s", session.kind, session.program);
            }
        } else {
            session.active = false;
            if (!session.confirmed) return;
            sessions_total++;
            save_file();
            if (online) { send_event_session(false); publish_attributes(); }
            log_send("   [CLOUD] Ket thuc buoi tap (con %d s) -> tong %d buoi", session.last_remaining_s, sessions_total);
        }
    }
}

static void track_error(const status_t *s, bool online)
{
    if (s->error == prev_error) return;
    if (s->error && online) {
        cJSON *j = cJSON_CreateObject();
        char code[8];
        snprintf(code, sizeof(code), "ERR.0%d", s->error);
        cJSON_AddStringToObject(j, "event", "error");
        cJSON_AddStringToObject(j, "error_code", code);
        send_json(T_TELEMETRY, j, 1);
    }
    prev_error = s->error;
}

static bool status_changed(const status_t *a, const status_t *b)
{
    return a->state != b->state || strcmp(a->kind, b->kind) != 0 || a->page != b->page ||
           a->intensity != b->intensity || a->program != b->program ||
           a->level != b->level || a->cm8 != b->cm8 || a->card != b->card || a->error != b->error;
}

static bool settings_changed(const settings_t *a, const settings_t *b) { return memcmp(a, b, sizeof(*a)) != 0; }

static void send_program_button(uint16_t value)
{
    hmi_event_t e = { .addr = ADDR_PROG_BUTTON, .value = value, .source = EV_SRC_REPORT };
    page_program_on_event(&e);
}

static const char *remote_play(void)
{
    status_t s;
    read_status(&s);
    if (is_program_page(s.page)) {
        if (s.state == PROGRAM_IDLE || s.state == PROGRAM_PAUSE) send_program_button(PROG_BTN_PLAY);
        return NULL;
    }
    if (is_manual_page(s.page)) {
        if (s.state != PROGRAM_RUN) hmi_write(ADDR_RUN, 1);
        return NULL;
    }
    if (s.page == PAGE_MAIN) {
        hmi_goto_page(PAGE_MANUEL);
        pending_play_at = ms_now() + REMOTE_PLAY_DELAY_MS;
        return NULL;
    }
    return "not_ready";
}

static const char *remote_pause(void)
{
    status_t s;
    read_status(&s);
    if (is_program_page(s.page) && (s.state == PROGRAM_RUN || s.state == PROGRAM_COUNTDOWN)) {
        send_program_button(PROG_BTN_PLAY);
        return NULL;
    }
    if (is_manual_page(s.page) && s.state == PROGRAM_RUN) {
        hmi_write(ADDR_RUN, 0);
        return NULL;
    }
    return "not_running";
}

static const char *remote_stop(void)
{
    status_t s;
    read_status(&s);
    pending_play_at = 0;
    if (is_program_page(s.page)) {
        if (s.state != PROGRAM_IDLE) send_program_button(PROG_BTN_STOP);
        return NULL;
    }
    if (is_manual_page(s.page)) {
        hmi_write(ADDR_INTENSITY, 0);
        hmi_write(ADDR_RUN, 0);
        hmi_goto_page(s.page == PAGE_MANUEL ? PAGE_MAIN : PAGE_DUREE);
        return NULL;
    }
    return NULL;
}

static int json_int(const cJSON *o, const char *key, int def)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsNumber(it)) return it->valueint;
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it) ? 1 : 0;
    if (cJSON_IsString(it) && it->valuestring[0]) return atoi(it->valuestring);
    return def;
}

static const char *apply_settings(const cJSON *p)
{
    if (!cJSON_IsObject(p)) return "bad_params";
    settings_t cur;
    read_settings(&cur);
    int mode = json_int(p, "motor_mode", cur.motor_mode);
    int ratio = json_int(p, "motor_ratio", cur.motor_ratio);
    int access = json_int(p, "access_mode", cur.access_mode);
    int timer = json_int(p, "card_timer", cur.card_timer);
    int lang = json_int(p, "language", cur.language);
    if (mode < MOTOR_MODE_MIN || mode > MOTOR_MODE_MAX || ratio < MOTOR_RATIO_MIN || ratio > MOTOR_RATIO_MAX ||
        access < 0 || access > 1 || timer < 0 || timer > 1 || lang < 0 || lang >= LANGUAGE_COUNT) return "out_of_range";
    bool motor_change = mode != cur.motor_mode || ratio != cur.motor_ratio;
    if (motor_change && page_session_get_status().mode == SESSION_RUN) return "motor_running";
    if (motor_change && !motor_combo_ok(mode, ratio)) return "bad_mode_ratio";
    bool ok = true;
    if (motor_change) {
        if (mode != cur.motor_mode && ratio != cur.motor_ratio && !motor_combo_ok(mode, cur.motor_ratio)) {
            ok &= device_set_motor_ratio(ratio);
            ok &= device_set_motor_mode(mode);
        } else {
            if (mode != cur.motor_mode)   ok &= device_set_motor_mode(mode);
            if (ratio != cur.motor_ratio) ok &= device_set_motor_ratio(ratio);
        }
    }
    if (access != cur.access_mode) ok &= device_set_access_mode((access_mode_t)access);
    if (timer != cur.card_timer)   ok &= device_set_card_timer((card_timer_t)timer);
    if (lang != cur.language)      ok &= device_set_language(lang);
    page_setting_refresh();
    log_send("   [CLOUD] Doi cai dat tu xa: %s | Mode %d | %s | %s | Ratio %d", access_mode_name(access), mode,
             card_timer_name(timer), language_name(lang), ratio);
    return ok ? NULL : "save_failed";
}

static void add_status(cJSON *j, const status_t *s)
{
    cJSON_AddStringToObject(j, "state", STATE_NAMES[s->state]);
    cJSON_AddStringToObject(j, "kind", s->kind);
    cJSON_AddNumberToObject(j, "remaining_s", s->remaining_s);
    cJSON_AddStringToObject(j, "program", s->program_name);
    cJSON_AddNumberToObject(j, "page", s->page);
    cJSON_AddNumberToObject(j, "fault", s->error);
}

static void handle_rpc(const char *id, const char *body)
{
    cJSON *req = cJSON_Parse(body);
    const cJSON *m = req ? cJSON_GetObjectItemCaseSensitive(req, "method") : NULL;
    const cJSON *params = req ? cJSON_GetObjectItemCaseSensitive(req, "params") : NULL;
    const char *method = cJSON_IsString(m) ? m->valuestring : "";
    const char *err = NULL;
    bool control = strcmp(method, "play") == 0 || strcmp(method, "pause") == 0 || strcmp(method, "stop") == 0 ||
                   strcmp(method, "setSettings") == 0 || strcmp(method, "reboot") == 0;
    bool reboot = false;

    if (control && device_get_remote_lock()) err = "locked";
    else if (strcmp(method, "getStatus") == 0) err = NULL;
    else if (strcmp(method, "play") == 0)      err = remote_play();
    else if (strcmp(method, "pause") == 0)     err = remote_pause();
    else if (strcmp(method, "stop") == 0)      err = remote_stop();
    else if (strcmp(method, "setSettings") == 0) err = apply_settings(params);
    else if (strcmp(method, "setRemoteLock") == 0) {
        bool locked = cJSON_IsBool(params) ? cJSON_IsTrue(params) : json_int(params, "locked", 0) != 0;
        if (!device_set_remote_lock(locked)) err = "save_failed";
        log_send("   [CLOUD] %s dieu khien tu xa", locked ? "KHOA" : "MO");
    } else if (strcmp(method, "reboot") == 0) {
        status_t s;
        read_status(&s);
        if (busy(&s)) err = "busy";
        else reboot = true;
    } else err = "unknown_method";

    status_t s;
    read_status(&s);
    settings_t st;
    read_settings(&st);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddBoolToObject(res, "ok", err == NULL);
    if (err) cJSON_AddStringToObject(res, "error", err);
    add_status(res, &s);
    add_settings(res, &st);
    char topic[64];
    snprintf(topic, sizeof(topic), T_RPC_RES "%s", id);
    send_json(topic, res, 1);
    log_send("   [CLOUD] RPC %s -> %s", method, err ? err : "OK");
    cJSON_Delete(req);
    if (settings_valid && settings_changed(&st, &last_settings)) publish_attributes();
    if (reboot) {
        log_send("   [CLOUD] Khoi dong lai theo lenh tu xa");
        net_finish_config_and_restart(REBOOT_DELAY_MS);
    }
}

static void fw_reset(void)
{
    if (fw.sha_active) { mbedtls_sha256_free(&fw.sha); fw.sha_active = false; }
    fw.phase = FW_NONE;
}

static void fw_fail(const char *why)
{
    if (fw.phase == FW_DOWNLOADING || fw.phase == FW_DOWNLOADED || fw.phase == FW_WAIT_APPLY) ota_upload_abort();
    snprintf(failed_checksum, sizeof(failed_checksum), "%s", fw.checksum);
    save_file();
    send_fw_state("FAILED", why);
    fw_reset();
}

static void fw_request_chunk(void)
{
    char topic[64], size[8];
    snprintf(topic, sizeof(topic), T_FW_REQ "%d/chunk/%d", fw.req_id, fw.chunk);
    snprintf(size, sizeof(size), "%d", CLOUD_FW_CHUNK);
    send_text(topic, size, 1);
    fw.sent_ms = ms_now();
}

static void fw_offer(const cJSON *a, bool live)
{
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(a, "fw_title");
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, "fw_version");
    const cJSON *sz = cJSON_GetObjectItemCaseSensitive(a, "fw_size");
    const cJSON *cs = cJSON_GetObjectItemCaseSensitive(a, "fw_checksum");
    const cJSON *al = cJSON_GetObjectItemCaseSensitive(a, "fw_checksum_algorithm");
    if (!cJSON_IsString(t) || !cJSON_IsString(v) || !cJSON_IsNumber(sz) || !cJSON_IsString(cs)) return;
    if (strcmp(t->valuestring, CLOUD_FW_TITLE) != 0) return;
    ota_info_t o;
    ota_get_info(&o);
    if (strcmp(v->valuestring, o.version) == 0) return;
    if (fw.phase != FW_NONE && strcmp(fw.checksum, cs->valuestring) == 0) return;
    if (!live && strcmp(failed_checksum, cs->valuestring) == 0) {
        log_send("   [CLOUD] Goi OTA %s da loi truoc do -> bo qua (chon lai tren dashboard de thu lai)", v->valuestring);
        return;
    }
    if (!live && strcmp(done_checksum, cs->valuestring) == 0) {
        log_send("   [CLOUD] Goi OTA %s da cai xong truoc do, may dang chay v%s (nap tay) -> khong nap lai ban cu",
                 v->valuestring, o.version);
        return;
    }
    if (fw.phase == FW_DOWNLOADING || fw.phase == FW_DOWNLOADED || fw.phase == FW_WAIT_APPLY) ota_upload_abort();
    fw_reset();
    snprintf(fw.title, sizeof(fw.title), "%s", t->valuestring);
    snprintf(fw.version, sizeof(fw.version), "%s", v->valuestring);
    snprintf(fw.checksum, sizeof(fw.checksum), "%s", cs->valuestring);
    fw.size = sz->valueint;
    if (cJSON_IsString(al) && strcmp(al->valuestring, "SHA256") != 0) {
        fw_fail("checksum algorithm must be SHA256");
        return;
    }
    fw.phase = FW_WAIT_IDLE;
    fw.retry_at = 0;
    log_send("   [CLOUD] Co firmware %s v%s (%d byte) -> tai khi may ranh", fw.title, fw.version, fw.size);
}

static void fw_begin(void)
{
    const char *err = NULL;
    if (!ota_upload_begin(fw.size, &err)) {
        if (strstr(err, "verified")) { fw.retry_at = ms_now() + FW_BEGIN_RETRY_MS; return; }
        fw.phase = FW_DOWNLOADING;
        fw_fail(err);
        return;
    }
    mbedtls_sha256_init(&fw.sha);
    mbedtls_sha256_starts(&fw.sha, 0);
    fw.sha_active = true;
    fw.phase = FW_DOWNLOADING;
    fw.req_id = (int)(ms_now() % 100000) + 1;
    fw.chunk = 0;
    fw.received = 0;
    fw.tries = 0;
    fw.last_pct = -1;
    send_fw_state("DOWNLOADING", NULL);
    fw_request_chunk();
}

static void fw_on_chunk(const char *topic, const uint8_t *data, int len)
{
    int id = -1, n = -1;
    if (sscanf(topic, T_FW_RES "%d/chunk/%d", &id, &n) != 2) return;
    if (fw.phase != FW_DOWNLOADING || id != fw.req_id || n != fw.chunk) return;
    if (len > 0) {
        const char *err = NULL;
        if (fw.received + len > fw.size) { fw_fail("firmware larger than fw_size"); return; }
        if (!ota_upload_write(data, len, &err)) { fw.phase = FW_NONE; fw_fail(err); return; }
        mbedtls_sha256_update(&fw.sha, data, len);
        fw.received += len;
        fw.chunk++;
        fw.tries = 0;
        int pct = fw.size ? (int)((int64_t)fw.received * 100 / fw.size) : 100;
        if (pct / 10 != fw.last_pct / 10) {
            fw.last_pct = pct;
            cJSON *j = cJSON_CreateObject();
            cJSON_AddNumberToObject(j, "fw_progress", pct);
            send_json(T_TELEMETRY, j, 0);
            log_send("   [CLOUD] OTA %d%% (%d / %d byte)", pct, fw.received, fw.size);
        }
    }
    if (fw.received < fw.size && len > 0) { fw_request_chunk(); return; }
    if (fw.received != fw.size) { fw_fail("incomplete download"); return; }
    send_fw_state("DOWNLOADED", NULL);
    fw.phase = FW_DOWNLOADED;
    fw.sent_ms = ms_now();
}

static void fw_verify(void)
{
    uint8_t d[32];
    char h[65];
    mbedtls_sha256_finish(&fw.sha, d);
    mbedtls_sha256_free(&fw.sha);
    fw.sha_active = false;
    hex(d, 32, h);
    if (strcasecmp(h, fw.checksum) != 0) { fw.phase = FW_DOWNLOADING; fw_fail("checksum mismatch"); return; }
    send_fw_state("VERIFIED", NULL);
    fw.phase = FW_WAIT_APPLY;
    fw.sent_ms = ms_now();
}

static void fw_apply(void)
{
    const char *err = NULL;
    send_fw_state("UPDATING", NULL);
    vTaskDelay(pdMS_TO_TICKS(FW_STATE_GAP_MS));
    if (!ota_upload_finish(&err)) { fw.phase = FW_NONE; fw_fail(err); return; }
    snprintf(ota_target, sizeof(ota_target), "%s", fw.version);
    snprintf(ota_checksum, sizeof(ota_checksum), "%s", fw.checksum);
    save_file();
    fw_reset();
    log_send("   [CLOUD] Nap xong firmware v%s -> khoi dong lai", ota_target);
    net_finish_config_and_restart(REBOOT_DELAY_MS);
}

static void fw_report_after_boot(void)
{
    if (!ota_target[0]) return;
    ota_info_t o;
    ota_get_info(&o);
    if (strcmp(o.version, ota_target) == 0) {
        snprintf(done_checksum, sizeof(done_checksum), "%s", ota_checksum);
        send_fw_state("UPDATED", NULL);
    } else {
        char why[128];
        snprintf(why, sizeof(why), "new firmware v%s did not start, still running v%s", ota_target, o.version);
        snprintf(failed_checksum, sizeof(failed_checksum), "%s", ota_checksum);
        send_fw_state("FAILED", why);
    }
    ota_target[0] = '\0';
    ota_checksum[0] = '\0';
    save_file();
}

static void fw_tick(const status_t *s)
{
    int64_t now = ms_now();
    if (fw.phase == FW_WAIT_IDLE && !busy(s) && net_get_state() != NET_CONFIG && now >= fw.retry_at) fw_begin();
    else if (fw.phase == FW_DOWNLOADING && now - fw.sent_ms >= FW_CHUNK_TIMEOUT_MS) {
        if (++fw.tries >= FW_CHUNK_TRIES) fw_fail("download timeout");
        else fw_request_chunk();
    } else if (fw.phase == FW_DOWNLOADED && now - fw.sent_ms >= FW_STATE_GAP_MS) fw_verify();
    else if (fw.phase == FW_WAIT_APPLY && !busy(s) && now - fw.sent_ms >= FW_STATE_GAP_MS) fw_apply();
}

static void request_fw_attributes(bool live)
{
    char topic[64];
    snprintf(topic, sizeof(topic), T_ATTR_REQ "%d", ++request_id);
    if (live) fw_live_request = request_id;
    send_text(topic, "{\"sharedKeys\":\"" FW_KEYS "\"}", 1);
}

static void on_online(void)
{
    link_subscribe(T_ATTR, 1);
    link_subscribe(T_ATTR_RES "+", 1);
    link_subscribe(T_RPC_REQ "+", 1);
    link_subscribe(T_FW_RES "+/chunk/+", 1);
    publish_attributes();
    ota_info_t o;
    ota_get_info(&o);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "current_fw_title", CLOUD_FW_TITLE);
    cJSON_AddStringToObject(j, "current_fw_version", o.version);
    send_json(T_TELEMETRY, j, 1);
    fw_report_after_boot();
    status_t s;
    read_status(&s);
    publish_status(&s);
    request_fw_attributes(false);
    if (fw.phase == FW_DOWNLOADING) fw_request_chunk();
}

static void handle_message(const queued_event_t *q)
{
    const char *t = q->topic;
    const char *body = (const char *)q->data;
    if (strncmp(t, T_FW_RES, strlen(T_FW_RES)) == 0) { fw_on_chunk(t, q->data, q->len); return; }
    if (strncmp(t, T_RPC_REQ, strlen(T_RPC_REQ)) == 0) { handle_rpc(t + strlen(T_RPC_REQ), body); return; }
    if (strcmp(t, T_ATTR) == 0 || strncmp(t, T_ATTR_RES, strlen(T_ATTR_RES)) == 0) {
        bool push = strcmp(t, T_ATTR) == 0;
        bool live = !push && atoi(t + strlen(T_ATTR_RES)) == fw_live_request;
        cJSON *j = cJSON_Parse(body);
        if (!j) return;
        const cJSON *shared = push ? j : cJSON_GetObjectItemCaseSensitive(j, "shared");
        const cJSON *deleted = cJSON_GetObjectItemCaseSensitive(j, "deleted");
        if (push && cJSON_IsObject(shared) && (cJSON_GetObjectItemCaseSensitive(shared, "fw_version") ||
                                               cJSON_GetObjectItemCaseSensitive(shared, "fw_checksum")))
            request_fw_attributes(true);
        else if (!push && cJSON_IsObject(shared)) fw_offer(shared, live);
        if (push && cJSON_IsArray(deleted) && fw.phase != FW_NONE) {
            const cJSON *k;
            cJSON_ArrayForEach(k, deleted) {
                if (cJSON_IsString(k) && strcmp(k->valuestring, "fw_version") == 0) {
                    if (fw.phase != FW_WAIT_IDLE) ota_upload_abort();
                    fw_reset();
                    log_send("   [CLOUD] Server bo gan firmware -> huy cap nhat");
                }
            }
        }
        cJSON_Delete(j);
    }
}

static void open_with_token(void)
{
    log_send("   [CLOUD] Ket noi %s:%d, thiet bi %s", host, port, device_name);
    link_open(host, port, token, device_name, on_link_event);
    set_state(CLOUD_CONNECTING);
}

static void open_provision(void)
{
    log_send("   [CLOUD] Server chua co thiet bi %s -> dang ky (provision)", device_name);
    link_open(host, port, "provision", device_name, on_link_event);
    set_state(CLOUD_PROVISIONING);
}

static void handle_link_event(const queued_event_t *q)
{
    cloud_state_t s = cloud_get_state();
    switch (q->type) {
    case LINK_EV_CONNECTED:
        if (s == CLOUD_PROVISIONING) {
            link_subscribe(T_PROV_RES, 1);
            cJSON *j = cJSON_CreateObject();
            cJSON_AddStringToObject(j, "deviceName", device_name);
            cJSON_AddStringToObject(j, "provisionDeviceKey", CLOUD_PROVISION_KEY);
            cJSON_AddStringToObject(j, "provisionDeviceSecret", CLOUD_PROVISION_SECRET);
            cJSON_AddStringToObject(j, "credentialsType", "ACCESS_TOKEN");
            cJSON_AddStringToObject(j, "token", token);
            send_json(T_PROV_REQ, j, 1);
        } else if (s == CLOUD_CONNECTING || s == CLOUD_ONLINE) {
            set_state(CLOUD_ONLINE);
            on_online();
        }
        break;
    case LINK_EV_DISCONNECTED:
        if (s == CLOUD_ONLINE) {
            log_send("   [CLOUD] Mat ket noi server -> tu ket noi lai");
            set_state(CLOUD_CONNECTING);
        }
        break;
    case LINK_EV_REFUSED:
        link_close();
        if (s == CLOUD_CONNECTING && !provisioned_once) open_provision();
        else {
            log_send("   [CLOUD] Server tu choi ket noi (ma %d) -> thu lai sau %d s", q->code, RETRY_LATER_MS / 1000);
            set_state(CLOUD_RETRY_LATER);
        }
        break;
    case LINK_EV_MESSAGE:
        if (s == CLOUD_PROVISIONING && strcmp(q->topic, T_PROV_RES) == 0) {
            cJSON *j = cJSON_Parse((const char *)q->data);
            const cJSON *st = j ? cJSON_GetObjectItemCaseSensitive(j, "status") : NULL;
            log_send("   [CLOUD] Dang ky: %s", cJSON_IsString(st) ? st->valuestring : "?");
            cJSON_Delete(j);
            link_close();
            provisioned_once = true;
            open_with_token();
        } else if (s == CLOUD_ONLINE) {
            handle_message(q);
        }
        break;
    }
}

static void connection_tick(void)
{
    char h[SERVER_HOST_MAX + 1];
    int p;
    device_get_server(h, sizeof(h), &p);
    cloud_state_t s = cloud_get_state();
    int64_t now = ms_now();
    if (strcmp(h, host) != 0 || p != port) {
        link_close();
        snprintf(host, sizeof(host), "%s", h);
        port = p;
        provisioned_once = false;
        set_state(host[0] ? CLOUD_WAIT_NETWORK : CLOUD_OFF);
        return;
    }
    if (!host[0]) { if (s != CLOUD_OFF) { link_close(); set_state(CLOUD_OFF); } return; }
    bool net_ok = net_get_state() == NET_ONLINE || net_get_state() == NET_CONFIG;
    if (!net_ok) {
        if (s != CLOUD_WAIT_NETWORK) { link_close(); set_state(CLOUD_WAIT_NETWORK); }
        return;
    }
    static char prev_net[24] = "";
    char ip[16] = "", net[24];
    snprintf(net, sizeof(net), "%s %s", net_link_name(net_link_to(host, ip, sizeof(ip))), ip);
    bool net_changed = strcmp(net, prev_net) != 0;
    snprintf(prev_net, sizeof(prev_net), "%s", net);
    if (s == CLOUD_OFF || s == CLOUD_WAIT_NETWORK) open_with_token();
    else if (net_changed && net_get_link() != NET_LINK_NONE && (s == CLOUD_CONNECTING || s == CLOUD_RETRY_LATER)) {
        log_send("   [CLOUD] Doi mang (%s) -> ket noi lai ngay", net);
        provisioned_once = false;
        open_with_token();
    }
    else if (s == CLOUD_PROVISIONING && now - state_since >= PROVISION_TIMEOUT_MS) {
        link_close();
        log_send("   [CLOUD] Dang ky khong tra loi -> thu lai sau %d s", RETRY_LATER_MS / 1000);
        set_state(CLOUD_RETRY_LATER);
    } else if (s == CLOUD_RETRY_LATER && now - state_since >= RETRY_LATER_MS) {
        provisioned_once = false;
        open_with_token();
    }
}

static void task_cloud(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    while (1) {
        queued_event_t q;
        if (xQueueReceive(queue, &q, pdMS_TO_TICKS(CLOUD_LOOP_MS)) == pdTRUE) {
            handle_link_event(&q);
            free(q.data);
            while (xQueueReceive(queue, &q, 0) == pdTRUE) {
                handle_link_event(&q);
                free(q.data);
                esp_task_wdt_reset();
            }
        }
        esp_task_wdt_reset();
        connection_tick();

        bool online = cloud_get_state() == CLOUD_ONLINE;
        status_t s;
        read_status(&s);
        track_session(&s, online);
        track_error(&s, online);

        if (pending_play_at && ms_now() >= pending_play_at) {
            pending_play_at = 0;
            if (hmi_get().page == PAGE_MANUEL) hmi_write(ADDR_RUN, 1);
        }

        if (!online) continue;
        int64_t now = ms_now();
        int64_t gap = now - last_status_ms;
        if ((!status_valid || status_changed(&s, &last_status)) && gap >= STATUS_MIN_GAP_MS) publish_status(&s);
        else if (s.remaining_s != last_status.remaining_s && gap >= REMAINING_REPORT_MS) publish_status(&s);
        else if (gap >= CLOUD_HEARTBEAT_MS) publish_status(&s);
        settings_t st;
        read_settings(&st);
        char ip[16] = "", net[24];
        snprintf(net, sizeof(net), "%s %s", net_link_name(net_link_to(host, ip, sizeof(ip))), ip);
        if (!settings_valid || settings_changed(&st, &last_settings) || strcmp(net, last_net) != 0) publish_attributes();
        fw_tick(&s);
    }
}

void cloud_start(void)
{
    snprintf(device_name, sizeof(device_name), "%s", net_ap_name());
    make_token();
    load_file();
    queue = xQueueCreate(QUEUE_LEN, sizeof(queued_event_t));
    xTaskCreatePinnedToCore(task_cloud, "cloud", STACK_CLOUD, NULL, PRIO_CLOUD, NULL, NET_CORE);
}
