#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t x, y;
    uint16_t w, h;
} bitui_rect_t;

typedef struct {
    uint16_t width, height;
    uint8_t *framebuffer;

    bool color;
    bitui_rect_t dirty;
} bitui_ctx_t;

typedef bitui_ctx_t *bitui_t;

void bitui_hline(bitui_t ctx, uint16_t y, uint16_t x1, uint16_t x2);
void bitui_vline(bitui_t ctx, uint16_t x, uint16_t y1, uint16_t y2);

void bitui_rect(bitui_t ctx, bitui_rect_t rect);
void bitui_rrect(bitui_t ctx, bitui_rect_t rect, bitui_rect_t radius);

void bitui_paste(bitui_t ctx, const uint8_t *src_bitmap, bitui_rect_t src, uint16_t dst_x, uint16_t dst_y);
