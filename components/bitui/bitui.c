#include "bitui.h"
#include <assert.h>
#include <string.h>

void bitui_clear(bitui_t ctx, bool color) {
    memset(ctx->framebuffer, color ? 0xff : 0, ctx->stride * ctx->height);
}

static inline void bitui_merge_rect(bitui_rect_t *dst, const bitui_rect_t src) {
    if (dst->x > src.x) dst->x = src.x;
    if (dst->y > src.y) dst->y = src.y;
    if (dst->w < src.w) dst->w = src.w;
    if (dst->h < src.h) dst->h = src.h;
}

static inline void bitui_colorize(bitui_t ctx, uint16_t offset, uint8_t updated_pixels_mask) {
    uint8_t temp = ctx->framebuffer[offset] & ~updated_pixels_mask;
    if (ctx->color) temp |= updated_pixels_mask;
    ctx->framebuffer[offset] = temp;
}

void bitui_point(bitui_t ctx, uint16_t x, uint16_t y) {
    _Static_assert(BITUI_ROT_270 == (BITUI_ROT_090 | BITUI_ROT_180), "Rotation bitwise composition");
    if (ctx->rot & BITUI_ROT_090) {
        uint16_t t = x;
        x = ctx->width - 1 - y;
        y = t;
    }
    if (ctx->rot & BITUI_ROT_180) {
        x = ctx->width - 1 - x;
        y = ctx->height - 1 - y;
    }
    if (x >= ctx->width || y >= ctx->height)
        return;
    bitui_colorize(ctx, y * ctx->stride + x/8, 0x80 >> (x & 7));
}

void bitui_hline(bitui_t ctx, const uint16_t y, uint16_t x1, uint16_t x2) {
    assert(x1 <= x2);
    bitui_merge_rect(&ctx->dirty, (bitui_rect_t){ .x = x1, .y = y, .w = x2-x1, .h = 1 });
    // [not aligned][aligned][not aligned]

    const uint16_t row_start = y * ctx->stride;
    uint16_t x1_aligned = x1 / 8;
    const uint16_t x1_rem = x1 & 7;
    const uint16_t x2_aligned = x2 / 8;
    const uint16_t x2_rem = x2 & 7;
    if (x1_aligned == x2_aligned) {
        // x1 = 0
        // x2 = 3
        // ****....
        // ^  ^
        // x1 x2
        // mask = (0xff >> 0)

        // x1 = 3
        // x2 = 7
        // ....****
        //     ^  ^
        //     x1 x2
        // TODO : Xor should also work (and one less op) but I'm not sure at 100%
        uint8_t mask = (0xff >> x1_rem) & ~(0xff >> x2_rem);
        bitui_colorize(ctx, row_start + x1_aligned, mask);
    } else {
        uint8_t mask = (0xff >> x1_rem);
        bitui_colorize(ctx, row_start + x1_aligned, mask);
        x1_aligned += 1;

        const uint8_t fill = ctx->color ? 0xff : 0x00;
        for (; x1_aligned < x2_aligned; ++x1_aligned) {
            ctx->framebuffer[row_start + x1_aligned] = fill;
        }

        if (x2_rem) {
            mask = ~(0xff >> x2_rem);
            bitui_colorize(ctx, row_start + x1_aligned, mask);
        }
    }
}

void bitui_vline(bitui_t ctx, uint16_t x, uint16_t y1, uint16_t y2)
{
    assert(y1 <= y2);
    bitui_merge_rect(&ctx->dirty, (bitui_rect_t){ .x = x, .y = y1, .w = 1, .h = y2-y1 });

    const uint8_t col = x / 8;
    const uint8_t mask = 0x80 >> (x & 7);
    const uint16_t s = ctx->stride;

    for (; y1 <= y2; ++y1) {
        bitui_colorize(ctx, y1 * s + col, mask);
    }
}

void bitui_rect(bitui_t ctx, bitui_rect_t rect) {
    bitui_hline(ctx, rect.y,              rect.x, rect.x + rect.w);
    bitui_hline(ctx, rect.y + rect.h - 1, rect.x, rect.x + rect.w);
    if (rect.h > 2) {
        bitui_vline(ctx, rect.x,          rect.y + 1, rect.y + rect.h - 2);
        bitui_vline(ctx, rect.x + rect.w - 1, rect.y + 1, rect.y + rect.h - 2);
    }
}

void bitui_paste_bitstream(bitui_t ctx, const uint8_t *src_bitstream, uint16_t src_w, uint16_t src_h, const uint16_t dst_x, const uint16_t dst_y)
{
    uint8_t bits = 0;
    uint8_t c = 0;
    bitui_merge_rect(&ctx->dirty, (bitui_rect_t){ .x = dst_x, .y = dst_y, .w = src_w, .h = src_h });

    ctx->color = !ctx->color;
    for (uint16_t y = dst_y; y < dst_y + src_h; y++) {
        for (uint16_t x = dst_x; x < dst_x + src_w; x++) {
            if ((c & 7) == 0) bits = src_bitstream[c/8];

            if (bits & 0x80)
                bitui_point(ctx, x, y);

            bits <<= 1;
            ++c;
        }
    }
    ctx->color = !ctx->color;
}
