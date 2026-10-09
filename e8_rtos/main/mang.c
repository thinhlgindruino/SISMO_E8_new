/*
 * mang.c - WiFi STA / AP cấu hình (xem mang.h)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/ip4_addr.h"
#include "mang.h"
#include "web.h"

static const char *TAG = "MANG";

#define NVS_TEN      "mang"
#define NVS_SSID     "ssid"
#define NVS_MATKHAU  "pass"

#define SO_LAN_THU_NHANH   5          // 5 lần đầu thử lại sau 2 s
#define THU_LAI_NHANH_MS   2000
#define THU_LAI_CHAM_MS    15000      // sau đó cứ 15 s thử lại 1 lần (router bật lại là tự vào)

static portMUX_TYPE khoa = portMUX_INITIALIZER_UNLOCKED;
static mang_trang_thai_t tt = MANG_OFFLINE;
static char ten_ap[33];
static char ssid_luu[33];
static char ip_hien[16] = "";
static bool co_wifi_luu = false;
static bool dang_cau_hinh = false;
static int  lan_thu = 0;

static esp_netif_t *netif_sta = NULL;
static esp_netif_t *netif_ap  = NULL;
static esp_timer_handle_t hen_thu_lai = NULL;
static esp_timer_handle_t hen_reset = NULL;

static void dat_trang_thai(mang_trang_thai_t moi)
{
    portENTER_CRITICAL(&khoa);
    bool doi = (tt != moi);
    tt = moi;
    portEXIT_CRITICAL(&khoa);
    if (doi) ESP_LOGI(TAG, "Trang thai -> %s", mang_ten_trang_thai(moi));
}

mang_trang_thai_t mang_trang_thai(void)
{
    portENTER_CRITICAL(&khoa);
    mang_trang_thai_t t = tt;
    portEXIT_CRITICAL(&khoa);
    return t;
}

const char *mang_ten_trang_thai(mang_trang_thai_t t)
{
    switch (t) {
    case MANG_OFFLINE:      return "OFFLINE";
    case MANG_DANG_KET_NOI: return "DANG KET NOI";
    case MANG_ONLINE:       return "ONLINE";
    case MANG_CAU_HINH:     return "CONFIGURATION WIFI";
    default:                return "?";
    }
}

const char *mang_ten_ap(void) { return ten_ap; }

void mang_lay_thong_tin(char *ssid, size_t n_ssid, char *ip, size_t n_ip)
{
    portENTER_CRITICAL(&khoa);
    if (ssid) snprintf(ssid, n_ssid, "%s", ssid_luu);
    if (ip)   snprintf(ip, n_ip, "%s", ip_hien);
    portEXIT_CRITICAL(&khoa);
}

// ---------------- NVS ----------------
static void doc_wifi_luu(void)
{
    nvs_handle_t h;
    co_wifi_luu = false;
    ssid_luu[0] = '\0';
    if (nvs_open(NVS_TEN, NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof(ssid_luu);
    if (nvs_get_str(h, NVS_SSID, ssid_luu, &n) == ESP_OK && ssid_luu[0]) co_wifi_luu = true;
    nvs_close(h);
}

bool mang_luu_wifi(const char *ssid, const char *mat_khau)
{
    nvs_handle_t h;
    if (nvs_open(NVS_TEN, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_str(h, NVS_SSID, ssid) == ESP_OK &&
              nvs_set_str(h, NVS_MATKHAU, mat_khau) == ESP_OK &&
              nvs_commit(h) == ESP_OK;
    nvs_close(h);
    ESP_LOGI(TAG, "Luu WiFi \"%s\": %s", ssid, ok ? "OK" : "LOI");
    return ok;
}

void mang_xoa_wifi(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_TEN, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Da xoa WiFi da luu");
}

static void reset_ngay(void *arg) { (void)arg; esp_restart(); }

void mang_khoi_dong_lai_sau(int ms)
{
    if (!hen_reset) {
        const esp_timer_create_args_t a = { .callback = reset_ngay, .name = "mang_reset" };
        esp_timer_create(&a, &hen_reset);
    }
    esp_timer_stop(hen_reset);
    esp_timer_start_once(hen_reset, (uint64_t)ms * 1000);
}

// ---------------- Sự kiện WiFi ----------------
static void thu_ket_noi(void *arg) { (void)arg; if (!dang_cau_hinh) esp_wifi_connect(); }

static void su_kien(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (co_wifi_luu && !dang_cau_hinh) {
            dat_trang_thai(MANG_DANG_KET_NOI);
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (dang_cau_hinh) return;
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
        lan_thu++;
        portENTER_CRITICAL(&khoa);
        ip_hien[0] = '\0';
        portEXIT_CRITICAL(&khoa);
        ESP_LOGW(TAG, "Mat ket noi / khong vao duoc \"%s\" (ly do %d), lan %d", ssid_luu, e->reason, lan_thu);
        // Thử vài lần đầu vẫn coi là "đang kết nối", quá thì báo OFFLINE nhưng vẫn thử lại chậm
        dat_trang_thai(lan_thu < SO_LAN_THU_NHANH ? MANG_DANG_KET_NOI : MANG_OFFLINE);
        esp_timer_start_once(hen_thu_lai,
            (uint64_t)(lan_thu < SO_LAN_THU_NHANH ? THU_LAI_NHANH_MS : THU_LAI_CHAM_MS) * 1000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        portENTER_CRITICAL(&khoa);
        snprintf(ip_hien, sizeof(ip_hien), IPSTR, IP2STR(&e->ip_info.ip));
        portEXIT_CRITICAL(&khoa);
        lan_thu = 0;
        ESP_LOGI(TAG, "Da vao WiFi \"%s\", IP %s", ssid_luu, ip_hien);
        dat_trang_thai(MANG_ONLINE);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "Thiet bi " MACSTR " da vao AP", MAC2STR(e->mac));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "Thiet bi " MACSTR " da roi AP", MAC2STR(e->mac));
    }
}

// ---------------- Khởi động ----------------
void mang_khoi_dong(void)
{
    // NVS (nơi lưu WiFi). Bộ nhớ hỏng / đổi phiên bản thì xóa đi làm lại.
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    // Tên AP từ MAC (MAC "WiFi STA", là MAC mà lệnh esptool read_mac in ra)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(ten_ap, sizeof(ten_ap), "SISMO_E8_%02X%02X%02X", mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "MAC " MACSTR " -> ten AP cau hinh: %s", MAC2STR(mac), ten_ap);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    netif_sta = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   // tự quản lý NVS, không để WiFi tự lưu
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, su_kien, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, su_kien, NULL));

    const esp_timer_create_args_t a = { .callback = thu_ket_noi, .name = "mang_thu_lai" };
    ESP_ERROR_CHECK(esp_timer_create(&a, &hen_thu_lai));

    doc_wifi_luu();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (co_wifi_luu) {
        wifi_config_t w = {0};
        char mk[65] = "";
        nvs_handle_t h;
        if (nvs_open(NVS_TEN, NVS_READONLY, &h) == ESP_OK) {
            size_t n = sizeof(mk);
            nvs_get_str(h, NVS_MATKHAU, mk, &n);
            nvs_close(h);
        }
        strncpy((char *)w.sta.ssid, ssid_luu, sizeof(w.sta.ssid) - 1);
        strncpy((char *)w.sta.password, mk, sizeof(w.sta.password) - 1);
        w.sta.threshold.authmode = mk[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &w));
        ESP_LOGI(TAG, "Co WiFi da luu: \"%s\" -> dang ket noi", ssid_luu);
    } else {
        ESP_LOGI(TAG, "Chua co WiFi da luu -> OFFLINE");
    }
    dat_trang_thai(MANG_OFFLINE);
    ESP_ERROR_CHECK(esp_wifi_start());       // STA_START -> tự connect nếu có WiFi lưu
}

void mang_bat_cau_hinh(void)
{
    if (dang_cau_hinh) return;
    dang_cau_hinh = true;
    esp_timer_stop(hen_thu_lai);
    ESP_LOGI(TAG, "Chuyen sang che do cau hinh (AP %s)", ten_ap);

    esp_wifi_disconnect();
    esp_wifi_stop();

    if (!netif_ap) netif_ap = esp_netif_create_default_wifi_ap();

    // IP tĩnh 192.168.10.100 cho AP, DHCP cấp IP cho máy kết nối vào
    esp_netif_ip_info_t ip = {0};
    ip.ip.addr      = ipaddr_addr(MANG_AP_IP);
    ip.gw.addr      = ipaddr_addr(MANG_AP_IP);
    ip.netmask.addr = ipaddr_addr("255.255.255.0");
    esp_netif_dhcps_stop(netif_ap);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif_ap, &ip));
    esp_netif_dhcps_start(netif_ap);

    wifi_config_t w = {0};
    strncpy((char *)w.ap.ssid, ten_ap, sizeof(w.ap.ssid) - 1);
    w.ap.ssid_len = strlen(ten_ap);
    strncpy((char *)w.ap.password, MANG_AP_MAT_KHAU, sizeof(w.ap.password) - 1);
    w.ap.channel = MANG_AP_KENH;
    w.ap.max_connection = MANG_AP_SO_KET_NOI;
    w.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &w));
    ESP_ERROR_CHECK(esp_wifi_start());

    portENTER_CRITICAL(&khoa);
    snprintf(ip_hien, sizeof(ip_hien), "%s", MANG_AP_IP);
    portEXIT_CRITICAL(&khoa);

    web_bat();
    dat_trang_thai(MANG_CAU_HINH);
    ESP_LOGI(TAG, "AP \"%s\" mat khau \"%s\" -> mo http://%s", ten_ap, MANG_AP_MAT_KHAU, MANG_AP_IP);
}
