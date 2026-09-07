#include "bitui.h"
#include <assert.h>
#include <string.h>
#include <limits.h>

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

static inline void bitui_colorize32(uint32_t color, uint32_t *pix, uint32_t updated_pixels_mask) {
    // https://graphics.stanford.edu/~seander/bithacks.html#ConditionalSetOrClearBitsWithoutBranching
    *pix ^= (color ^ *pix) & updated_pixels_mask;
}

#ifdef BITUI_ROTATION
#define bitui_rotate(Ctx, X, Y) do { \
    bitui_point_t p = { .x = *(X), .y = *(Y) }; \
    p = bitui_apply_rot((Ctx), p); \
    *(X) = p.x; \
    *(Y) = p.y; \
} while (0)

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
#define BIT_AT(X, Y) (0x80U >> ((X) & 7))
#else
#define ROW_AT(X, Y) ((Y)/8)
#define STRIDE(Ctx) (ctx->width)
#define COL_AT(X, Y) (X)
#define BIT_AT(X, Y) (0x80U >> ((Y) & 7))
#endif
#define IDX_AT(Ctx, X, Y) (ROW_AT(X, Y) * STRIDE(Ctx) + COL_AT(X, Y))

void bitui_point(bitui_t ctx, uint16_t x, uint16_t y) {
    bitui_rotate(ctx, &x, &y);

    if (x >= ctx->width || y >= ctx->height)
        return;

    bitui_colorize(ctx, IDX_AT(ctx, x, y), BIT_AT(x, y));
}

#ifndef BITUI_SWAP_XY
static inline uint32_t bswapped_srl(uint32_t k) {
    // Computes bswap32(0xffffffff >> (k % 32)) with the minimum number of instructions
#if defined(__riscv_zbb) || __ARM_ARCH >= 6 || defined(__x86_64__) || defined(__aarch64__)
    return __builtin_bswap32(0xffffffff >> (k & 31));
#else
    return ((0xffU >> (k & 7)) | 0xffffff00U) << (k & 24);
#endif
}

__attribute__((optimize("O2"))) /* Using -Os actually increases the code size and leads to slower code.
                                   The O2 flag leads to the best code layout and the fastest implementation. */
void bitui_hline_fast(bitui_t ctx, const uint16_t y, uint16_t x1, uint16_t x2) {
    if (x1 > x2) SWAP_U16(x1, x2);
    x2 += 1;

    // [partial fill][full fill][partial fill]

    uint8_t * const framebuffer = ctx->framebuffer + y * ctx->stride;
    uint32_t x1_rem = bswapped_srl(x1);
    const uint32_t color = -ctx->color;
    uint32_t x2_rem = ~bswapped_srl(x2);

    uint32_t x1_aligned = (x1 / (sizeof(uint32_t) * CHAR_BIT)) * sizeof(uint32_t);
    uint32_t x2_aligned = (x2 / (sizeof(uint32_t) * CHAR_BIT)) * sizeof(uint32_t);
    uint32_t *pix = (uint32_t *)(framebuffer + x1_aligned);

    if (x1_aligned == x2_aligned) {
        bitui_colorize32(color, pix, x1_rem & x2_rem);
    } else {
        bitui_colorize32(color, pix, x1_rem);
        pix++;

        uint32_t *end = (uint32_t *)(framebuffer + x2_aligned);
        bitui_colorize32(color, end, x2_rem);

        // Fill
#if 0 && defined(BITUI_NO_UNROLL_LOOPS) /* Not worth it, code size too big for a small improvement */
        uint32_t *rem =  pix + (uint32_t)(end - pix) % 4;

        while (pix < rem) {
            *pix = color;
            pix++;
        }

        while (pix < end) {
            pix[3] = color;
            pix[2] = color;
            pix[1] = color;
            pix[0] = color;
            pix += 4;
        }
#else
        do {
            *pix++ = color;
        } while (pix < end);
#endif
     }
}

