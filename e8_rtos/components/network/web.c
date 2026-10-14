/*
 * web.c - Web server cấu hình tại http://192.168.10.100 (giống trang web E8 cũ)
 *
 *   GET  /           tab Settings: WIFI SETUP (SSID + Password + Submit)
 *   POST /settings   lưu WiFi (đúng hay sai đều lưu) -> tắt AP -> màn hình về trang 0 -> khởi động lại
 *   GET  /upload     tab Upload: phiên bản đang chạy, vùng đang chạy, nút Roll back, chọn file .bin
 *   POST /update     thân request là nguyên file .bin -> ghi qua component ota -> khởi động lại
 *   POST /rollback   quay về firmware ở vùng OTA còn lại
 *   GET  /wifi.json  xem file WiFi đã lưu (mật khẩu che ****)
 *
 * Chỉ chạy trong chế độ cấu hình (web_start / web_stop do network gọi). Server chạy core 0, stack 8 KB.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "ota.h"
#include "network.h"
#include "web.h"

static const char *TAG = "WEB";
static httpd_handle_t server = NULL;

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
    "function quayLai(){if(!confirm('Roll back to the previous firmware?'))return;"
    "var t=document.getElementById('tt');var x=new XMLHttpRequest();x.open('POST','/rollback');"
    "x.onload=function(){t.innerHTML=x.responseText;};x.send();}"
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

static esp_err_t handle_root(httpd_req_t *req) { return send_page(req, HTML_MENU_SETTINGS, HTML_SETTINGS); }

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

    ssid[32] = '\0';
    pass[64] = '\0';
    if (ssid[0] == '\0') return send_message(req, "Please enter the SSID.<br><a href='/'>Back</a>");

    bool ok = net_save_wifi(ssid, pass);
    char ssid_html[200], msg[320];
    html_escape(ssid, ssid_html, sizeof(ssid_html));
    if (ok) snprintf(msg, sizeof(msg), "Saved. The device will restart and connect to <b>%s</b>.", ssid_html);
    else    snprintf(msg, sizeof(msg), "Save failed. The device will restart.");
    send_message(req, msg);
    net_finish_config_and_restart(1000);
    return ESP_OK;
}

static esp_err_t handle_view_json(httpd_req_t *req)
{
    char buf[256];
    int n = net_read_json(buf, sizeof(buf), true);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, n > 0 ? buf : "{}");
}

static esp_err_t handle_upload_page(httpd_req_t *req)
{
    ota_info_t i;
    ota_get_info(&i);
    char info[640];
    int n = snprintf(info, sizeof(info),
        "<div class='noidung'><h2>DEVICE SETTINGS</h2><h3>FIRMWARE UPDATE</h3>"
        "<p>Current firmware: <b>%s v%s</b><br>Built: %s %s<br>Running from: %s%s</p>",
        i.project, i.version, i.build_date, i.build_time, i.running,
        i.pending_verify ? " (new, being verified)" : "");
    if (i.can_rollback && n < (int)sizeof(info))
        snprintf(info + n, sizeof(info) - n,
                 "<p>Previous firmware: v%s <button type='button' onclick='quayLai()'>Roll back</button></p>",
                 i.other_version);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, HTML_HEAD);
    httpd_resp_sendstr_chunk(req, HTML_MENU_UPLOAD);
    httpd_resp_sendstr_chunk(req, info);
    httpd_resp_sendstr_chunk(req, HTML_UPLOAD);
    httpd_resp_sendstr_chunk(req, HTML_TAIL);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t handle_update(httpd_req_t *req)
{
    const char *err = NULL;
    if (!ota_upload_begin(req->content_len, &err)) { httpd_resp_sendstr(req, err); return ESP_OK; }

    char buf[1024];
    size_t remaining = req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { ota_upload_abort(); return ESP_FAIL; }
        if (!ota_upload_write(buf, r, &err)) { httpd_resp_sendstr(req, err); return ESP_OK; }
        remaining -= r;
    }
    if (!ota_upload_finish(&err)) { httpd_resp_sendstr(req, err); return ESP_OK; }

    httpd_resp_sendstr(req, "Update OK. The device is restarting...");
    net_finish_config_and_restart(1500);
    return ESP_OK;
}

static esp_err_t handle_rollback(httpd_req_t *req)
{
    const char *err = NULL;
    if (!ota_rollback(&err)) { httpd_resp_sendstr(req, err); return ESP_OK; }
    httpd_resp_sendstr(req, "Rolling back. The device is restarting...");
    net_finish_config_and_restart(1500);
    return ESP_OK;
}

void web_start(void)
{
    if (server) return;
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.stack_size = 8192;
    c.core_id = 0;
    c.lru_purge_enable = true;
    if (httpd_start(&server, &c) != ESP_OK) { ESP_LOGE(TAG, "Khong mo duoc web server"); return; }

    const httpd_uri_t routes[] = {
        { .uri = "/",         .method = HTTP_GET,  .handler = handle_root },
        { .uri = "/settings", .method = HTTP_POST, .handler = handle_save_wifi },
        { .uri = "/upload",   .method = HTTP_GET,  .handler = handle_upload_page },
        { .uri = "/update",   .method = HTTP_POST, .handler = handle_update },
        { .uri = "/wifi.json", .method = HTTP_GET, .handler = handle_view_json },
        { .uri = "/rollback", .method = HTTP_POST, .handler = handle_rollback },
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
