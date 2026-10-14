#include <stdint.h>
#include "esp_log.h"

#include "ui_router.h"
#include "page_home.h"
#include "page_setting.h"

static const char *TAG = "ROUTER";

static void handleRead(const uint8_t *rx)
{
    uint8_t length = rx[0];
    uint16_t address = (rx[2] << 8) | rx[3];
    uint16_t amount = (rx[4] << 8) | rx[5];

    uint16_t maxWords = (length - 7) / 2;
    if (amount > maxWords) amount = maxWords;
    if (amount == 0) return;

    uint16_t value = (rx[6] << 8) | rx[7];

    pageHomeOnRead(address, value);
    pageSettingOnRead(address, value);
}

static void handleTouch(const uint8_t *rx)
{
    if (rx[0] == 0x23)
    {
        // Multi address touch
        for (int i = 0; i < 8; i++)
        {
            uint16_t address = ((uint16_t)rx[2 + 4*i] << 8) | rx[3 + 4*i];
            uint16_t data = ((uint16_t)rx[4 + 4*i] << 8) | rx[5 + 4*i];
            ESP_LOGI(TAG, "Touch %d: addr=0x%04X value=%d", i + 1, address, data);
        }
        return;
    }

    uint16_t address = ((uint16_t)rx[2] << 8) | rx[3];
    uint16_t data = ((uint16_t)rx[4] << 8) | rx[5];

    // Page nào nhận xử lý thì dừng
    if (pageHomeHandleTouch(address, data)) return;
    if (pageSettingHandleTouch(address, data)) return;

    ESP_LOGW(TAG, "Unhandled touch: addr=0x%04X value=0x%04X", address, data);
}

void processFrame(const Frame &frame)
{
    switch (frame.buf[1])
    {
        case CMD_READ:  handleRead(frame.buf);  break;
        case CMD_TOUCH: handleTouch(frame.buf); break;
        default: break;
    }
}

void uiTick()
{
    pageHomeTick();
    pageSettingTick();
}