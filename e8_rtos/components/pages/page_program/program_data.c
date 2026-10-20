/*
 * program_data.c - Số liệu 54 bài Programmes (SINH TỰ ĐỘNG bởi tools/programmes.py, không sửa tay)
 *
 * Mỗi bài: tên, có nút X, lưới bài tập con, thời lượng / mức độ, biểu đồ / mức độ (đoạn cột:
 * số cột, mức cao, mức thấp; mức thấp 0 = cùng mức), danh sách bài tập con (giây, hình 1..32).
 * Số thứ tự biểu đồ (0..53) = icon 0800 + n (xám), 0860 + n (xanh).
 */
#include "program_data.h"

static const program_seg_t SEG_00[] = { { 60, 18, 6 } };
static const program_seg_t SEG_01[] = { { 60, 22, 8 } };
static const program_seg_t SEG_02[] = { { 60, 25, 10 } };
static const program_seg_t SEG_03[] = { { 36, 16, 5 }, { 24, 20, 7 } };
static const program_seg_t SEG_04[] = { { 36, 19, 7 }, { 24, 23, 8 } };
static const program_seg_t SEG_05[] = { { 36, 23, 9 }, { 24, 27, 11 } };
static const program_seg_t SEG_06[] = { { 120, 18, 6 } };
static const program_seg_t SEG_07[] = { { 48, 21, 8 }, { 72, 22, 9 } };
static const program_seg_t SEG_08[] = { { 120, 25, 10 } };
static const program_seg_t SEG_09[] = { { 8, 19, 7 }, { 8, 23, 9 }, { 8, 19, 7 }, { 8, 23, 9 }, { 8, 19, 7 }, { 8, 23, 9 }, { 8, 19, 7 }, { 8, 23, 9 }, { 8, 19, 7 }, { 8, 23, 9 }, { 40, 19, 7 } };
static const program_seg_t SEG_10[] = { { 8, 23, 7 }, { 8, 27, 10 }, { 8, 23, 7 }, { 8, 27, 10 }, { 8, 23, 7 }, { 8, 27, 10 }, { 8, 23, 7 }, { 8, 27, 10 }, { 8, 23, 7 }, { 8, 27, 10 }, { 40, 23, 7 } };
static const program_seg_t SEG_11[] = { { 8, 25, 10 }, { 8, 29, 12 }, { 8, 25, 10 }, { 8, 29, 12 }, { 8, 25, 10 }, { 8, 29, 12 }, { 8, 25, 10 }, { 8, 29, 12 }, { 8, 25, 10 }, { 8, 29, 12 }, { 40, 25, 10 } };
static const program_seg_t SEG_12[] = { { 18, 23, 9 }, { 18, 19, 7 }, { 18, 23, 9 }, { 18, 19, 7 }, { 18, 23, 9 }, { 18, 19, 7 }, { 12, 17, 6 } };
static const program_seg_t SEG_13[] = { { 18, 27, 11 }, { 18, 23, 9 }, { 18, 27, 11 }, { 18, 23, 9 }, { 18, 27, 11 }, { 18, 23, 9 }, { 12, 19, 7 } };
static const program_seg_t SEG_14[] = { { 18, 30, 13 }, { 18, 27, 11 }, { 18, 30, 13 }, { 18, 27, 11 }, { 18, 30, 13 }, { 18, 27, 11 }, { 12, 23, 9 } };
static const program_seg_t SEG_15[] = { { 36, 14, 0 }, { 24, 21, 0 } };
static const program_seg_t SEG_16[] = { { 36, 18, 0 }, { 24, 25, 0 } };
static const program_seg_t SEG_17[] = { { 36, 22, 0 }, { 24, 29, 0 } };
static const program_seg_t SEG_18[] = { { 120, 18, 0 } };
static const program_seg_t SEG_19[] = { { 120, 22, 0 } };
static const program_seg_t SEG_20[] = { { 120, 25, 0 } };
static const program_seg_t SEG_21[] = { { 120, 18, 0 } };
static const program_seg_t SEG_22[] = { { 120, 22, 0 } };
static const program_seg_t SEG_23[] = { { 120, 25, 0 } };
static const program_seg_t SEG_24[] = { { 8, 17, 0 }, { 8, 21, 0 }, { 8, 17, 0 }, { 8, 21, 0 }, { 8, 17, 0 }, { 8, 21, 0 }, { 8, 17, 0 }, { 8, 21, 0 }, { 8, 17, 0 }, { 8, 21, 0 }, { 40, 17, 0 } };
static const program_seg_t SEG_25[] = { { 8, 21, 0 }, { 8, 25, 0 }, { 8, 21, 0 }, { 8, 25, 0 }, { 8, 21, 0 }, { 8, 25, 0 }, { 8, 21, 0 }, { 8, 25, 0 }, { 8, 21, 0 }, { 8, 25, 0 }, { 40, 21, 0 } };
static const program_seg_t SEG_26[] = { { 8, 25, 0 }, { 8, 29, 0 }, { 8, 25, 0 }, { 8, 29, 0 }, { 8, 25, 0 }, { 8, 29, 0 }, { 8, 25, 0 }, { 8, 29, 0 }, { 8, 25, 0 }, { 8, 29, 0 }, { 40, 25, 0 } };
static const program_seg_t SEG_27[] = { { 6, 19, 0 }, { 6, 13, 0 }, { 6, 19, 0 }, { 6, 13, 0 }, { 6, 19, 0 }, { 60, 13, 0 }, { 18, 9, 0 }, { 12, 23, 0 } };
static const program_seg_t SEG_28[] = { { 6, 23, 0 }, { 6, 17, 0 }, { 6, 23, 0 }, { 6, 17, 0 }, { 6, 23, 0 }, { 60, 17, 0 }, { 18, 13, 0 }, { 12, 27, 0 } };
static const program_seg_t SEG_29[] = { { 6, 27, 0 }, { 6, 21, 0 }, { 6, 27, 0 }, { 6, 21, 0 }, { 6, 27, 0 }, { 60, 21, 0 }, { 18, 17, 0 }, { 12, 30, 0 } };
static const program_seg_t SEG_30[] = { { 12, 18, 0 }, { 12, 21, 0 }, { 12, 18, 0 }, { 12, 21, 0 }, { 12, 18, 0 }, { 12, 21, 0 }, { 12, 18, 0 }, { 12, 21, 0 }, { 12, 18, 0 }, { 12, 21, 0 } };
static const program_seg_t SEG_31[] = { { 12, 21, 0 }, { 12, 25, 0 }, { 12, 21, 0 }, { 12, 25, 0 }, { 12, 21, 0 }, { 12, 25, 0 }, { 12, 21, 0 }, { 12, 25, 0 }, { 12, 21, 0 }, { 12, 25, 0 } };
static const program_seg_t SEG_32[] = { { 12, 25, 0 }, { 12, 29, 0 }, { 12, 25, 0 }, { 12, 29, 0 }, { 12, 25, 0 }, { 12, 29, 0 }, { 12, 25, 0 }, { 12, 29, 0 }, { 12, 25, 0 }, { 12, 29, 0 } };
static const program_seg_t SEG_33[] = { { 30, 25, 0 }, { 30, 21, 0 }, { 30, 18, 0 }, { 30, 14, 0 } };
static const program_seg_t SEG_34[] = { { 12, 23, 0 }, { 12, 19, 0 }, { 12, 16, 0 }, { 12, 12, 0 }, { 12, 8, 0 } };
static const program_seg_t SEG_35[] = { { 24, 11, 0 }, { 24, 8, 0 }, { 24, 12, 0 }, { 24, 8, 0 }, { 24, 12, 0 } };
static const program_seg_t SEG_36[] = { { 120, 10, 0 } };
static const program_seg_t SEG_37[] = { { 60, 10, 0 } };
static const program_seg_t SEG_38[] = { { 120, 10, 0 } };
static const program_seg_t SEG_39[] = { { 60, 23, 0 } };
static const program_seg_t SEG_40[] = { { 60, 27, 0 } };
static const program_seg_t SEG_41[] = { { 60, 31, 0 } };
static const program_seg_t SEG_42[] = { { 13, 14, 0 }, { 2, 20, 0 }, { 12, 14, 0 }, { 2, 20, 0 }, { 12, 14, 0 }, { 2, 20, 0 }, { 17, 14, 0 } };
static const program_seg_t SEG_43[] = { { 13, 17, 0 }, { 2, 23, 0 }, { 12, 17, 0 }, { 2, 23, 0 }, { 12, 17, 0 }, { 2, 23, 0 }, { 17, 17, 0 } };
static const program_seg_t SEG_44[] = { { 13, 21, 0 }, { 2, 27, 0 }, { 12, 21, 0 }, { 2, 27, 0 }, { 12, 21, 0 }, { 2, 27, 0 }, { 17, 21, 0 } };
static const program_seg_t SEG_45[] = { { 60, 25, 0 } };
static const program_seg_t SEG_46[] = { { 6, 17, 0 }, { 6, 21, 0 }, { 6, 17, 0 }, { 6, 21, 0 }, { 6, 17, 0 }, { 6, 21, 0 }, { 6, 17, 0 }, { 6, 21, 0 }, { 6, 17, 0 }, { 6, 21, 0 } };
static const program_seg_t SEG_47[] = { { 4, 23, 0 }, { 4, 17, 0 }, { 4, 23, 0 }, { 4, 17, 0 }, { 4, 23, 0 }, { 20, 17, 0 }, { 12, 12, 0 }, { 8, 27, 0 } };
static const program_seg_t SEG_48[] = { { 10, 10, 0 }, { 10, 29, 0 }, { 10, 10, 0 }, { 10, 29, 0 }, { 10, 10, 0 }, { 10, 29, 0 } };
static const program_seg_t SEG_49[] = { { 10, 10, 0 }, { 10, 19, 0 }, { 10, 10, 0 }, { 10, 19, 0 }, { 10, 10, 0 }, { 10, 19, 0 }, { 10, 10, 0 }, { 10, 19, 0 }, { 10, 10, 0 }, { 10, 19, 0 }, { 10, 10, 0 }, { 10, 19, 0 } };
static const program_seg_t SEG_50[] = { { 2, 9, 0 }, { 2, 19, 0 }, { 2, 28, 0 }, { 2, 9, 0 }, { 2, 19, 0 }, { 2, 28, 0 }, { 2, 9, 0 }, { 2, 19, 0 }, { 2, 28, 0 }, { 2, 9, 0 }, { 2, 19, 0 }, { 2, 28, 0 }, { 2, 9, 0 }, { 2, 19, 0 }, { 2, 28, 0 }, { 2, 9, 0 }, { 2, 19, 0 }, { 2, 28, 0 } };
static const program_seg_t SEG_51[] = { { 4, 18, 0 }, { 4, 22, 0 }, { 4, 18, 0 }, { 4, 22, 0 }, { 4, 18, 0 }, { 4, 22, 0 }, { 4, 18, 0 }, { 4, 22, 0 }, { 4, 18, 0 }, { 4, 22, 0 }, { 20, 18, 0 } };
static const program_seg_t SEG_52[] = { { 12, 21, 0 }, { 12, 25, 0 }, { 12, 21, 0 }, { 12, 25, 0 }, { 12, 21, 0 }, { 12, 25, 0 }, { 12, 21, 0 }, { 12, 25, 0 }, { 12, 21, 0 }, { 12, 25, 0 } };
static const program_seg_t SEG_53[] = { { 6, 23, 0 }, { 6, 17, 0 }, { 6, 23, 0 }, { 6, 17, 0 }, { 6, 23, 0 }, { 60, 17, 0 }, { 18, 14, 0 }, { 12, 27, 0 } };

