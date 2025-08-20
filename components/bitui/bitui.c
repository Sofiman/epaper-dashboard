#include "bitui.h"
#include <assert.h>
#include <string.h>

#define SWAP_U16(A,B) do { \
    uint16_t __tmp = (A); \
    (A) = (B); \
    (B) = __tmp; \
} while (0)

void bitui_clear(bitui_t ctx, bool color) {
    size_t count;
#ifndef BITUI_SWAP_XY
    count = ctx->stride * ctx->height;
#else
    count = ctx->stride * ctx->width;
#endif
    memset(ctx->framebuffer, color ? 0xff : 0, count);
}

static inline void bitui_merge_rect(bitui_rect_t *dst, const bitui_rect_t src) {
    if (src.w == 0 || src.h == 0)
        return;
    if (dst->w == 0 || dst->h == 0) {
        *dst = src;
        return;
    }

    const uint16_t dst_left   = dst->x;
    const uint16_t dst_top    = dst->y;
    const uint16_t dst_right  = dst->x + dst->w - 1;
    const uint16_t dst_bottom = dst->y + dst->h - 1;

    const uint16_t src_left   = src.x;
    const uint16_t src_top    = src.y;
    const uint16_t src_right  = src.x + src.w - 1;
    const uint16_t src_bottom = src.y + src.h - 1;

    dst->x = dst_left < src_left ? dst_left : src_left;
    dst->y =  dst_top < src_top  ?  dst_top :  src_top;
    dst->w = ( dst_right >  src_right ?  dst_right :  src_right) - dst->x + 1;
    dst->h = (dst_bottom > src_bottom ? dst_bottom : src_bottom) - dst->y + 1;
}

static inline void bitui_colorize(bitui_t ctx, uint16_t offset, uint8_t updated_pixels_mask) {
    uint8_t temp = ctx->framebuffer[offset] & ~updated_pixels_mask;
    if (ctx->color) temp |= updated_pixels_mask;
    ctx->framebuffer[offset] = temp;
}

#ifdef BITUI_ROTATION
#define bitui_rotate(Ctx, X, Y) do { \
    bitui_point_t p = { .x = *(X), .y = *(Y) }; \
    p = bitui_apply_rot((Ctx), p); \
    *(X) = p.x; \
    *(Y) = p.y; \
} while (0);

bitui_point_t bitui_apply_rot(bitui_t ctx, bitui_point_t point) {
    _Static_assert(BITUI_ROT_270 == (BITUI_ROT_090 | BITUI_ROT_180), "Rotation bitwise composition");
    if (ctx->rot & BITUI_ROT_090) {
        uint16_t t = point.x;
        point.x = ctx->width - 1 - point.y;
        point.y = t;
    }
    if (ctx->rot & BITUI_ROT_180) {
        point.x = ctx->width - 1 - point.x;
        point.y = ctx->height - 1 - point.y;
    }
    return point;
}
#else
#define bitui_rotate(Ctx, X, Y)
#endif

#ifndef BITUI_SWAP_XY
#define ROW_AT(X, Y) (Y)
#define STRIDE(Ctx) (ctx->stride)
#define COL_AT(X, Y) ((X)/8)
#define BIT_AT(X, Y) (0x80 >> ((X) & 7))
#else
#define ROW_AT(X, Y) ((Y)/8)
#define STRIDE(Ctx) (ctx->width)
#define COL_AT(X, Y) (X)
#define BIT_AT(X, Y) (0x80 >> ((Y) & 7))
#endif
#define IDX_AT(Ctx, X, Y) (ROW_AT(X, Y) * STRIDE(Ctx) + COL_AT(X, Y))

void bitui_point(bitui_t ctx, uint16_t x, uint16_t y) {
    bitui_rotate(ctx, &x, &y);

    if (x >= ctx->width || y >= ctx->height)
        return;

    bitui_colorize(ctx, IDX_AT(ctx, x, y), BIT_AT(x, y));
}

