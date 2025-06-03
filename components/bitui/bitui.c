#include "bitui.h"
#include <assert.h>

void bitui_hline(bitui_t ctx, const uint16_t y, uint16_t x1, uint16_t x2) {
    assert(x1 <= x2);
    // [not aligned][aligned][not aligned]

    const uint16_t row_start = y * ctx->width;
    uint16_t aligned_x1 = x1 / 8;
    uint16_t aligned_x2 = x2 / 8;
    if (aligned_x1 == aligned_x2) {
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
        uint8_t mask = TODO;
        uint8_t temp = ctx->framebuffer[row_start + aligned_x1] & mask;
        if (ctx->color) temp |= mask;
        ctx->framebuffer[row_start + aligned_x1] = temp;
    } else {
        uint8_t mask = (0xff >> x1);
        uint8_t temp = ctx->framebuffer[row_start + aligned_x1] & mask;
        if (ctx->color) temp |= mask;
        ctx->framebuffer[row_start + aligned_x1] = temp;
        aligned_x1 += 1;


        uint8_t fill = ctx->color ? 0xff : 0x00;
        for (; aligned_x1 < aligned_x2; ++aligned_x1) {
            ctx->framebuffer[row_start + aligned_x1] = fill;
        }

        if (x2 & 7) {
            mask = (0xff << (8 - (x2 & 7)));
            temp = ctx->framebuffer[row_start + aligned_x1] & mask;
            if (ctx->color) temp |= mask;
            ctx->framebuffer[row_start + aligned_x1] = temp;
        }
    }
}

void bitui_vline(bitui_t ctx, uint16_t x, uint16_t y1, uint16_t y2)
{
    assert(y1 <= y2);
    // [not aligned][aligned][not aligned]

    const uint8_t col = x / 8;
    const uint8_t mask = 1 << (7 - (x & 7));

    uint8_t temp;
    while (y1 <= y2) {
        temp = ctx->framebuffer[y1 * ctx->width + col] & (~mask);
        if (ctx->color) temp |= mask;
        ctx->framebuffer[y1 * ctx->width + col] = temp;

        ++y1;
    }
}

void bitui_rect(bitui_t ctx, bitui_rect_t rect) {
    bitui_hline(ctx, rect.y,          rect.x, rect.x + rect.w);
    bitui_hline(ctx, rect.y + rect.h, rect.x, rect.x + rect.w);
    //bitui_vline(ctx, rect.x,          rect.y + 1, rect.y + rect.h - 1);
    //bitui_vline(ctx, rect.x + rect.w, rect.y + 1, rect.y + rect.h - 1);
}