static const program_sub_t SUB_00[] = { { 40, 24 }, { 20, 23 }, { 40, 24 }, { 20, 23 }, { 60, 24 }, { 20, 23 }, { 80, 24 }, { 20, 23 } };
static const program_sub_t SUB_01[] = { { 60, 5 }, { 60, 7 }, { 60, 3 }, { 60, 5 }, { 60, 7 } };
static const program_sub_t SUB_02[] = { { 40, 20 }, { 40, 26 }, { 40, 20 }, { 40, 26 }, { 40, 20 }, { 40, 26 }, { 40, 20 }, { 40, 26 }, { 40, 20 }, { 40, 26 }, { 40, 20 }, { 40, 26 }, { 40, 20 }, { 40, 26 }, { 40, 27 } };
static const program_sub_t SUB_03[] = { { 40, 26 }, { 40, 15 }, { 40, 26 }, { 40, 15 }, { 40, 26 }, { 40, 15 }, { 40, 26 }, { 40, 15 }, { 40, 26 }, { 40, 15 }, { 40, 26 }, { 80, 18 }, { 80, 20 } };
static const program_sub_t SUB_04[] = { { 90, 25 }, { 45, 26 }, { 45, 30 }, { 90, 25 }, { 45, 26 }, { 45, 30 }, { 90, 25 }, { 45, 26 }, { 45, 30 }, { 30, 12 }, { 30, 13 } };
static const program_sub_t SUB_05[] = { { 60, 5 }, { 60, 7 }, { 60, 3 }, { 60, 5 }, { 60, 7 } };
static const program_sub_t SUB_06[] = { { 40, 26 }, { 40, 20 }, { 40, 27 }, { 40, 22 }, { 40, 21 }, { 40, 22 }, { 40, 21 }, { 40, 22 }, { 40, 21 }, { 40, 22 }, { 40, 21 }, { 80, 22 }, { 80, 21 } };
static const program_sub_t SUB_07[] = { { 40, 26 }, { 40, 16 }, { 40, 19 }, { 40, 16 }, { 40, 19 }, { 40, 16 }, { 40, 19 }, { 40, 16 }, { 40, 19 }, { 40, 16 }, { 40, 19 }, { 80, 18 }, { 80, 20 } };
static const program_sub_t SUB_08[] = { { 40, 26 }, { 40, 15 }, { 40, 26 }, { 40, 15 }, { 40, 26 }, { 40, 15 }, { 40, 26 }, { 40, 15 }, { 40, 26 }, { 40, 15 }, { 40, 26 }, { 80, 18 }, { 80, 20 } };
static const program_sub_t SUB_09[] = { { 30, 25 }, { 30, 26 }, { 30, 25 }, { 30, 26 }, { 30, 25 }, { 30, 22 }, { 30, 21 }, { 30, 22 }, { 30, 21 }, { 30, 26 }, { 60, 27 }, { 30, 17 }, { 30, 18 }, { 30, 20 }, { 30, 10 }, { 30, 12 }, { 30, 8 }, { 30, 4 }, { 30, 6 } };
static const program_sub_t SUB_10[] = { { 60, 26 }, { 30, 18 }, { 30, 25 }, { 60, 26 }, { 30, 18 }, { 30, 25 }, { 60, 26 }, { 30, 18 }, { 30, 25 }, { 60, 26 }, { 30, 18 }, { 30, 25 }, { 60, 26 }, { 30, 18 }, { 30, 25 } };
static const program_sub_t SUB_11[] = { { 0, 29 } };
static const program_sub_t SUB_12[] = { { 0, 13 } };
static const program_sub_t SUB_13[] = { { 0, 17 } };
static const program_sub_t SUB_14[] = { { 0, 14 } };
static const program_sub_t SUB_15[] = { { 0, 28 } };
static const program_sub_t SUB_16[] = { { 0, 32 } };
static const program_sub_t SUB_17[] = { { 0, 32 } };
static const program_sub_t SUB_18[] = { { 0, 32 } };
static const program_sub_t SUB_19[] = { { 0, 32 } };
static const program_sub_t SUB_20[] = { { 0, 32 } };
static const program_sub_t SUB_21[] = { { 0, 32 } };

