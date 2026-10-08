#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "HMI";
static const char *TAG_MOTOR = "MOTOR";

// UART
#define HMI_UART    UART_NUM_2
#define RX_PIN      17
#define TX_PIN      16
#define BAUD        115200

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
#define MOTOR_CMD_RUN           1       // driver: 1 = run
#define MOTOR_CMD_STOP          2       // driver: 2 = ramp down smoothly then stop (0 is not handled reliably)
#define MOTOR_CURRENT_SAFETY    0       // 0 = driver default limit (3.70 A)
#define MOTOR_MODE              3       // must be non-zero, 3 = neutral
#define MOTOR_RATIO             9       // must be non-zero, 9 = driver default ratio

// HMI commands
#define CMD_WRITE   0x10
#define CMD_READ    0x03
#define CMD_TOUCH   0x41

#define FRAME_HEADER_1    0x5A
#define FRAME_HEADER_2    0xA5

#define READ_INTERVAL_MS  1000

#define QUEUE_SIZE        8
#define FRAME_SIZE        64

// UART Address
#define TOUCH_BUTTON_ADDRESS 0xFFFF
#define TRIGGER_BUTTON_ADDRESS 0x700D
#define TIMER_ADDRESS 0x0002
#define POWER_ADDRESS 0x0004
#define TIMER_CONTROL_ADDRESS 0x000A
#define TIMER_PRESET_ADDRESS 0x000B
#define TIMER_COUNT_ADDRESS 0x000C
#define CONTROL_TIMER_BUTTON_ADDRESS 0x000A
#define CONTROL_WAVE_BUTTON_ADDRESS 0x0043
#define PASSWORD_PAGE_BUTTON_ADDRESS 0x0042
#define PASSWORD_ADDRESS 0x002D

// UART Value
#define RESET_TIMER_BUTTON_VALUE 0x0100
#define PASSWORD_PAGE_BUTTON_VALUE 0x0014
#define DIGIT_0_VALUE 0x00B0            // keys 0..9 = 0x00B0..0x00B9
#define DIGIT_9_VALUE 0x00B9
#define DELETE_PASSWORD_VALUE 0x00BC
#define ENTER_PASSWORD_VALUE 0x00BE

// Password
#define PASSWORD_MAX_LEN 10
static const char *PASSWORD_CORRECT = "1234";   // TODO: change to your password

#define POWER_MIN      1
#define POWER_MAX      32
#define WAVE_STEP_MS   100

// Variable
static uint16_t timer_second = 0;
static uint16_t timer_minute = 0;
static uint16_t power_value = 1;
static uint16_t motor_command = MOTOR_CMD_STOP;
static uint16_t motor_speed = 1;    // speed sent to motor: power_value, or power_value + wave offset while wave is active

static const int8_t WAVE_TABLE[] = {0, 1, 2, 3, 2, 1, 0, -1, -2, -3, -2, -1};
#define WAVE_LEN (sizeof(WAVE_TABLE) / sizeof(WAVE_TABLE[0]))
static bool     waveActive  = false;
static uint8_t  wavePhase   = 0;
static uint32_t lastWaveMs  = 0;

static char     inputBuf[PASSWORD_MAX_LEN + 1] = {0};
static uint8_t  inputLen = 0;

static uint32_t lastReadMs = 0;

static inline uint32_t millis() {return (uint32_t)(esp_timer_get_time() / 1000);}

static uint16_t crc16(const uint8_t *data, uint8_t len)
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

// ===================== Motor driver (Modbus RTU master) =====================
static uint16_t au16data[32] = {0};     // [0..14] written to driver, [16..30] read from driver
static SemaphoreHandle_t motorMutex = NULL;
static volatile bool linkLost = false;

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

static void driver_begin()
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

// Call every loop: send current command / speed to the driver and log error changes
static void motorUpdate()
{
    static uint16_t lastError = 0;
    uint16_t card, current, error;

    poll_driver(motor_command, motor_speed, MOTOR_CURRENT_SAFETY, MOTOR_MODE, MOTOR_RATIO, &card, &current, &error);

    if (error != lastError)
    {
        lastError = error;
        if (error != 0) ESP_LOGW(TAG_MOTOR, "Driver error code = %d", error);
        else ESP_LOGI(TAG_MOTOR, "Driver error cleared");
    }
}

