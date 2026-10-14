#pragma once
#include <stdint.h>
#include <stdbool.h>

bool pageHomeHandleTouch(uint16_t address, uint16_t data);   // true nếu page này đã xử lý
void pageHomeOnRead(uint16_t address, uint16_t value);       // kết quả readData() trả về
void pageHomeTick();                                         // gọi mỗi vòng lặp