/*
 * cloud_link.c - Lớp kết nối MQTT bằng esp-mqtt (xem cloud_link.h)
 *
 * - Kết nối TCP thường (cổng 1883), keepalive LINK_KEEPALIVE_S: mất điện / mất mạng thì server biết sau
 *   khoảng 1,5 x keepalive. Bộ đệm LINK_BUFFER_SIZE đủ chứa 1 khúc firmware 4 KB + topic.
 * - Tin dài hơn bộ đệm bị esp-mqtt chia nhiều sự kiện DATA -> ghép lại đủ rồi mới báo LINK_EV_MESSAGE.
 * - Server từ chối (CONNACK 4 / 5) -> báo LINK_EV_REFUSED; cloud.c tự đóng kết nối và đổi cách đăng nhập.
 * - Gửi bằng esp_mqtt_client_enqueue (không chờ mạng); task MQTT của esp-mqtt chạy core 0. Tin trong hàng đợi đi sau tối đa
 *   CONFIG_MQTT_POLL_READ_TIMEOUT_MS (sdkconfig: 100 ms, mặc định 1000 ms làm OTA chậm ~1 s / khúc).
 * - Dừng client (link_close) và subscribe có thể phải chờ task esp-mqtt vài giây (đang chờ thử lại / đang mở kết nối
 *   tới 10 s) -> task gọi tạm rời watchdog trong lúc chờ rồi đăng ký lại, không bị watchdog reset máy.
 * - Log lỗi kết nối của esp-mqtt / esp-tls / transport (dòng đỏ mỗi lần rớt mạng hoặc thử lại) bị tắt; cloud.c tự in
 *   1 dòng [CLOUD] dễ hiểu cho mỗi lần mất / có lại kết nối.
 */
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "mqtt_client.h"
#include "cloud_link.h"

static const char *TAG = "CLOUD_LINK";

static esp_mqtt_client_handle_t client = NULL;
static link_callback_t callback = NULL;
static char cur_topic[LINK_TOPIC_MAX];
static uint8_t *join_buf = NULL;
static int join_total = 0;

static bool wdt_pause(void)
{
    if (esp_task_wdt_status(NULL) != ESP_OK) return false;
    esp_task_wdt_delete(NULL);
    return true;
}

static void wdt_resume(bool paused)
{
    if (paused) esp_task_wdt_add(NULL);
}

static void emit(link_event_type_t type, int code, const char *topic, const uint8_t *data, int len)
{
    if (!callback) return;
    link_event_t e = { .type = type, .code = code, .topic = topic, .data = data, .len = len };
    callback(&e);
}

static void on_data(esp_mqtt_event_handle_t ev)
{
    if (ev->current_data_offset == 0) {
        int n = ev->topic_len < LINK_TOPIC_MAX - 1 ? ev->topic_len : LINK_TOPIC_MAX - 1;
        memcpy(cur_topic, ev->topic, n);
        cur_topic[n] = '\0';
        free(join_buf);
        join_buf = NULL;
        join_total = ev->total_data_len;
        if (ev->data_len == ev->total_data_len) {
            emit(LINK_EV_MESSAGE, 0, cur_topic, (const uint8_t *)ev->data, ev->data_len);
            return;
        }
        join_buf = malloc(join_total);
        if (!join_buf) return;
    }
    if (!join_buf || ev->current_data_offset + ev->data_len > join_total) return;
    memcpy(join_buf + ev->current_data_offset, ev->data, ev->data_len);
    if (ev->current_data_offset + ev->data_len == join_total) {
        emit(LINK_EV_MESSAGE, 0, cur_topic, join_buf, join_total);
        free(join_buf);
        join_buf = NULL;
    }
}

static void handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t ev = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        emit(LINK_EV_CONNECTED, 0, NULL, NULL, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        emit(LINK_EV_DISCONNECTED, 0, NULL, NULL, 0);
        break;
    case MQTT_EVENT_DATA:
        on_data(ev);
        break;
    case MQTT_EVENT_ERROR:
        if (ev->error_handle && ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
            emit(LINK_EV_REFUSED, ev->error_handle->connect_return_code, NULL, NULL, 0);
        break;
    default:
        break;
    }
}

bool link_open(const char *host, int port, const char *username, const char *client_id, link_callback_t cb)
{
    esp_log_level_set("mqtt_client", ESP_LOG_NONE);
    esp_log_level_set("transport_base", ESP_LOG_NONE);
    esp_log_level_set("transport", ESP_LOG_NONE);
    esp_log_level_set("esp-tls", ESP_LOG_NONE);
    link_close();
    callback = cb;
    esp_mqtt_client_config_t c = {
        .broker.address.hostname = host,
        .broker.address.port = port,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.username = username,
        .credentials.client_id = client_id,
        .session.keepalive = LINK_KEEPALIVE_S,
        .network.reconnect_timeout_ms = 5000,
        .network.timeout_ms = 10000,
        .buffer.size = LINK_BUFFER_SIZE,
        .buffer.out_size = 2048,
        .task.stack_size = 6144,
    };
    client = esp_mqtt_client_init(&c);
    if (!client) { ESP_LOGE(TAG, "Khong tao duoc MQTT client"); return false; }
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, handler, NULL);
    if (esp_mqtt_client_start(client) != ESP_OK) {
        esp_mqtt_client_destroy(client);
        client = NULL;
        return false;
    }
    return true;
}

void link_close(void)
{
    if (!client) return;
    bool paused = wdt_pause();
    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    wdt_resume(paused);
    client = NULL;
    free(join_buf);
    join_buf = NULL;
}

bool link_publish(const char *topic, const void *data, int len, int qos)
{
    if (!client) return false;
    return esp_mqtt_client_enqueue(client, topic, data, len, qos, 0, true) >= 0;
}

bool link_subscribe(const char *topic, int qos)
{
    if (!client) return false;
    bool paused = wdt_pause();
    bool ok = esp_mqtt_client_subscribe_single(client, topic, qos) >= 0;
    wdt_resume(paused);
    return ok;
}
