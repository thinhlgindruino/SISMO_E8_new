#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "motor_modbus.h"

static const char *TAG_MOTOR = "MOTOR";

// Motor driver UART (RS232 through SP3232)
#define MOTOR_UART          UART_NUM_1
#define MOTOR_RX_PIN        33
#define MOTOR_TX_PIN        32
#define MOTOR_BAUD          9600

// Motor modbus
#define MODBUS_SLAVE_ID         1
#define MODBUS_TIMEOUT_MS       1000
#define MODBUS_INTERBYTE_MS     20
#define MODBUS_POLL_PERIOD_MS   100
#define LINK_LOST_TIMEOUT_MS    20000
#define ERR_CONNECTION          2
#define WRITE_START_ADDR        0
#define WRITE_COUNT             15
#define READ_START_ADDR         16
#define READ_COUNT              15

// Motor registers
#define reg_control_command     0
#define reg_current_safety      3
#define reg_speed_setting       6
#define reg_mode_setting        8
#define reg_ratio_setting       9
#define reg_value_error         17
#define reg_card_status         18
#define reg_current_run         22

// Motor fixed settings (from driver_config.h)
#define MOTOR_CMD_RUN           1
#define MOTOR_CMD_STOP          2
#define MOTOR_CURRENT_SAFETY    0
#define MOTOR_MODE              3
#define MOTOR_RATIO             9

static uint16_t motor_command = MOTOR_CMD_STOP;
static uint16_t motor_speed = 1;

static uint16_t au16data[32] = {0};     // [0..14] written to driver, [16..30] read from driver
static SemaphoreHandle_t motorMutex = NULL;
static volatile bool linkLost = false;

uint16_t crc16(const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0xFFFF;

    for (uint8_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }

    return crc;
}

// Wait for first byte up to MODBUS_TIMEOUT_MS, then read until the line is silent
static int modbusReceive(uint8_t *buf, size_t maxLen)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)MODBUS_TIMEOUT_MS * 1000;
    size_t n = 0;

    while (n == 0 && esp_timer_get_time() < deadline)
        n = (uart_read_bytes(MOTOR_UART, buf, 1, pdMS_TO_TICKS(10)) == 1) ? 1 : 0;

    if (n == 0) return 0;

    while (n < maxLen)
    {
        int r = uart_read_bytes(MOTOR_UART, buf + n, maxLen - n, pdMS_TO_TICKS(MODBUS_INTERBYTE_MS));
        if (r <= 0) break;
        n += r;
    }

    return (int)n;
}

// Send request, receive response, check CRC / slave id / exception. Returns response length or 0
static int modbusTransfer(const uint8_t *tx, uint8_t txLen, uint8_t *rx, size_t rxMax)
{
    uart_flush_input(MOTOR_UART);
    uart_write_bytes(MOTOR_UART, (const char *)tx, txLen);
    uart_wait_tx_done(MOTOR_UART, pdMS_TO_TICKS(100));

    int len = modbusReceive(rx, rxMax);

    if (len < 5 || crc16(rx, len - 2) != (rx[len - 2] | (rx[len - 1] << 8)) || rx[0] != tx[0])
    {
        ESP_LOGW(TAG_MOTOR, "No valid response (len=%d)", len);
        return 0;
    }

    if (rx[1] & 0x80)
    {
        ESP_LOGW(TAG_MOTOR, "Exception 0x%02X", rx[2]);
        return 0;
    }

    return len;
}

// Function 16: write multiple registers
static bool modbusWrite(uint16_t address, const uint16_t *regs, uint16_t count)
{
    uint8_t tx[9 + 2 * WRITE_COUNT];
    uint8_t rx[16];
    uint8_t p = 0;

    tx[p++] = MODBUS_SLAVE_ID;
    tx[p++] = 0x10;
    tx[p++] = address >> 8;
    tx[p++] = address & 0xFF;
    tx[p++] = count >> 8;
    tx[p++] = count & 0xFF;
    tx[p++] = count * 2;

    for (uint16_t i = 0; i < count; i++)
    {
        tx[p++] = regs[i] >> 8;
        tx[p++] = regs[i] & 0xFF;
    }

    uint16_t crc = crc16(tx, p);
    tx[p++] = crc & 0xFF;
    tx[p++] = crc >> 8;

    return modbusTransfer(tx, p, rx, sizeof(rx)) == 8;
}

