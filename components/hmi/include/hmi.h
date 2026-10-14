#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_timer.h"

#define CMD_WRITE   0x10
#define CMD_READ    0x03
#define CMD_TOUCH   0x41
#define FRAME_SIZE  64

struct Frame { uint8_t buf[FRAME_SIZE]; };

static inline uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }

void hmiInit();
void writeData(uint16_t address, uint16_t value);
void readData(uint16_t address, uint16_t amount);
void pollFrame();            // đọc UART, ghép frame, đẩy vào hàng đợi
bool qPop(Frame &frame);     // lấy 1 frame ra khỏi hàng đợi