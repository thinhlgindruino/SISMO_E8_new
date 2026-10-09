/*
 * web.c - Web server cấu hình tại http://192.168.10.100 (xem web.h)
 *
 *   GET  /          trang Settings (WIFI SETUP: SSID + Password + Submit)  - giống E8 cũ
 *   POST /settings  lưu WiFi vào /spiffs/wifi.json -> tắt AP -> màn hình về trang 0 -> ESP32 khởi động lại
 *   GET  /wifi.json xem file WiFi đã lưu (mật khẩu che ****)
 *   GET  /upload    trang Upload (chọn file firmware .bin)
 *   POST /update    nhận file .bin, ghi vào vùng OTA còn lại -> khởi động lại bằng firmware mới
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "network.h"
#include "web.h"

static const char *TAG = "WEB";
static httpd_handle_t server = NULL;

// ---------------- Phần HTML chung (đầu trang + thanh menu) ----------------
static const char *HTML_HEAD =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Sismo E8</title><style>"
    "body{margin:0;padding:8px;background:#eeeeee;font-family:'Times New Roman',Times,serif;color:#000}"
    ".tieude{background:#bfb8b8;border-bottom:1px solid #00e5ff;text-align:center;padding:20px 0}"
    ".tieude h1{margin:0;color:#00e5ff;font-size:26px;font-weight:bold;letter-spacing:1px}"
    ".menu{background:#bfb8b8;text-align:right;padding:0;height:42px}"
    ".menu a{display:inline-block;color:#fff;text-decoration:none;font-size:17px;"
    "padding:10px 16px;line-height:22px}"
    ".menu a.chon{background:#13b597;border-radius:4px}"
    ".noidung{text-align:center}"
    ".noidung h2{font-weight:normal;font-size:32px;margin:22px 0 10px}"
    ".noidung h3{font-size:26px;margin:10px 0 6px}"
    "form{display:inline-block;text-align:left}"
    "label{display:block;font-weight:bold;font-style:italic;font-size:16px;margin-top:4px}"
    "input[type=text],input[type=password]{width:170px;margin:4px 0 6px}"
    ".nut{text-align:center;margin-top:50px}"
    ".thongbao{font-size:20px;margin-top:30px}"
    "</style></head><body>"
    "<div class='tieude'><h1>E8 PARAMETERS SETTINGS</h1></div>";

static const char *HTML_MENU_SETTINGS =
    "<div class='menu'><a class='chon' href='/'>Settings</a><a href='/upload'>Upload</a></div>";
static const char *HTML_MENU_UPLOAD =
    "<div class='menu'><a href='/'>Settings</a><a class='chon' href='/upload'>Upload</a></div>";

static const char *HTML_SETTINGS =
    "<div class='noidung'><h2>DEVICE SETTINGS</h2><h3>WIFI SETUP</h3>"
    "<form method='post' action='/settings'>"
    "<label>SSID:</label><input type='text' name='ssid' maxlength='32' required autofocus>"
    "<label>Password:</label><input type='password' name='password' maxlength='64'>"
    "<div class='nut'><input type='submit' value='Submit'></div>"
    "</form></div>";

static const char *HTML_UPLOAD =
    "<div class='noidung'><h2>DEVICE SETTINGS</h2><h3>FIRMWARE UPDATE</h3>"
    "<form onsubmit='return guiFile()'>"
    "<input type='file' id='f' accept='.bin'>"
    "<div class='nut'><input type='submit' value='Upload'></div>"
    "</form><p id='tt' class='thongbao'></p></div>"
    "<script>"
    "function guiFile(){var f=document.getElementById('f').files[0];var t=document.getElementById('tt');"
    "if(!f){t.innerHTML='Please choose a .bin file';return false;}"
    "var x=new XMLHttpRequest();x.open('POST','/update');"
    "x.upload.onprogress=function(e){if(e.lengthComputable)t.innerHTML='Uploading... '+Math.round(e.loaded*100/e.total)+'%';};"
    "x.onload=function(){t.innerHTML=x.responseText;};"
    "x.onerror=function(){t.innerHTML='Upload failed';};"
    "x.setRequestHeader('Content-Type','application/octet-stream');x.send(f);return false;}"
    "</script>";

static const char *HTML_TAIL = "</body></html>";

static esp_err_t send_page(httpd_req_t *req, const char *menu, const char *content)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, HTML_HEAD);
    httpd_resp_sendstr_chunk(req, menu);
    httpd_resp_sendstr_chunk(req, content);
    httpd_resp_sendstr_chunk(req, HTML_TAIL);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t send_message(httpd_req_t *req, const char *text)
{
    char content[384];
    snprintf(content, sizeof(content),
             "<div class='noidung'><h2>DEVICE SETTINGS</h2><p class='thongbao'>%s</p></div>", text);
    return send_page(req, HTML_MENU_SETTINGS, content);
}

// ---------------- Tab Settings ----------------
static esp_err_t handle_root(httpd_req_t *req) { return send_page(req, HTML_MENU_SETTINGS, HTML_SETTINGS); }

// Giải mã dữ liệu form: '+' -> ' ', "%XX" -> ký tự
static void url_decode(char *s)
{
    char *d = s;
    while (*s) {
        if (*s == '+') { *d++ = ' '; s++; }
        else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char h[3] = {s[1], s[2], 0};
            *d++ = (char)strtol(h, NULL, 16);
            s += 3;
        } else *d++ = *s++;
    }
    *d = '\0';
}

// Chuỗi an toàn để chèn vào HTML (tránh tên WiFi có ký tự < > & ")
static void html_escape(const char *src, char *dst, size_t n)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 6 < n; i++) {
        switch (src[i]) {
        case '<': j += snprintf(dst + j, n - j, "&lt;");   break;
        case '>': j += snprintf(dst + j, n - j, "&gt;");   break;
        case '&': j += snprintf(dst + j, n - j, "&amp;");  break;
        case '"': j += snprintf(dst + j, n - j, "&quot;"); break;
        default:  dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static esp_err_t handle_save_wifi(httpd_req_t *req)
{
    char body[400];
    int total = 0;
    if (req->content_len >= sizeof(body)) return send_message(req, "Data too long.");
    while (total < (int)req->content_len) {
        int r = httpd_req_recv(req, body + total, req->content_len - total);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return ESP_FAIL;
        total += r;
    }
    body[total] = '\0';

    char ssid[100] = "", pass[200] = "";
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body, "password", pass, sizeof(pass));
    url_decode(ssid);
    url_decode(pass);

    // Submit = kết thúc cấu hình, dù mật khẩu đúng hay sai: lưu lại, TẮT AP, khởi động lại.
    // Sai mật khẩu -> sau khi khởi động lại không vào được mạng -> OFFLINE.
    // (Chỉ chặn SSID rỗng; ô SSID trên trang đã 'required' nên trình duyệt không cho gửi rỗng.)
    ssid[32] = '\0';                        // SSID tối đa 32 ký tự, mật khẩu tối đa 64
    pass[64] = '\0';
    if (ssid[0] == '\0') return send_message(req, "Please enter the SSID.<br><a href='/'>Back</a>");

    bool ok = net_save_wifi(ssid, pass);
    char ssid_html[200], msg[320];
    html_escape(ssid, ssid_html, sizeof(ssid_html));
    if (ok) snprintf(msg, sizeof(msg), "Saved. The device will restart and connect to <b>%s</b>.", ssid_html);
    else    snprintf(msg, sizeof(msg), "Save failed. The device will restart.");
    send_message(req, msg);
    net_finish_config_and_restart(1000);  // đợi trang kịp gửi xong -> tắt AP -> reset
    return ESP_OK;
}

// ---------------- Tab Upload (cập nhật firmware) ----------------
// GET /wifi.json : xem file WiFi đã lưu (mật khẩu che ****)
static esp_err_t handle_view_json(httpd_req_t *req)
{
    char buf[256];
    int n = net_read_json(buf, sizeof(buf), true);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, n > 0 ? buf : "{}");
}

static esp_err_t handle_upload_page(httpd_req_t *req) { return send_page(req, HTML_MENU_UPLOAD, HTML_UPLOAD); }

static esp_err_t handle_update(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) { httpd_resp_sendstr(req, "No OTA partition."); return ESP_OK; }
    if (req->content_len == 0 || req->content_len > part->size) {
        httpd_resp_sendstr(req, "Invalid file size.");
        return ESP_OK;
    }

    esp_ota_handle_t ota;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        httpd_resp_sendstr(req, "Cannot start update.");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Nhan firmware %u byte -> %s", (unsigned)req->content_len, part->label);

    char buf[1024];
    size_t remaining = req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { esp_ota_abort(ota); return ESP_FAIL; }
        if (esp_ota_write(ota, buf, r) != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_sendstr(req, "Write failed.");
            return ESP_OK;
        }
        remaining -= r;
    }
    if (esp_ota_end(ota) != ESP_OK) { httpd_resp_sendstr(req, "Invalid firmware file."); return ESP_OK; }
    if (esp_ota_set_boot_partition(part) != ESP_OK) { httpd_resp_sendstr(req, "Cannot set boot partition."); return ESP_OK; }

    httpd_resp_sendstr(req, "Update OK. The device is restarting...");
    ESP_LOGI(TAG, "Cap nhat firmware xong -> khoi dong lai");
    net_restart_after(2000);
    return ESP_OK;
}

void web_start(void)
{
    if (server) return;
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.stack_size = 8192;
    c.core_id = 0;                          // chạy ở core 0, không giành core 1 của phần điều khiển
    c.lru_purge_enable = true;
    if (httpd_start(&server, &c) != ESP_OK) { ESP_LOGE(TAG, "Khong mo duoc web server"); return; }

    const httpd_uri_t routes[] = {
        { .uri = "/",         .method = HTTP_GET,  .handler = handle_root },
        { .uri = "/settings", .method = HTTP_POST, .handler = handle_save_wifi },
        { .uri = "/upload",   .method = HTTP_GET,  .handler = handle_upload_page },
        { .uri = "/update",   .method = HTTP_POST, .handler = handle_update },
        { .uri = "/wifi.json", .method = HTTP_GET, .handler = handle_view_json },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) httpd_register_uri_handler(server, &routes[i]);
    ESP_LOGI(TAG, "Web server da chay");
}

void web_stop(void)
{
    if (!server) return;
    httpd_stop(server);
    server = NULL;
    ESP_LOGI(TAG, "Web server da dung");
}
