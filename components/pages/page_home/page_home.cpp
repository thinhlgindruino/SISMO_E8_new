#include <stdint.h>
#include "esp_log.h"

#include "page_home.h"
#include "hmi.h"
#include "hmi_map.h"
#include "motor_modbus.h"

static const char *TAG = "HOME";

// Address
#define TIMER_ADDRESS                   0x0007
#define POWER_ADDRESS                   0x000B
#define TIMER_CONTROL_ADDRESS           0x0014
#define TIMER_PRESET_ADDRESS            0x0012
#define TIMER_COUNT_ADDRESS             0x0013
#define CONTROL_TIMER_BUTTON_ADDRESS    0x0015
#define CONTROL_WAVE_BUTTON_ADDRESS     0x0010

// Value
#define RESET_TIMER_BUTTON_VALUE        0x0100
#define BUZZER_BUTTON_VALUE             0x0105

#define POWER_MIN      1
#define POWER_MAX      32
#define WAVE_STEP_MS   100

#define START_BUZZER_COUNT        5
#define START_BUZZER_INTERVAL_MS  1000

// Variable
static uint16_t timer_second = 0;
static uint16_t timer_minute = 0;
static uint16_t power_value = 1;

static const int8_t WAVE_TABLE[] = {0, 1, 2, 3, 2, 1, 0, -1, -2, -3, -2, -1};
#define WAVE_LEN (sizeof(WAVE_TABLE) / sizeof(WAVE_TABLE[0]))
static bool     waveActive  = false;
static uint8_t  wavePhase   = 0;
static uint32_t lastWaveMs  = 0;

static bool     startPending = false;
static uint8_t  buzzerDone   = 0;
static uint32_t lastBuzzerMs = 0;

// ===================== Wave =====================
static void stopWave(bool restorePower)
{
    if (waveActive && restorePower) writeData(POWER_ADDRESS, power_value);
    motorSetSpeed(power_value);
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
    motorSetSpeed((uint16_t)power);
}

// ===================== Start sequence =====================
static void cancelStart()
{
    startPending = false;
    buzzerDone = 0;
}

static void startTick()
{
    if (!startPending) return;

    uint32_t now = millis();

    if (buzzerDone > 0 && now - lastBuzzerMs < START_BUZZER_INTERVAL_MS) return;

    writeData(TRIGGER_BUTTON_ADDRESS, BUZZER_BUTTON_VALUE);
    buzzerDone++;
    lastBuzzerMs = now;
    ESP_LOGI(TAG, "Start buzzer %d/%d", buzzerDone, START_BUZZER_COUNT);

    if (buzzerDone >= START_BUZZER_COUNT)
    {
        startPending = false;
        buzzerDone = 0;
        writeData(TIMER_CONTROL_ADDRESS, 1);
        motorSetRun(true);
    }
}

// ===================== Public =====================
void pageHomeTick()
{
    waveTick();
    startTick();
}

void pageHomeOnRead(uint16_t address, uint16_t value)
{
    if (address == TIMER_COUNT_ADDRESS) timer_second = value;
}

bool pageHomeHandleTouch(uint16_t address, uint16_t data)
{
    if (address == TOUCH_BUTTON_ADDRESS)
    {
        if (data != RESET_TIMER_BUTTON_VALUE) return false;

        cancelStart();
        stopWave(true);
        motorSetRun(false);
        writeData(CONTROL_WAVE_BUTTON_ADDRESS, 0);
        writeData(TIMER_CONTROL_ADDRESS, 0);
        timer_second = timer_minute * 60;
        writeData(TIMER_COUNT_ADDRESS, timer_second);
        return true;
    }
    else if (address == TIMER_ADDRESS)
    {
        cancelStart();
        timer_minute = data;
        timer_second = timer_minute * 60;
        motorSetRun(false);
        writeData(TIMER_CONTROL_ADDRESS, 0);
        writeData(TIMER_COUNT_ADDRESS, timer_second);
        ESP_LOGI(TAG, "Change time to %d minute!", timer_minute);
        return true;
    }
    else if (address == POWER_ADDRESS)
    {
        power_value = data;
        if (!waveActive) motorSetSpeed(power_value);
        ESP_LOGI(TAG, "Change motor power to level %d!", power_value);
        return true;
    }
    else if (address == CONTROL_TIMER_BUTTON_ADDRESS)
    {
        if (data == 1)
        {
            if (!startPending)
            {
                writeData(TIMER_COUNT_ADDRESS, timer_second);
                writeData(TIMER_PRESET_ADDRESS, 0);
                buzzerDone = 0;
                startPending = true;
            }
        }
        else if (data == 0)
        {
            cancelStart();
            readData(TIMER_COUNT_ADDRESS, 1);
            writeData(TIMER_CONTROL_ADDRESS, 0);
            motorSetRun(false);
        }
        return true;
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
        else stopWave(true);
        return true;
    }

    return false;
}