static void motorSetRun(bool run)
{
    motor_command = run ? MOTOR_CMD_RUN : MOTOR_CMD_STOP;
    ESP_LOGI(TAG_MOTOR, "Motor %s, speed = %d", run ? "RUN" : "STOP", power_value);
}

// ===================== HMI =====================
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

static void writeData(uint16_t address, uint16_t value)
{
    uint8_t buffer[5];

    buffer[0] = CMD_WRITE;
    buffer[1] = address >> 8;
    buffer[2] = address & 0xFF;
    buffer[3] = value >> 8;
    buffer[4] = value & 0xFF;

    sendFrame(buffer, 5);
}

static void readData(uint16_t address, uint16_t amount)
{
    uint8_t buffer[5];

    buffer[0] = CMD_READ;
    buffer[1] = address >> 8;
    buffer[2] = address & 0xFF;
    buffer[3] = amount >> 8;
    buffer[4] = amount & 0xFF;

    sendFrame(buffer, 5);
}

// Show the typed password on the HMI text variable.
// Each word holds 2 ASCII chars; writeData is called once per word at consecutive addresses,
// then a 0x0000 word is written as the string terminator (also clears leftover chars after delete).
static void showInput()
{
    uint8_t words = 0;

    for (uint8_t i = 0; i < inputLen; i += 2)
    {
        uint8_t high = inputBuf[i];
        uint8_t low = (i + 1 < inputLen) ? inputBuf[i + 1] : 0x00;

        writeData(PASSWORD_ADDRESS + words, (high << 8) | low);
        words++;
    }

    writeData(PASSWORD_ADDRESS + words, 0x0000);
}

static void clearInput()
{
    inputLen = 0;
    inputBuf[0] = '\0';
    showInput();
}

struct Frame { uint8_t buf[FRAME_SIZE]; };

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

static bool qPop(Frame &frame)
{
    if (qHead == qTail) return false;

    frame = queue_[qTail];
    qTail = (qTail + 1) % QUEUE_SIZE;

    return true;
}

static inline bool uartReadByte(uint8_t &c) { return uart_read_bytes(HMI_UART, &c, 1, 0) == 1; }

// UART frame parser
static void pollFrame()
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

static void stopWave(bool restorePower)
{
    if (waveActive && restorePower) writeData(POWER_ADDRESS, power_value);
    motor_speed = power_value;
    waveActive = false;
    wavePhase = 0;
}

static void waveTick()
{
    if (!waveActive) return;

    uint32_t now = millis();

    if (now - lastWaveMs < WAVE_STEP_MS) return;

    lastWaveMs = now;
    wavePhase = (wavePhase + 1) % WAVE_LEN;
    int power = (int)power_value + WAVE_TABLE[wavePhase];

    if (power < POWER_MIN) power = POWER_MIN;
    if (power > POWER_MAX) power = POWER_MAX;

    writeData(POWER_ADDRESS, (uint16_t)power);
    motor_speed = (uint16_t)power;
}

static void handleRead(const uint8_t *rx)
{
    uint8_t length = rx[0];
    uint16_t address = (rx[2] << 8) | rx[3];
    uint16_t amount = (rx[4] << 8) | rx[5];
    uint16_t data[32];

    // Limit amount to what the frame really contains and to the buffer size
    uint16_t maxWords = (length - 7) / 2;
    if (amount > maxWords) amount = maxWords;
    if (amount > 32) amount = 32;
    if (amount == 0) return;

    for (int i = 0; i < amount; i++) {data[i] = (rx[6 + 2*i] << 8) | rx[7 + 2*i];}

    if (address == TIMER_COUNT_ADDRESS) timer_second = data[0];
    if (data[0] == 10)
    {
        clearInput();   // start with an empty password field
        writeData(TRIGGER_BUTTON_ADDRESS, PASSWORD_PAGE_BUTTON_VALUE);
        writeData(PASSWORD_PAGE_BUTTON_ADDRESS, 0);
    }

}

// Handle password keypad buttons (all come through TOUCH_BUTTON_ADDRESS)
static bool handlePasswordKey(uint16_t value)
{
    if (value >= DIGIT_0_VALUE && value <= DIGIT_9_VALUE)
    {
        if (inputLen < PASSWORD_MAX_LEN)
        {
            inputBuf[inputLen++] = '0' + (value - DIGIT_0_VALUE);
            inputBuf[inputLen] = '\0';
            showInput();
        }
        return true;
    }
    else if (value == DELETE_PASSWORD_VALUE)
    {
        if (inputLen > 0)
        {
            inputBuf[--inputLen] = '\0';
            showInput();
        }
        return true;
    }
    else if (value == ENTER_PASSWORD_VALUE)
    {
        if (strcmp(inputBuf, PASSWORD_CORRECT) == 0)
        {
            ESP_LOGI(TAG, "Password correct!");
        }
        else
        {
            ESP_LOGW(TAG, "Password wrong!");
        }

        clearInput();
        return true;
    }

    return false;
}

