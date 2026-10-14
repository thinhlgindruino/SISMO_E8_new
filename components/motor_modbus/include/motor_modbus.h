#pragma once
#include <stdint.h>
#include <stdbool.h>

uint16_t crc16(const uint8_t *data, uint8_t len);

void driver_begin();                 // khởi tạo UART motor + tạo task Modbus
void motorUpdate();                  // gọi mỗi vòng lặp
void motorSetRun(bool run);
void motorSetSpeed(uint16_t speed);