const program_info_t PROGRAM_INFO[PROGRAM_COUNT] = {
    { "Tonification - Bras", true, 2, 0, { 300, 300, 300 }, { { SEG_00, 1, 0 }, { SEG_01, 1, 1 }, { SEG_02, 1, 2 } }, SUB_00, 8 },
    { "Tonification - Poitrine Bras", true, 1, 0, { 300, 300, 300 }, { { SEG_03, 2, 3 }, { SEG_04, 2, 4 }, { SEG_05, 2, 5 } }, SUB_01, 5 },
    { "Tonification - Sangle abdominale", true, 6, 0, { 600, 600, 600 }, { { SEG_06, 1, 6 }, { SEG_07, 2, 7 }, { SEG_08, 1, 8 } }, SUB_02, 15 },
    { "Tonification - Cuisses Fessiers", true, 7, 0, { 600, 600, 600 }, { { SEG_09, 11, 9 }, { SEG_10, 11, 10 }, { SEG_11, 11, 11 } }, SUB_03, 13 },
    { "Tonification - Complet", true, 4, 0, { 600, 600, 600 }, { { SEG_12, 7, 12 }, { SEG_13, 7, 13 }, { SEG_14, 7, 14 } }, SUB_04, 11 },
    { "Remodelage - Poitrine Bras", true, 1, 0, { 300, 300, 300 }, { { SEG_15, 2, 15 }, { SEG_16, 2, 16 }, { SEG_17, 2, 17 } }, SUB_05, 5 },
    { "Remodelage - Sangle abdominale", true, 8, 0, { 600, 600, 600 }, { { SEG_18, 1, 18 }, { SEG_19, 1, 19 }, { SEG_20, 1, 20 } }, SUB_06, 13 },
    { "Remodelage - Cuisses Fessiers 1", true, 9, 0, { 600, 600, 600 }, { { SEG_21, 1, 21 }, { SEG_22, 1, 22 }, { SEG_23, 1, 23 } }, SUB_07, 13 },
    { "Remodelage - Cuisses Fessiers 2", true, 7, 0, { 600, 600, 600 }, { { SEG_24, 11, 24 }, { SEG_25, 11, 25 }, { SEG_26, 11, 26 } }, SUB_08, 13 },
    { "Remodelage - Complet", true, 5, 3, { 600, 600, 600 }, { { SEG_27, 8, 27 }, { SEG_28, 8, 28 }, { SEG_29, 8, 29 } }, SUB_09, 19 },
    { "Anti-cellulite - Cuisses Fessiers", true, 10, 0, { 600, 600, 600 }, { { SEG_30, 10, 30 }, { SEG_31, 10, 31 }, { SEG_32, 10, 32 } }, SUB_10, 15 },
    { "Gainage - Complet 1", false, 0, 0, { 120, 300, 600 }, { { SEG_33, 4, 33 }, { SEG_34, 5, 34 }, { SEG_35, 5, 35 } }, SUB_11, 1 },
    { "Gainage - Complet 2", false, 0, 0, { 120, 300, 600 }, { { SEG_36, 1, 36 }, { SEG_37, 1, 37 }, { SEG_38, 1, 38 } }, SUB_12, 1 },
    { "Relaxation Drainage - Drainage", false, 0, 0, { 300, 300, 300 }, { { SEG_39, 1, 39 }, { SEG_40, 1, 40 }, { SEG_41, 1, 41 } }, SUB_13, 1 },
    { "Relaxation Drainage - Relax 1", false, 0, 0, { 300, 300, 300 }, { { SEG_42, 7, 42 }, { SEG_43, 7, 43 }, { SEG_44, 7, 44 } }, SUB_14, 1 },
    { "Relaxation Drainage - Relax 2", false, 0, 0, { 300, 300, 300 }, { { SEG_45, 1, 45 }, { SEG_46, 10, 46 }, { SEG_47, 8, 47 } }, SUB_15, 1 },
    { "Proprioception - 1", false, 0, 0, { 60, 60, 60 }, { { SEG_48, 6, 48 }, { SEG_48, 6, 48 }, { SEG_48, 6, 48 } }, SUB_16, 1 },
    { "Proprioception - 2", false, 0, 0, { 120, 120, 120 }, { { SEG_49, 12, 49 }, { SEG_49, 12, 49 }, { SEG_49, 12, 49 } }, SUB_17, 1 },
    { "Proprioception - 3", false, 0, 0, { 180, 180, 180 }, { { SEG_50, 18, 50 }, { SEG_50, 18, 50 }, { SEG_50, 18, 50 } }, SUB_18, 1 },
    { "Proprioception - 4", false, 0, 0, { 300, 300, 300 }, { { SEG_51, 11, 51 }, { SEG_51, 11, 51 }, { SEG_51, 11, 51 } }, SUB_19, 1 },
    { "Proprioception - 5", false, 0, 0, { 600, 600, 600 }, { { SEG_52, 10, 52 }, { SEG_52, 10, 52 }, { SEG_52, 10, 52 } }, SUB_20, 1 },
    { "Proprioception - 6", false, 0, 0, { 600, 600, 600 }, { { SEG_53, 8, 53 }, { SEG_53, 8, 53 }, { SEG_53, 8, 53 } }, SUB_21, 1 },
};
