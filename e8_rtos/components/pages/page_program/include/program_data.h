/*
 * program_data.h - Kiểu dữ liệu 22 bài Programmes (số liệu ở program_data.c, sinh bởi tools/programmes.py)
 *
 * program_seg_t: đoạn cột liên tiếp của biểu đồ (số cột, mức cao, mức thấp; thấp = 0 -> mọi cột mức cao,
 * khác 0 -> cột xen kẽ cao / thấp bắt đầu bằng cao). Mỗi cột = 5 s (trục 10 phút) hoặc 1 s (bài <= 2 phút).
 * program_sub_t: bài tập con (số giây, hình minh họa 1..32 = icon 0500..0531; giây 0 = cả bài một hình).
 * program_profile_t: biểu đồ của một mức độ, chart = số thứ tự ảnh 0..53 (icon 0800 + chart xám,
 * 0860 + chart xanh).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PROGRAM_COUNT             22
#define PROGRAM_LEVELS            3
#define PROGRAM_SLOTS             120
#define ICON_PROFILE_GRAY_BASE    800
#define ICON_PROFILE_GREEN_BASE   860
#define ICON_COMB_BASE            920
#define COMB_LEVELS               32

typedef struct {
    uint8_t bars;
    uint8_t high;
    uint8_t low;
} program_seg_t;

typedef struct {
    uint16_t seconds;
    uint8_t  illus;
} program_sub_t;

typedef struct {
    const program_seg_t *segs;
    uint8_t              nsegs;
    uint8_t              chart;
} program_profile_t;

typedef struct {
    const char          *name;
    bool                 has_x;
    uint8_t              grid;
    uint8_t              grid2;
    uint16_t             seconds[PROGRAM_LEVELS];
    program_profile_t    profile[PROGRAM_LEVELS];
    const program_sub_t *subs;
    uint8_t              nsubs;
} program_info_t;

extern const program_info_t PROGRAM_INFO[PROGRAM_COUNT];
