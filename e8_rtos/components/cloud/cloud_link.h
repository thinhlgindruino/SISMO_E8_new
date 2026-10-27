/*
 * cloud_link.h - Lớp kết nối MQTT của cloud (tách riêng để phần logic cloud.c không phụ thuộc thư viện MQTT)
 *
 * Bản ESP32 (cloud_link.c) dùng esp-mqtt của ESP-IDF; bản chạy thử trên máy tính dùng libmosquitto.
 * Mỗi lúc chỉ mở 1 kết nối. link_open() trả về ngay, kết quả báo qua hàm callback (chạy trong task của
 * thư viện MQTT, dữ liệu chỉ hợp lệ trong lúc gọi -> cloud.c chép ra hàng đợi của mình):
 *   LINK_EV_CONNECTED     đã kết nối (sau mỗi lần tự kết nối lại cũng báo)
 *   LINK_EV_DISCONNECTED  mất kết nối (thư viện tự kết nối lại)
 *   LINK_EV_REFUSED       server từ chối, code = mã CONNACK (4 sai username / token, 5 không được phép)
 *   LINK_EV_MESSAGE       tin nhắn đến (topic + data, tin dài đã được ghép đủ)
 * link_publish / link_subscribe: chỉ đưa vào hàng đợi gửi của thư viện, không chờ.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LINK_TOPIC_MAX            128
#define LINK_KEEPALIVE_S          30
#define LINK_BUFFER_SIZE          6144

typedef enum {
    LINK_EV_CONNECTED,
    LINK_EV_DISCONNECTED,
    LINK_EV_REFUSED,
    LINK_EV_MESSAGE,
} link_event_type_t;

typedef struct {
    link_event_type_t type;
    int               code;
    const char       *topic;
    const uint8_t    *data;
    int               len;
} link_event_t;

typedef void (*link_callback_t)(const link_event_t *e);

bool link_open(const char *host, int port, const char *username, const char *client_id, link_callback_t cb);
void link_close(void);
bool link_publish(const char *topic, const void *data, int len, int qos);
bool link_subscribe(const char *topic, int qos);
