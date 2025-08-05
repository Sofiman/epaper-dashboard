#include <stdio.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <signal.h>

#include <SDL.h>

#include "gui.h"
#include "bitui.h"

#define WIN_WIDTH SCREEN_ROWS
#define WIN_HEIGHT SCREEN_COLS

static volatile int should_hotreload = 0;
static void signal_handler(int signum) {
    if (signum == SIGUSR1) {
        should_hotreload = 1;
    }
}

static void (*dyn_gui_render)(bitui_t ctx, const gui_data_t *data) = gui_render;

int hotreload_load_symbols(void) {
    static void *libgui = NULL;
    if (libgui != NULL) {
        dyn_gui_render = gui_render;
        dlclose(libgui);
    }

    libgui = dlopen("./libgui.so", RTLD_NOW);
    if (!libgui) {
        fprintf(stderr, "Could not hotreload GUI: %s\n", dlerror());
        return 0;
    }

    void *sym_gui_render = dlsym(libgui, "gui_render");
    if (sym_gui_render == NULL) {
        fprintf(stderr, "Could not hotreload GUI: %s for symbol `gui_render`\n", dlerror());
        return 0;
    }
    dyn_gui_render = sym_gui_render;

    printf("Hot reload successful.\n");
    return 1;
}

static bitui_t ctx;
static gui_data_t gui_data = {
    .current_screen = GUI_HOME,
    .forecast = {
        .hourly = {
            .time = {1754348400,1754352000,1754355600,1754359200,1754362800,1754366400,1754370000,1754373600,1754377200,1754380800,1754384400,1754388000,1754391600,1754395200,1754398800,1754402400,1754406000,1754409600,1754413200,1754416800,1754420400,1754424000,1754427600,1754431200,1754434800,1754438400,1754442000,1754445600,1754449200,1754452800,1754456400,1754460000,1754463600,1754467200,1754470800,1754474400,1754478000,1754481600,1754485200,1754488800,1754492400,1754496000,1754499600,1754503200,1754506800,1754510400,1754514000,1754517600},
            .temperature_2m = {23.5,22.1,21.3,20.5,20.1,18.5,18.3,18.4,18.6,19.2,20.2,20.8,21.5,22.3,22.8,23.2,22.9,22.7,22.1,21.5,20.4,19.3,18.7,18.1,17.4,16.8,16.3,15.8,15.4,15.0,14.9,15.3,16.2,17.4,18.7,19.8,20.7,21.6,22.1,23.0,23.3,23.6,23.5,23.1,22.4,21.7,21.3,20.8},
            .weather_code_2m = {2,3,3,61,95,63,61,3,2,2,2,2,1,2,1,2,1,1,1,1,0,0,1,0,3,3,1,3,3,3,3,3,3,3,3,3,3,3,2,1,0,0,1,3,3,3,3,3}
        },
        .daily = {
            .time = {1754348400,1754434800},
            .sunrise = {1754368192,1754454675},
            .sunset = {1754421789,1754508092}
        }
    }
};

void render_copy(SDL_Texture *texture) {
    hotreload_load_symbols();
    dyn_gui_render(ctx, &gui_data);

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
                pixels[y * texture_pitch + x] = (ctx->framebuffer[p.y * SCREEN_STRIDE + p.x / 8]) & (0x80 >> (p.x & 7)) ? 0xff : 0;
            }
        }
    }
    SDL_UnlockTexture(texture);
}

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
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
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

    struct sigaction sa = { .sa_flags = SA_SIGINFO, .sa_handler = signal_handler };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1)
        fprintf(stderr, "Failed to setup SIGUSR1 signal handler. Hot reloading not available.");

    static uint8_t framebuffer[SCREEN_STRIDE * SCREEN_ROWS];
    static bitui_ctx_t bitui_handle = (bitui_ctx_t){
        .width = SCREEN_COLS,
        .height = SCREEN_ROWS,
        .stride = SCREEN_STRIDE,
        .framebuffer = framebuffer,
        .rot = BITUI_ROT_090,
        .color = true,
    };

    ctx = &bitui_handle;

    render_copy(texture);

    // main loop
    bool should_quit = false;
    SDL_Event e;
    while (!should_quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                should_quit = true;
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.scancode == SDL_SCANCODE_R) {
                    should_hotreload = 1;
                }
            }
        }

        if (should_hotreload) {
            should_hotreload = 0;
            render_copy(texture);
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

