#pragma once
#include <stdint.h>
#include <stdbool.h>

bool pageSettingHandleTouch(uint16_t address, uint16_t data);
void pageSettingOnRead(uint16_t address, uint16_t value);
void pageSettingTick();