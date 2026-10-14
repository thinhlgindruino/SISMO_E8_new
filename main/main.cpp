#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hmi.h"
#include "motor_modbus.h"
#include "ui_router.h"     

static void loopOnce()
{
    pollFrame();

    Frame frame;

    while (qPop(frame))
    {
        processFrame(frame);
        pollFrame();
    }

    uiTick();
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