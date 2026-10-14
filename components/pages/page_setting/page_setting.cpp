#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_log.h"

#include "page_setting.h"
#include "hmi.h"
#include "hmi_map.h"

static const char *TAG = "SETTING";

// Address
#define PASSWORD_PAGE_BUTTON_ADDRESS    0x0006
#define PASSWORD_ADDRESS                0x0024
#define CHANGE_LANGUAGE_BUTTON_ADDRESS  0x001E
#define STATE_12_ADDRESS                0x0039

// Value
#define PASSWORD_PAGE_BUTTON_VALUE      0x0014
#define DIGIT_0_VALUE                   0x00B0      // keys 0..9 = 0x00B0..0x00B9
#define DIGIT_9_VALUE                   0x00B9
#define DELETE_PASSWORD_VALUE           0x00BC
#define ON_STATE_VALUE                  0x0106
#define OFF_STATE_VALUE                 0x0107
#define PASSWORD_1_BUTTON_VALUE         0x0110

#define PASSWORD_MAX_LEN    6
#define READ_INTERVAL_MS    1000

// State button: 1..12 (1..6 = one group, 7..12 = other group)
#define STATE_MIN           1
#define STATE_MAX           12
#define STATE_GROUP_SIZE    6

static uint8_t  state_button = 1;
static char     inputBuf[PASSWORD_MAX_LEN + 1] = {0};
static uint8_t  inputLen = 0;
static uint32_t lastReadMs = 0;

static void updateState()
{
    writeData(STATE_12_ADDRESS, state_button);
    ESP_LOGI(TAG, "state_button = %d", state_button);
}

// ===================== Password =====================
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

struct PasswordEntry
{
    const char *password;
    void (*action)();
};

static void passwordEvent1()
{
    ESP_LOGI(TAG, "Password 1 correct!");
    writeData(TRIGGER_BUTTON_ADDRESS, PASSWORD_1_BUTTON_VALUE);
}

static const PasswordEntry PASSWORD_TABLE[] = {
    {"74766", passwordEvent1},
};
#define PASSWORD_COUNT (sizeof(PASSWORD_TABLE) / sizeof(PASSWORD_TABLE[0]))

static bool checkPassword()
{
    for (size_t i = 0; i < PASSWORD_COUNT; i++)
    {
        if (strcmp(inputBuf, PASSWORD_TABLE[i].password) == 0)
        {
            PASSWORD_TABLE[i].action();
            return true;
        }
    }

    return false;
}

static bool handlePasswordKey(uint16_t value)
{
    if (value >= DIGIT_0_VALUE && value <= DIGIT_9_VALUE)
    {
        if (inputLen < PASSWORD_MAX_LEN)
        {
            inputBuf[inputLen++] = '0' + (value - DIGIT_0_VALUE);
            inputBuf[inputLen] = '\0';

            if (checkPassword())
            {
                clearInput();
            }
            else if (inputLen >= PASSWORD_MAX_LEN)
            {
                ESP_LOGW(TAG, "Password wrong!");
                clearInput();
            }
            else
            {
                showInput();
            }
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

    return false;
}

// ===================== Public =====================
void pageSettingTick()
{
    uint32_t now = millis();

    if (now - lastReadMs >= READ_INTERVAL_MS)
    {
        lastReadMs = now;
        readData(PASSWORD_PAGE_BUTTON_ADDRESS, 1);
    }
}

void pageSettingOnRead(uint16_t address, uint16_t value)
{
    if (address == PASSWORD_PAGE_BUTTON_ADDRESS && value == 10)
    {
        clearInput();
        writeData(TRIGGER_BUTTON_ADDRESS, PASSWORD_PAGE_BUTTON_VALUE);
        writeData(PASSWORD_PAGE_BUTTON_ADDRESS, 0);
    }
}

bool pageSettingHandleTouch(uint16_t address, uint16_t data)
{
    if (address == TOUCH_BUTTON_ADDRESS)
    {
        if (data == ON_STATE_VALUE)
        {
            if (state_button > STATE_GROUP_SIZE)
            {
                state_button -= STATE_GROUP_SIZE;
                updateState();
            }
            return true;
        }
        else if (data == OFF_STATE_VALUE)
        {
            if (state_button <= STATE_GROUP_SIZE)
            {
                state_button += STATE_GROUP_SIZE;
                updateState();
            }
            return true;
        }

        return handlePasswordKey(data);
    }
    else if (address == CHANGE_LANGUAGE_BUTTON_ADDRESS)
    {
        if (data == 1)
            state_button = (state_button > STATE_GROUP_SIZE) ? (STATE_GROUP_SIZE + 1) : STATE_MIN;
        else if (state_button < STATE_MAX)
            state_button++;

        updateState();
        return true;
    }
    else if (address == PASSWORD_PAGE_BUTTON_ADDRESS)
    {
        if (data != 10) writeData(PASSWORD_PAGE_BUTTON_ADDRESS, 0);
        return true;
    }

    return false;
}