static void handleTouch(const uint8_t *rx)
{
    uint8_t length = rx[0];
    if (length == 0x23)
    {
        // Multi address touch
        for (int i = 0; i < 8; i++)
        {
            uint16_t address = ((uint16_t)rx[2 + 4*i] << 8) | rx[3 + 4*i];
            uint16_t data = ((uint16_t)rx[4 + 4*i] << 8) | rx[5 + 4*i];
            ESP_LOGI(TAG, "Touch %d: addr=0x%04X value=%d", i + 1, address, data);
        }
    }
    else
    {
        // Single address touch
        uint16_t address = ((uint16_t)rx[2] << 8) | rx[3];
        uint16_t data = ((uint16_t)rx[4] << 8) | rx[5];

        if (address == TOUCH_BUTTON_ADDRESS)
        {
            if (data == RESET_TIMER_BUTTON_VALUE)
            {
                stopWave(true);
                motorSetRun(false);
                writeData(CONTROL_WAVE_BUTTON_ADDRESS, 0); 
                writeData(TIMER_CONTROL_ADDRESS, 0);
                // Since time minute is not change, store it value again
                timer_second = timer_minute * 60;
                writeData(TIMER_COUNT_ADDRESS, timer_second);
            }
            else
            {
                // Digit / delete / enter keys of the password page
                handlePasswordKey(data);
            }
        }
        else if (address == TIMER_ADDRESS)
        {
            timer_minute = data;
            timer_second = timer_minute * 60;
            // Stop timer if running
            motorSetRun(false);
            writeData(TIMER_CONTROL_ADDRESS, 0);
            writeData(TIMER_COUNT_ADDRESS, timer_second);
            ESP_LOGI(TAG, "Change time to %d minute!", timer_minute);
        }
        else if (address == POWER_ADDRESS)
        {
            power_value = data;
            if (!waveActive) motor_speed = power_value;
            ESP_LOGI(TAG, "Change motor power to level %d!", power_value);
        }
        else if (address == CONTROL_TIMER_BUTTON_ADDRESS)
        {
            if (data == 1)
            {
                // Since start button have same address with control address, we dont need to modify control address
                writeData(TIMER_COUNT_ADDRESS, timer_second);
                writeData(TIMER_PRESET_ADDRESS, 0);
                motorSetRun(true);
            }
            else if (data == 0)
            {
                // Read value of current second to start from this value next time
                readData(TIMER_COUNT_ADDRESS, 1);
                motorSetRun(false);
            }
        }
        else if (address == CONTROL_WAVE_BUTTON_ADDRESS)
        {
            if (data == 1)
            {
                if (!waveActive)
                {
                    wavePhase = 0;
                    lastWaveMs = millis();
                    waveActive = true;
                    ESP_LOGI(TAG, "Wave start, power = %d", power_value);
                }
            }
            else {stopWave(true);}
        }
        // if user press button long enough, jump to password page and reset value of time press, otherwise reset it
        else if (address == PASSWORD_PAGE_BUTTON_ADDRESS)
        {
            if (data != 10)
            {
                writeData(PASSWORD_PAGE_BUTTON_ADDRESS, 0);
            }
        }
    }
}

static void processFrame(const Frame &frame)
{
    switch (frame.buf[1])
    {
        case CMD_READ:  handleRead(frame.buf);  break;
        case CMD_TOUCH: handleTouch(frame.buf); break;
        default: break;
    }
}

static void hmiInit()
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

static void loopOnce()
{
    uint32_t now = millis();

    if (now - lastReadMs >= READ_INTERVAL_MS)
    {
        lastReadMs = now;

        readData(PASSWORD_PAGE_BUTTON_ADDRESS, 1);
    }

    pollFrame();

    Frame frame;

    while (qPop(frame))
    {
        processFrame(frame);
        pollFrame();
    }

    waveTick();
    motorUpdate();
}

extern "C" void app_main(void)
{
    hmiInit();
    driver_begin();

    while (true)
    {
        loopOnce();
        vTaskDelay(1);
    }
}