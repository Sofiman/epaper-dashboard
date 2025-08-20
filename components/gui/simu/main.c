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
static struct Forecast g_forecast = {
        .hourly = {
            .time = {1754784000,1754787600,1754791200,1754794800,1754798400,1754802000,1754805600,1754809200,1754812800,1754816400,1754820000,1754823600,1754827200,1754830800,1754834400,1754838000,1754841600,1754845200,1754848800,1754852400,1754856000,1754859600,1754863200,1754866800,1754870400,1754874000,1754877600,1754881200,1754884800,1754888400,1754892000,1754895600,1754899200,1754902800,1754906400,1754910000,1754913600,1754917200,1754920800,1754924400,1754928000,1754931600,1754935200,1754938800,1754942400,1754946000,1754949600,1754953200},
            .temperature_2m = {17.3,16.6,16.0,15.5,15.2,15.0,15.2,16.1,17.6,19.7,21.5,23.3,24.8,26.2,27.2,27.9,27.9,27.7,27.1,26.1,25.1,23.8,22.7,21.7,20.9,20.2,19.4,18.6,18.0,17.5,17.8,19.0,20.7,22.9,25.2,27.4,29.2,30.2,31.0,31.6,31.6,31.2,30.5,29.5,28.2,26.8,25.6,24.8},
            .weather_code = {1,2,3,3,3,3,3,3,3,2,3,3,1,2,1,0,0,3,1,1,1,2,2,2,2,2,2,2,2,1,1,1,2,2,2,3,2,2,1,1,1,0,0,0,0,0,0,0}
        },
        .daily = {
            .time = {1754784000,1754870400},
            .sunrise = {1754800610,1754887094},
            .sunset = {1754853293,1754939590}
        },
        .updated_at = 1
    };
static TempData temp_data = {
    .start = 5,
    .count = 32,
    .items = {
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        14,
        15,
        16,
        17,
        18,
        19,
        20,
        21,
        22,
        23,
        24,
        25,
        26,
        27,
        28,
        29,
        30,
        31,
        32,
    },
};

static gui_data_t gui_data = {
    .current_screen = GUI_HOME,
    .forecast = &g_forecast,
    .temp_data = &temp_data
};

void render_copy(SDL_Texture *texture, bool reload) {
    if (reload) hotreload_load_symbols();
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
#ifdef BITUI_ROTATION
                p = bitui_apply_rot(ctx, (bitui_point_t){.x = x, .y = y});
#else
                p.x = x;
                p.y = y;
#endif
                pixels[y * texture_pitch + x] = (ctx->framebuffer[p.y * SCREEN_STRIDE + p.x / 8]) & (0x80 >> (p.x & 7)) ? 0xff : 0;
            }
        }
    }
    SDL_UnlockTexture(texture);
}

static float *ringbuf_emplace(TempData *buf) {
    if (buf->count < 32) return &buf->items[buf->count++];
    size_t new_item = buf->start;
    buf->start = (buf->start + 1) % 32;
    return &buf->items[new_item];
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
#ifdef BITUI_ROTATION
        .rot = BITUI_ROT_090,
#endif
        .color = true,
    };

    ctx = &bitui_handle;

    render_copy(texture, true);

    // main loop
    bool should_quit = false;
    SDL_Event e;
    while (!should_quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                should_quit = true;
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.scancode == SDL_SCANCODE_H) {
                    should_hotreload = 1;
                } else if (e.key.keysym.scancode == SDL_SCANCODE_G) {
                    gui_data.tick++;
                    if (gui_data.tick >= 8) g_forecast.updated_at = time(NULL);
                    render_copy(texture, false);
                } else if (e.key.keysym.scancode == SDL_SCANCODE_R) {
                    gui_data.tick = 0;
                    g_forecast.updated_at = 0;
                    temp_data.count = 0;
                    render_copy(texture, false);
                } else if (e.key.keysym.scancode == SDL_SCANCODE_T) {
                    const float new = ringbuf_newest(&temp_data) + 1.0 - ((float)rand() / (float)RAND_MAX) * 2.0;
                    *ringbuf_emplace(&temp_data) = new;
                    render_copy(texture, false);
                }
            }
        }

        if (should_hotreload) {
            should_hotreload = 0;
            render_copy(texture, true);
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