// Function 3: read holding registers
static bool modbusRead(uint16_t address, uint16_t *regs, uint16_t count)
{
    uint8_t tx[8] = {MODBUS_SLAVE_ID, 0x03, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF),
                     (uint8_t)(count >> 8), (uint8_t)(count & 0xFF), 0, 0};
    uint8_t rx[5 + 2 * READ_COUNT];

    uint16_t crc = crc16(tx, 6);
    tx[6] = crc & 0xFF;
    tx[7] = crc >> 8;

    if (modbusTransfer(tx, 8, rx, sizeof(rx)) != 5 + count * 2 || rx[1] != 0x03) return false;

    for (uint16_t i = 0; i < count; i++)
        regs[i] = (rx[3 + 2*i] << 8) | rx[4 + 2*i];

    return true;
}

// Background task: alternate write (registers 0..14) and read (registers 16..30)
static void motorTask(void *arg)
{
    uint8_t query = 0;
    int64_t lastOkUs = esp_timer_get_time();

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(MODBUS_POLL_PERIOD_MS));

        uint16_t tmp[WRITE_COUNT];
        bool ok;

        if (query == 0)
        {
            xSemaphoreTake(motorMutex, portMAX_DELAY);
            memcpy(tmp, &au16data[WRITE_START_ADDR], sizeof(tmp));
            xSemaphoreGive(motorMutex);

            ok = modbusWrite(WRITE_START_ADDR, tmp, WRITE_COUNT);
        }
        else
        {
            ok = modbusRead(READ_START_ADDR, tmp, READ_COUNT);

            if (ok)
            {
                xSemaphoreTake(motorMutex, portMAX_DELAY);
                memcpy(&au16data[READ_START_ADDR], tmp, sizeof(tmp));
                xSemaphoreGive(motorMutex);
            }
        }

        query ^= 1;

        if (ok)
        {
            lastOkUs = esp_timer_get_time();
            linkLost = false;
        }
        else if (esp_timer_get_time() - lastOkUs > (int64_t)LINK_LOST_TIMEOUT_MS * 1000)
        {
            linkLost = true;
        }
    }
}

void driver_begin()
{
    uart_config_t config = {};

    config.baud_rate = MOTOR_BAUD;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(MOTOR_UART, 512, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MOTOR_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(MOTOR_UART, MOTOR_TX_PIN, MOTOR_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    motorMutex = xSemaphoreCreateMutex();
    xTaskCreate(motorTask, "motor_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG_MOTOR, "Motor UART initialized");
}

static void poll_driver(uint16_t command_motor, uint16_t speed_setting, uint16_t speed_current,
                        uint16_t mode_setting, uint16_t ratio_setting,
                        uint16_t *card, uint16_t *current, uint16_t *error)
{
    xSemaphoreTake(motorMutex, portMAX_DELAY);

    au16data[reg_control_command] = command_motor;
    au16data[reg_speed_setting] = speed_setting;
    au16data[reg_current_safety] = speed_current;
    au16data[reg_mode_setting] = mode_setting;
    au16data[reg_ratio_setting] = ratio_setting;

    *card = au16data[reg_card_status];
    *current = au16data[reg_current_run];
    *error = linkLost ? ERR_CONNECTION : au16data[reg_value_error];

    xSemaphoreGive(motorMutex);
}

void motorUpdate()
{
    static uint16_t lastError = 0;
    static int64_t lastLogUs = 0;
    uint16_t card, current, error;

    poll_driver(motor_command, motor_speed, MOTOR_CURRENT_SAFETY, MOTOR_MODE, MOTOR_RATIO, &card, &current, &error);

    // Log debug mỗi 1 giây
    if (esp_timer_get_time() - lastLogUs > 1000000)
    {
        lastLogUs = esp_timer_get_time();
        ESP_LOGI(TAG_MOTOR, "cmd=%d speed=%d card=%d current=%d error=%d",
                 motor_command, motor_speed, card, current, error);
    }

    if (error != lastError)
    {
        lastError = error;
        if (error != 0) ESP_LOGW(TAG_MOTOR, "Driver error code = %d", error);
        else ESP_LOGI(TAG_MOTOR, "Driver error cleared");
    }
}

void motorSetRun(bool run)
{
    motor_command = run ? MOTOR_CMD_RUN : MOTOR_CMD_STOP;
    ESP_LOGI(TAG_MOTOR, "Motor %s, speed = %d", run ? "RUN" : "STOP", motor_speed);
}

void motorSetSpeed(uint16_t speed)
{
    motor_speed = speed;
}