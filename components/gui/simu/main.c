#include <stdio.h>
#include <stdbool.h>

#include <SDL.h>

#include "gui.h"
#include "bitui.h"

#define WIN_WIDTH SCREEN_ROWS
#define WIN_HEIGHT SCREEN_COLS

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    // SDL init
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
        return 1;
    }

    // create SDL window
    SDL_Window *window = SDL_CreateWindow("sdl2_pixelbuffer",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIN_WIDTH * 3,
        WIN_HEIGHT * 3,
        0);
    if (window == NULL) {
        SDL_Log("Unable to create window: %s", SDL_GetError());
        return 1;
    }

    // create renderer
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == NULL) {
        SDL_Log("Unable to create renderer: %s", SDL_GetError());
        return 1;
    }

    SDL_RenderSetLogicalSize(renderer, WIN_WIDTH, WIN_HEIGHT);

    // create texture
    SDL_Texture *texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB332,
        SDL_TEXTUREACCESS_STREAMING,
        WIN_WIDTH,
        WIN_HEIGHT);
    if (texture == NULL) {
        SDL_Log("Unable to create texture: %s", SDL_GetError());
        return 1;
    }

    static uint8_t framebuffer[SCREEN_STRIDE * SCREEN_ROWS];
    static bitui_ctx_t bitui_handle = (bitui_ctx_t){
        .width = SCREEN_COLS,
        .height = SCREEN_ROWS,
        .stride = SCREEN_STRIDE,
        .framebuffer = framebuffer,
        .rot = BITUI_ROT_090,
        .color = true,
    };

    bitui_t ctx = &bitui_handle;

    gui_data_t gui_data = {
        .current_screen = GUI_HOME,
        .forecast = {
            .hourly.temperature_2m = {
                12.3,
                45.6,
                78.9
            },
        }
    };

    gui_render(ctx, &gui_data);

    // update texture with new data
    int texture_pitch = 0;
    void* texture_pixels = NULL;
    if (SDL_LockTexture(texture, NULL, &texture_pixels, &texture_pitch) != 0) {
        SDL_Log("Unable to lock texture: %s", SDL_GetError());
    }
    else {
        //ctx->rot = (BITUI_ROT_INVERT(ctx->rot) + 1) & 3;
        bitui_point_t p;
        uint8_t* pixels = texture_pixels;
        for (int y = 0; y < WIN_HEIGHT; y++) {
            for (int x = 0; x < WIN_WIDTH; x++) {
                p = bitui_apply_rot(ctx, (bitui_point_t){.x = x, .y = y});
                pixels[y * texture_pitch + x] = (framebuffer[p.y * SCREEN_STRIDE + p.x / 8]) & (0x80 >> (p.x & 7)) ? 0xff : 0;
            }
        }
    }
    SDL_UnlockTexture(texture);

    // main loop
    bool should_quit = false;
    SDL_Event e;
    while (!should_quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                should_quit = true;
            }
        }

        // render on screen
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