void bitui_vline_fast(bitui_t ctx, uint16_t x, uint16_t y1, uint16_t y2) {
    if (y1 > y2) SWAP_U16(y1, y2);

    const uint32_t s = ctx->stride;
    const uint32_t col = x / 8;
    const uint32_t mask = 0x80 >> (x & 7);

    uint8_t *pix = &ctx->framebuffer[y1 * s + col];
    uint8_t * const end = &ctx->framebuffer[y2 * s + col];

    // This function is memory bound because of the column memory access pattern required for this
    // operation. After profiling with ESP32C6 performance counters, there is 115440 load hazards.
    // However, by manually unrolling the loop in blocks of 4 to use the CPU superscalar
    // capabilities, we reduce the benchmark best case scenario time from 4513us us to 3484us (-22%)
    // but inrecase the code size y ~230 bytes. Furthermore, only 1200 load hazards occur and the
    // number of idle cycles increased from 2884 to 4320.
    //
    // Further unrolling the loop in blocks of 8 lead to equal performance but bigger code size
    // (+406 bytes), it is not worth it.
    if (ctx->color) {
#ifndef BITUI_NO_UNROLL_LOOPS
       for (uint32_t blocks = (y2 - y1) / 4; blocks-- > 0; ) {
           *pix = *pix | mask; pix += s;
           *pix = *pix | mask; pix += s;
           *pix = *pix | mask; pix += s;
           *pix = *pix | mask; pix += s;
       }
#endif

       for (; pix <= end; pix += s) {
           *pix = *pix | mask;
       }
    } else {
#ifndef BITUI_NO_UNROLL_LOOPS
       for (uint32_t blocks = (y2 - y1) / 4; blocks-- > 0; ) {
           *pix = *pix & ~mask; pix += s;
           *pix = *pix & ~mask; pix += s;
           *pix = *pix & ~mask; pix += s;
           *pix = *pix & ~mask; pix += s;
       }
#endif

       for (; pix <= end; pix += s) {
           *pix = *pix & ~mask;
       }
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

    if (y1 == y2) bitui_hline_fast(ctx, y1, x1, x2);
    else if (x1 == x2) bitui_vline_fast(ctx, x1, y1, y2);
    else assert(0 && "Unsupported non axis aligned lines");
}

void bitui_rect_(bitui_t ctx, const bitui_rect_t rect) {
    const uint16_t left   = rect.x;
    const uint16_t top    = rect.y;
    const uint16_t right  = rect.x + rect.w - 1;
    const uint16_t bottom = rect.y + rect.h - 1;

    bitui_hline_fast(ctx,    top, left,  right);
    bitui_hline_fast(ctx, bottom, left,  right);
    bitui_vline_fast(ctx,   left,  top + 1, bottom - 1);
    bitui_vline_fast(ctx,  right,  top + 1, bottom - 1);
}

static inline void bitui_colorize8(uint32_t color, uint8_t *pix, uint8_t updated_pixels_mask) {
    // https://graphics.stanford.edu/~seander/bithacks.html#ConditionalSetOrClearBitsWithoutBranching
    *pix ^= (color ^ *pix) & updated_pixels_mask;
}

void bitui_paste_bitstream(bitui_ctx_t *ctx, const uint8_t *src_bitstream, uint16_t src_w, uint16_t src_h, const uint16_t dst_x, const uint16_t dst_y)
{
    if (src_w == 0 || src_h == 0) return;
    uint32_t bits = 0x10000;

    const uint32_t stride = ctx->stride;
    uint8_t *pix = ctx->framebuffer + dst_y * stride;
    const uint32_t color = -(!ctx->color);

    uint32_t h = src_h;
    const uint32_t end_x = dst_x + src_w;
    do {
        uint32_t x = dst_x;
        do {
            if (bits & 0x10000) { // TODO: test unlikely() here
                bits = 0x100 | *src_bitstream;
                src_bitstream++;
            }

            uint32_t c = color ^ ((int32_t)(bits << 24) >> 31);
            static_assert((-1 >> 1) == -1, "Target compiler does not implement arithmetic right shift!");

            uint8_t mask = BIT_AT(x, dy);
            bitui_colorize8(c, pix + x/8, mask);
            bits <<= 1;
            x++;
        } while (x < end_x);
        pix += stride;
        h--;
    } while (h > 0);
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
