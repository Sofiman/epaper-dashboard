#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t x, y;
    uint16_t w, h;
} bitui_rect_t;

typedef enum {
    BITUI_ROT_000 = 0x00,
    BITUI_ROT_090 = 0x01,
    BITUI_ROT_180 = 0x02,
    BITUI_ROT_270 = 0x03,
} bitui_rot;

typedef struct {
    uint16_t width, height, stride;
    uint8_t *framebuffer;

    bitui_rot rot;
    bool color;
    bitui_rect_t dirty;
} bitui_ctx_t;

typedef bitui_ctx_t *bitui_t;

void bitui_point(bitui_t ctx, uint16_t x, uint16_t y);
void bitui_hline(bitui_t ctx, uint16_t y, uint16_t x1, uint16_t x2);
void bitui_vline(bitui_t ctx, uint16_t x, uint16_t y1, uint16_t y2);

void bitui_rect(bitui_t ctx, bitui_rect_t rect);
void bitui_rrect(bitui_t ctx, bitui_rect_t rect, bitui_rect_t radius);

void bitui_paste_bitstream(bitui_t ctx, const uint8_t *src_bitstream, uint16_t src_w, uint16_t src_h, uint16_t dst_x, uint16_t dst_y);
