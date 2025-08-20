#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t x, y;
} bitui_point_t;

typedef struct {
    uint16_t x, y;
    uint16_t w, h;
} bitui_rect_t;

#ifdef BITUI_ROTATION
typedef enum {
    BITUI_ROT_000 = 0x00,
    BITUI_ROT_090 = 0x01,
    BITUI_ROT_180 = 0x02,
    BITUI_ROT_270 = 0x03,
} bitui_rot;
#define BITUI_ROT_INVERT(Rot) ((Rot) ^ BITUI_ROT_270)
#endif

typedef struct {
    uint16_t width, height, stride;
    uint8_t *framebuffer;

#ifdef BITUI_ROTATION
    bitui_rot rot;
#endif
    bool color;
    bitui_rect_t dirty;
} bitui_ctx_t;

typedef bitui_ctx_t *bitui_t;

void bitui_clear(bitui_t ctx, bool color);

#ifdef BITUI_ROTATION
bitui_point_t bitui_apply_rot(bitui_t ctx, bitui_point_t point);
#endif

void bitui_hline(bitui_t ctx, uint16_t y, uint16_t x1, uint16_t x2);
void bitui_vline(bitui_t ctx, uint16_t x, uint16_t y1, uint16_t y2);

void bitui_point(bitui_t ctx, uint16_t x, uint16_t y);
void bitui_line(bitui_t ctx, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

void bitui_rect(bitui_t ctx, bitui_rect_t rect);
void bitui_rrect(bitui_t ctx, bitui_rect_t rect, bitui_rect_t radius);

void bitui_paste_bitmap(bitui_t ctx, const uint8_t *src_bitmap, uint16_t src_w, uint16_t src_h, uint16_t dst_x, uint16_t dst_y);
void bitui_paste_bitstream(bitui_t ctx, const uint8_t *src_bitstream, uint16_t src_w, uint16_t src_h, uint16_t dst_x, uint16_t dst_y);
