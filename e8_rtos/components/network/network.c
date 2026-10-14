/*
 * network.c - WiFi STA / AP cấu hình (xem network.h)
 *
 * Lưu WiFi: file JSON /spiffs/wifi.json trên phân vùng "storage" (SPIFFS) của flash ESP32:
 *     {"ssid":"NCTD-11","password":"..."}
 *
 * Chế độ cấu hình chạy AP + STA cùng lúc: đang ONLINE thì bật AP vẫn giữ kết nối WiFi,
 * tắt AP là về lại ONLINE ngay.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "lwip/ip4_addr.h"
#include "network.h"
#include "web.h"

static const char *TAG = "MANG";

#define FS_BASE        "/spiffs"
#define FILE_WIFI     FS_BASE "/wifi.json"
#define FILE_TMP      FS_BASE "/wifi.tmp"

#define FAST_RETRY_COUNT   5          // 5 lần đầu thử lại sau 2 s
#define FAST_RETRY_MS   2000
#define SLOW_RETRY_MS    15000      // sau đó cứ 15 s thử lại 1 lần (router bật lại là tự vào)

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static net_state_t status = NET_OFFLINE;
static char ap_name[33];
static char saved_ssid[33];
static char saved_pass[65];
static char ip_sta[16] = "";          // IP khi đang vào WiFi (chế độ thường)
static bool has_saved_wifi = false;
static bool sta_has_ip = false;        // STA đang có IP (vẫn giữ trong lúc bật AP cấu hình)
static bool config_active = false;
static bool fs_ok = false;
static int  retry_count = 0;

static esp_netif_t *netif_sta = NULL;
static esp_netif_t *netif_ap  = NULL;
static esp_timer_handle_t retry_timer = NULL;
static esp_timer_handle_t restart_timer = NULL;
static esp_timer_handle_t finish_timer = NULL;
static void (*before_restart_hook)(void) = NULL;

static void set_state(net_state_t next)
{
    portENTER_CRITICAL(&lock);
    bool changed = (status != next);
    status = next;
    portEXIT_CRITICAL(&lock);
    if (changed) ESP_LOGI(TAG, "Trang thai -> %s", net_state_name(next));
}

net_state_t net_get_state(void)
{
    portENTER_CRITICAL(&lock);
    net_state_t t = status;
    portEXIT_CRITICAL(&lock);
    return t;
}

const char *net_state_name(net_state_t t)
{
    switch (t) {
    case NET_OFFLINE:      return "OFFLINE";
    case NET_CONNECTING: return "DANG KET NOI";
    case NET_ONLINE:       return "ONLINE";
    case NET_CONFIG:     return "CONFIGURATION WIFI";
    default:                return "?";
    }
}

const char *net_ap_name(void) { return ap_name; }

void net_get_info(char *ssid, size_t n_ssid, char *ip, size_t n_ip)
{
    portENTER_CRITICAL(&lock);
    if (ssid) snprintf(ssid, n_ssid, "%s", saved_ssid);
    if (ip)   snprintf(ip, n_ip, "%s", config_active ? NET_AP_IP : ip_sta);
    portEXIT_CRITICAL(&lock);
}

// Trạng thái "thường" (không tính chế độ cấu hình): ONLINE / đang kết nối / OFFLINE
static net_state_t normal_state(void)
{
    if (sta_has_ip) return NET_ONLINE;
    if (has_saved_wifi && retry_count < FAST_RETRY_COUNT) return NET_CONNECTING;
    return NET_OFFLINE;
}

// ---------------- File JSON lưu WiFi ----------------
static void mount_fs(void)
{
    const esp_vfs_spiffs_conf_t c = {
        .base_path = FS_BASE, .partition_label = "storage",
        .max_files = 4, .format_if_mount_failed = true,     // lần đầu: tự format phân vùng trống
    };
    esp_err_t e = esp_vfs_spiffs_register(&c);
    fs_ok = (e == ESP_OK);
    if (!fs_ok) ESP_LOGE(TAG, "Khong mo duoc SPIFFS (%s) -> khong luu duoc WiFi", esp_err_to_name(e));
}

// Đọc wifi.json vào saved_ssid / saved_pass. Trả về true nếu có SSID.
static bool read_wifi_file(void)
{
    saved_ssid[0] = saved_pass[0] = '\0';
    if (!fs_ok) return false;
    FILE *f = fopen(FILE_WIFI, "r");
    if (!f) return false;
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    cJSON *j = cJSON_Parse(buf);
    if (!j) { ESP_LOGW(TAG, "%s hong (khong phai JSON) -> bo qua", FILE_WIFI); return false; }
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(j, "ssid");
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(j, "password");
    if (cJSON_IsString(s)) snprintf(saved_ssid, sizeof(saved_ssid), "%s", s->valuestring);
    if (cJSON_IsString(p)) snprintf(saved_pass, sizeof(saved_pass), "%s", p->valuestring);
    cJSON_Delete(j);
    return saved_ssid[0] != '\0';
}

static bool write_wifi_file(const char *ssid, const char *pass)
{
    if (!fs_ok) return false;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "ssid", ssid);
    cJSON_AddStringToObject(j, "password", pass);
    char *txt = cJSON_Print(j);                 // có xuống dòng, thụt lề cho dễ đọc
    cJSON_Delete(j);
    if (!txt) return false;

    // Ghi ra file tạm rồi đổi tên: mất điện giữa chừng không làm hỏng file cũ
    bool ok = false;
    FILE *f = fopen(FILE_TMP, "w");
    if (f) {
        ok = fputs(txt, f) >= 0;
        ok = (fclose(f) == 0) && ok;
    }
    cJSON_free(txt);
    if (ok) {
        remove(FILE_WIFI);
        ok = (rename(FILE_TMP, FILE_WIFI) == 0);
    }
    return ok;
}

// Bản cũ lưu WiFi trong NVS -> chuyển sang wifi.json 1 lần rồi xóa khỏi NVS
static void migrate_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("mang", NVS_READWRITE, &h) != ESP_OK) return;
    char s[33] = "", p[65] = "";
    size_t ns = sizeof(s), np = sizeof(p);
    if (nvs_get_str(h, "ssid", s, &ns) == ESP_OK && s[0]) {
        nvs_get_str(h, "pass", p, &np);
        if (write_wifi_file(s, p)) {
            ESP_LOGI(TAG, "Da chuyen WiFi \"%s\" tu NVS (ban cu) sang %s", s, FILE_WIFI);
            nvs_erase_all(h);
            nvs_commit(h);
        }
    }
    nvs_close(h);
}

bool net_save_wifi(const char *ssid, const char *pass)
{
    bool ok = write_wifi_file(ssid, pass);
    ESP_LOGI(TAG, "Luu WiFi \"%s\" vao %s: %s", ssid, FILE_WIFI, ok ? "OK" : "LOI");
    return ok;
}

void net_clear_wifi(void)
{
    if (fs_ok) remove(FILE_WIFI);
    ESP_LOGI(TAG, "Da xoa %s", FILE_WIFI);
}

int net_read_json(char *buf, size_t n, bool mask_password)
{
    buf[0] = '\0';
    if (!fs_ok) return -1;
    FILE *f = fopen(FILE_WIFI, "r");
    if (!f) return 0;
    size_t k = fread(buf, 1, n - 1, f);
    fclose(f);
    buf[k] = '\0';
    if (mask_password) {
        cJSON *j = cJSON_Parse(buf);
        if (j) {
            cJSON *p = cJSON_GetObjectItemCaseSensitive(j, "password");
            if (cJSON_IsString(p) && p->valuestring[0]) cJSON_SetValuestring(p, "********");
            char *txt = cJSON_Print(j);
            if (txt) { snprintf(buf, n, "%s", txt); cJSON_free(txt); }
            cJSON_Delete(j);
        }
    }
    return (int)strlen(buf);
}

// ---------------- Khởi động lại ----------------
void net_set_before_restart_hook(void (*hook)(void)) { before_restart_hook = hook; }

static void restart_now(void *arg) { (void)arg; esp_restart(); }

void net_restart_after(int ms)
{
    if (!restart_timer) {
        const esp_timer_create_args_t a = { .callback = restart_now, .name = "net_restart" };
        esp_timer_create(&a, &restart_timer);
    }
    esp_timer_stop(restart_timer);
    esp_timer_start_once(restart_timer, (uint64_t)ms * 1000);
}

static void stop_ap_and_restart(void *arg)
{
    (void)arg;
    if (before_restart_hook) before_restart_hook();   // vd. đưa màn hình về trang 0
    esp_wifi_stop();                          // tắt AP trước, rồi khởi động lại để vào WiFi mới
    esp_restart();
}

void net_finish_config_and_restart(int ms)
{
    if (!finish_timer) {
        const esp_timer_create_args_t a = { .callback = stop_ap_and_restart, .name = "net_finish" };
        esp_timer_create(&a, &finish_timer);
    }
    esp_timer_stop(finish_timer);
    esp_timer_start_once(finish_timer, (uint64_t)ms * 1000);
}

// ---------------- Sự kiện WiFi ----------------
// Trong lúc cấu hình KHÔNG tự thử kết nối lại: mỗi lần thử STA phải quét kênh,
// làm điện thoại đang vào AP bị rớt.
static void retry_connect(void *arg) { (void)arg; if (!config_active && has_saved_wifi) esp_wifi_connect(); }

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (has_saved_wifi && !config_active) {
            set_state(NET_CONNECTING);
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        bool had_ip = sta_has_ip;
        sta_has_ip = false;
        portENTER_CRITICAL(&lock);
        ip_sta[0] = '\0';
        portEXIT_CRITICAL(&lock);
        if (config_active || !has_saved_wifi) return;
        if (had_ip) retry_count = 0;            // vừa rớt mạng: thử lại nhanh từ đầu
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
        retry_count++;
        ESP_LOGW(TAG, "Mat ket noi / khong vao duoc \"%s\" (ly do %d), lan %d", saved_ssid, e->reason, retry_count);
        // Thử vài lần đầu vẫn coi là "đang kết nối", quá thì báo OFFLINE nhưng vẫn thử lại chậm
        set_state(normal_state());
        esp_timer_stop(retry_timer);
        esp_timer_start_once(retry_timer,
            (uint64_t)(retry_count < FAST_RETRY_COUNT ? FAST_RETRY_MS : SLOW_RETRY_MS) * 1000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        portENTER_CRITICAL(&lock);
        snprintf(ip_sta, sizeof(ip_sta), IPSTR, IP2STR(&e->ip_info.ip));
        portEXIT_CRITICAL(&lock);
        sta_has_ip = true;
        retry_count = 0;
        ESP_LOGI(TAG, "Da vao WiFi \"%s\", IP %s", saved_ssid, ip_sta);
        if (!config_active) set_state(NET_ONLINE);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "Thiet bi " MACSTR " da vao AP", MAC2STR(e->mac));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "Thiet bi " MACSTR " da roi AP", MAC2STR(e->mac));
    }
}

// ---------------- Khởi động ----------------
void net_start(void)
{
    // NVS: driver WiFi cần (hiệu chỉnh RF). Bộ nhớ hỏng / đổi phiên bản thì xóa đi làm lại.
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    mount_fs();
    if (fs_ok) {
        struct stat st;
        if (stat(FILE_WIFI, &st) != 0) migrate_from_nvs();
    }

    // Tên AP từ MAC (MAC "WiFi STA", là MAC mà lệnh esptool read_mac in ra)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(ap_name, sizeof(ap_name), "SISMO_E8_%02X%02X%02X", mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "MAC " MACSTR " -> ten AP cau hinh: %s", MAC2STR(mac), ap_name);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    netif_sta = esp_netif_create_default_wifi_sta();
    netif_ap  = esp_netif_create_default_wifi_ap();

    // IP tĩnh 192.168.10.100 cho AP, DHCP cấp IP cho máy kết nối vào
    esp_netif_ip_info_t ip = {0};
    ip.ip.addr      = ipaddr_addr(NET_AP_IP);
    ip.gw.addr      = ipaddr_addr(NET_AP_IP);
    ip.netmask.addr = ipaddr_addr("255.255.255.0");
    esp_netif_dhcps_stop(netif_ap);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif_ap, &ip));
    esp_netif_dhcps_start(netif_ap);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   // chỉ lưu ở wifi.json, driver không tự lưu
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    const esp_timer_create_args_t a = { .callback = retry_connect, .name = "net_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&a, &retry_timer));

    has_saved_wifi = read_wifi_file();
    {
        char j[256];
        if (net_read_json(j, sizeof(j), true) > 0) ESP_LOGI(TAG, "Noi dung %s:\n%s", FILE_WIFI, j);
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (has_saved_wifi) {
        wifi_config_t w = {0};
        strncpy((char *)w.sta.ssid, saved_ssid, sizeof(w.sta.ssid) - 1);
        strncpy((char *)w.sta.password, saved_pass, sizeof(w.sta.password) - 1);
        w.sta.threshold.authmode = saved_pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
        // Không ESP_ERROR_CHECK: mật khẩu sai định dạng (vd. < 8 ký tự) chỉ làm kết nối thất bại
        esp_err_t ec = esp_wifi_set_config(WIFI_IF_STA, &w);
        if (ec != ESP_OK) {
            ESP_LOGW(TAG, "WiFi da luu \"%s\" khong hop le (%s) -> OFFLINE", saved_ssid, esp_err_to_name(ec));
            has_saved_wifi = false;
        } else {
            ESP_LOGI(TAG, "Co WiFi da luu trong %s: \"%s\" -> dang ket noi", FILE_WIFI, saved_ssid);
        }
    } else {
        ESP_LOGI(TAG, "Chua co WiFi da luu (%s) -> OFFLINE", FILE_WIFI);
    }
    set_state(NET_OFFLINE);
    ESP_ERROR_CHECK(esp_wifi_start());       // STA_START -> tự connect nếu có WiFi lưu
}

// ---------------- Chế độ cấu hình ----------------
void net_config_start(void)
{
    if (config_active) return;
    config_active = true;
    esp_timer_stop(retry_timer);
    ESP_LOGI(TAG, "Bat che do cau hinh (AP %s)%s", ap_name,
             sta_has_ip ? " - van giu ket noi WiFi hien tai" : "");
    if (!sta_has_ip) esp_wifi_disconnect();   // đang thử kết nối dở -> dừng, tránh quét kênh làm rớt AP

    wifi_config_t w = {0};
    strncpy((char *)w.ap.ssid, ap_name, sizeof(w.ap.ssid) - 1);
    w.ap.ssid_len = strlen(ap_name);
    strncpy((char *)w.ap.password, NET_AP_PASSWORD, sizeof(w.ap.password) - 1);
    w.ap.channel = NET_AP_CHANNEL;             // đang vào WiFi thì AP tự theo kênh của router
    w.ap.max_connection = NET_AP_MAX_CLIENTS;
    w.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &w));

    web_start();
    set_state(NET_CONFIG);
    ESP_LOGI(TAG, "AP \"%s\" mat khau \"%s\" -> mo http://%s", ap_name, NET_AP_PASSWORD, NET_AP_IP);
}

void net_config_stop(void)
{
    if (!config_active) return;
    web_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);        // tắt AP -> điện thoại không còn thấy SISMO_E8_...
    config_active = false;
    retry_count = 0;
    net_state_t t = normal_state();
    ESP_LOGI(TAG, "Tat che do cau hinh (tat AP) -> ve trang thai hien tai: %s", net_state_name(t));
    set_state(t);
    if (!sta_has_ip && has_saved_wifi) esp_wifi_connect();   // chưa vào mạng -> thử lại
}
