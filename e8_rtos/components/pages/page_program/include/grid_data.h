/*
 * grid_data.h - Lưới bài tập con trang 0013 ghép từ ô (số liệu ở grid_data.c, sinh bởi tools/grilles.py)
 *
 * GRID_TILES[lưới - 1][ô]: số ảnh ô (icon 1000 + số - 1), 0 = ô trống; 15 ô = 3 cột x 5 hàng.
 * GRID_ARROW[lưới - 1]: 0 không mũi tên, 1 mũi tên >, 2 mũi tên <.
 */
#pragma once
#include <stdint.h>

#define GRID_COUNT                10
#define GRID_SLOTS                15

extern const uint8_t GRID_TILES[GRID_COUNT][GRID_SLOTS];
extern const uint8_t GRID_ARROW[GRID_COUNT];
