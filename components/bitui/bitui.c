#include "bitui.h"
#include <assert.h>

static inline void bitui_colorize(bitui_t ctx, uint16_t offset, uint8_t updated_pixels_mask) {
    uint8_t temp = ctx->framebuffer[offset] & ~updated_pixels_mask;
    if (ctx->color) temp |= updated_pixels_mask;
    ctx->framebuffer[offset] = temp;
}

void bitui_hline(bitui_t ctx, const uint16_t y, uint16_t x1, uint16_t x2) {
    assert(x1 <= x2);
    // [not aligned][aligned][not aligned]

    const uint16_t row_start = y * ctx->width;
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
        uint8_t mask = (((uint8_t)0xff) >> x1_rem) ^ (((uint8_t)0xff) >> x2_rem);
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
            mask = (0xff << (8 - x2_rem));
            bitui_colorize(ctx, row_start + x1_aligned, mask);
        }
    }
}

void bitui_vline(bitui_t ctx, uint16_t x, uint16_t y1, uint16_t y2)
{
    assert(y1 <= y2);

    const uint8_t col = x / 8;
    const uint8_t mask = 1 << (7 - (x & 7));
    const uint16_t w = ctx->width;

    for (; y1 <= y2; ++y1) {
        bitui_colorize(ctx, y1 * w + col, mask);
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
