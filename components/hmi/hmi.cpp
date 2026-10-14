#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

#include "hmi.h"
#include "motor_modbus.h"   // crc16

static const char *TAG = "HMI";

// UART
#define HMI_UART    UART_NUM_2
#define RX_PIN      16
#define TX_PIN      17
#define BAUD        115200

#define FRAME_HEADER_1    0x5A
#define FRAME_HEADER_2    0xA5
#define QUEUE_SIZE        8

static void sendFrame(const uint8_t *payload, uint8_t length)
{
    uint8_t frame[FRAME_SIZE];
    uint16_t crc = crc16(payload, length);
    uint8_t index = 0;

    frame[index++] = FRAME_HEADER_1;
    frame[index++] = FRAME_HEADER_2;
    frame[index++] = length + 2;

    memcpy(&frame[index], payload, length);
    index += length;

    frame[index++] = crc & 0xFF;
    frame[index++] = crc >> 8;

    uart_write_bytes(HMI_UART, frame, index);
}

void writeData(uint16_t address, uint16_t value)
{
    uint8_t buffer[5];

    buffer[0] = CMD_WRITE;
    buffer[1] = address >> 8;
    buffer[2] = address & 0xFF;
    buffer[3] = value >> 8;
    buffer[4] = value & 0xFF;

    sendFrame(buffer, 5);
}

void readData(uint16_t address, uint16_t amount)
{
    uint8_t buffer[5];

    buffer[0] = CMD_READ;
    buffer[1] = address >> 8;
    buffer[2] = address & 0xFF;
    buffer[3] = amount >> 8;
    buffer[4] = amount & 0xFF;

    sendFrame(buffer, 5);
}

// ---------- Queue ----------
static Frame queue_[QUEUE_SIZE];
static uint8_t qHead = 0;
static uint8_t qTail = 0;

static bool qPush(const uint8_t *src, uint8_t length)
{
    uint8_t next = (qHead + 1) % QUEUE_SIZE;

    if (next == qTail) return false;

    memcpy(queue_[qHead].buf, src, length + 1);
    qHead = next;

    return true;
}

bool qPop(Frame &frame)
{
    if (qHead == qTail) return false;

    frame = queue_[qTail];
    qTail = (qTail + 1) % QUEUE_SIZE;

    return true;
}

// ---------- UART frame parser ----------
static inline bool uartReadByte(uint8_t &c) { return uart_read_bytes(HMI_UART, &c, 1, 0) == 1; }

void pollFrame()
{
    static uint8_t state = 0;
    static uint8_t index = 0;
    static uint8_t temp[FRAME_SIZE];

    uint8_t c;

    while (uartReadByte(c))
    {
        if (state == 0)
        {
            if (c == FRAME_HEADER_1) state = 1;
        }
        else if (state == 1)
        {
            state = (c == FRAME_HEADER_2) ? 2 : (c == FRAME_HEADER_1 ? 1 : 0);
        }
        else if (state == 2)
        {
            if (c < 5 || c > FRAME_SIZE - 3)
            {
                state = 0;
                continue;
            }

            temp[0] = c;
            index = 1;
            state = 3;
        }
        else
        {
            temp[index++] = c;

            if (index > temp[0])
            {
                uint8_t length = temp[0] - 2;
                uint16_t receivedCrc = temp[1 + length] | (temp[2 + length] << 8);

                if (crc16(&temp[1], length) == receivedCrc && (temp[1] == CMD_READ || temp[1] == CMD_TOUCH))
                {
                    qPush(temp, temp[0]);
                }

                state = 0;
            }
        }
    }
}

void hmiInit()
{
    uart_config_t config = {};

    config.baud_rate = BAUD;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(HMI_UART, 512, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(HMI_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(HMI_UART, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "HMI UART initialized");
}