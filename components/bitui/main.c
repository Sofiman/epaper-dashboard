#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#define PROGMEM
#include "gfxfont.h"
#include "DigitalDisco12pt7b.h"
#include "bitui.h"

#define WIDTH 160
#define HEIGHT 40

static void render_text(bitui_t ctx, const GFXfont *font, const char *str, const uint16_t bottom_left_x, const uint16_t bottom_left_y) {
    uint16_t x = bottom_left_x;
    uint16_t y = bottom_left_y;
    for (; *str; ++str) {
        const char c = *str;
        if (c == '\n') {
            x = bottom_left_x;
            y += font->yAdvance;
            continue;
        }

        assert(c >= font->first || c <= font->last);
        const GFXglyph glyph = font->glyph[c - font->first];

        bitui_paste_bitstream(ctx, font->bitmap + glyph.bitmapOffset, glyph.width, glyph.height, glyph.xOffset + x, y + glyph.yOffset);
        x += glyph.xAdvance;
    }
}

int main(int argc, char *argv[])
{
    static uint8_t framebuffer[WIDTH/8*HEIGHT];
    bitui_t ctx = &(bitui_ctx_t){
        .width = WIDTH,
        .height = HEIGHT,
        .framebuffer = framebuffer,
        .color = true,
    };

    /*
    for (size_t i = 0; i < 10; ++i) {
        bitui_rect(ctx, (bitui_rect_t){ .x = i, .y = i * 4, .w = 4, .h = 4 });
    }*/

    //  offset    w   h   xAd   xOf   yOf
    // {  1131,  12,  12,  14,    0,  -11 },   // 0x61 'a'

    render_text(ctx, &DigitalDisco12pt7b, "Hello, world!", 0, 15);

    //int c;
    //do {
        for (size_t y = 0; y < HEIGHT; ++y) {
            for (size_t i = 0; i < WIDTH/8; ++i) {
                uint8_t b = framebuffer[y * WIDTH/8 + i];
                for (size_t dx = 7; dx <= 7; --dx) {
                    printf("%s", (b & (1 << dx)) ? "██" : "░░");
                }
            }
            putchar('\n');
        }

    //    c = getchar();
    //} while (c != -1);
    return 0;
}