#ifndef BITUI_SWAP_XY
void bitui_hline(bitui_t ctx, const uint16_t y, uint16_t x1, uint16_t x2) {
    if (x1 > x2) SWAP_U16(x1, x2);

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
    if (y1 > y2) SWAP_U16(y1, y2);

    bitui_merge_rect(&ctx->dirty, (bitui_rect_t){ .x = x, .y = y1, .w = 1, .h = y2-y1 });

    const uint8_t col = x / 8;
    const uint8_t mask = 0x80 >> (x & 7);
    const uint16_t s = ctx->stride;

    for (; y1 <= y2; ++y1) {
        bitui_colorize(ctx, y1 * s + col, mask);
    }
}
#else
void bitui_vline(bitui_t ctx, const uint16_t x, uint16_t y1, uint16_t y2) {
    if (y1 > y2) SWAP_U16(y1, y2);

    // TODO : bitui_merge_rect(&ctx->dirty, (bitui_rect_t){ .x = x1, .y = y, .w = x2-x1, .h = 1 });
    // [not aligned][aligned][not aligned]

    uint16_t y1_aligned = y1 / 8;
    const uint16_t y1_rem = y1 & 7;
    const uint16_t y2_aligned = y2 / 8;
    const uint16_t y2_rem = y2 & 7;
    if (y1_aligned == y2_aligned) {
        uint8_t mask = (0xff >> y1_rem) & ~(0xff >> y2_rem);
        bitui_colorize(ctx, y1_aligned * ctx->width + x, mask);
    } else {
        uint8_t mask = (0xff >> y1_rem);
        bitui_colorize(ctx, y1_aligned * ctx->width + x, mask);
        y1_aligned += 1;

        const uint8_t fill = ctx->color ? 0xff : 0x00;
        for (; y1_aligned < y2_aligned; ++y1_aligned) {
            ctx->framebuffer[y1_aligned * ctx->width + x] = fill;
        }

        if (y2_rem) {
            mask = ~(0xff >> y2_rem);
            bitui_colorize(ctx, y1_aligned * ctx->width + x, mask);
        }
    }
}

void bitui_hline(bitui_t ctx, uint16_t y, uint16_t x1, uint16_t x2)
{
    if (x1 > x2) SWAP_U16(x1, x2);

    // TODO: bitui_merge_rect(&ctx->dirty, (bitui_rect_t){ .x = x, .y = y1, .w = 1, .h = y2-y1 });

    for (; x1 <= x2; ++x1) {
        bitui_colorize(ctx, IDX_AT(ctx, x1, y), BIT_AT(x1, y));
    }
}
#endif

void bitui_line(bitui_t ctx, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    bitui_rotate(ctx, &x1, &y1);
    bitui_rotate(ctx, &x2, &y2);

    if (x1 == x2) bitui_vline(ctx, x1, y1, y2);
    else if (y1 == y2) bitui_hline(ctx, y1, x1, x2);
    else assert(0 && "Unsupported non axis aligned lines");
}

void bitui_rect(bitui_t ctx, const bitui_rect_t rect) {
    const uint16_t left   = rect.x;
    const uint16_t top    = rect.y;
    const uint16_t right  = rect.x + rect.w - 1;
    const uint16_t bottom = rect.y + rect.h - 1;

    bitui_line(ctx,  left,    top, right,    top);
    bitui_line(ctx,  left,    top,  left, bottom);
    bitui_line(ctx, right,    top, right, bottom);
    bitui_line(ctx,  left, bottom, right, bottom);
}

void bitui_paste_bitstream(bitui_t ctx, const uint8_t *src_bitstream, uint16_t src_w, uint16_t src_h, const uint16_t dst_x, const uint16_t dst_y)
{
    uint8_t bits = 0;
    uint8_t bit = 0;
    bitui_merge_rect(&ctx->dirty, (bitui_rect_t){ .x = dst_x, .y = dst_y, .w = src_w, .h = src_h });

    ctx->color = !ctx->color;
    for (uint16_t y = dst_y; y < dst_y + src_h; y++) {
        for (uint16_t x = dst_x; x < dst_x + src_w; x++) {
            if (!(bit & 7))
                bits = *(src_bitstream++);

            if (bits & 0x80)
                bitui_point(ctx, x, y);

            bits <<= 1;
            ++bit;
        }
    }
    ctx->color = !ctx->color;
}

void bitui_paste_bitmap(bitui_t ctx, const uint8_t *src_bitmap, uint16_t src_w, uint16_t src_h, uint16_t dst_x, uint16_t dst_y)
{
    bitui_merge_rect(&ctx->dirty, (bitui_rect_t){ .x = dst_x, .y = dst_y, .w = src_w, .h = src_h });
    const uint16_t stride = (src_w - 1) /8 + 1;

    for (uint16_t dy = 0; dy < src_h; dy++) {
        for (uint16_t dx = 0; dx < src_w; dx++) {
            if (!(src_bitmap[dy * stride + (dx / 8)] & (0x80 >> (dx & 7))))
                bitui_point(ctx, dst_x + dx, dst_y + dy);
        }
    }
}
