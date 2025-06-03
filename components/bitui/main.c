#include <stdio.h>
#include "bitui.h"

#define WIDTH 40
#define HEIGHT 16

int main(int argc, char *argv[])
{
    static uint8_t framebuffer[WIDTH/8*HEIGHT];
    bitui_t ctx = &(bitui_ctx_t){
        .width = WIDTH,
        .height = HEIGHT,
        .framebuffer = framebuffer,
        .color = true,
    };

    bitui_rect(ctx, (bitui_rect_t){ .x = 3, .y = 3, .w = 2, .h = 2 });

    //int c;
    //do {
        for (size_t y = 0; y < HEIGHT; ++y) {
            for (size_t i = 0; i < WIDTH/8; ++i) {
                uint8_t b = framebuffer[y * WIDTH + i];
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

