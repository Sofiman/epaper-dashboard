#include "gui.h"

#include <stdio.h>
#include <esp_log.h>
#include <assert.h>
#include "gfxfont.h"
#define PROGMEM
#include "Meteocons.h"
#include "Thixel16pt7b.h"
#include "Thixel8pt7b.h"
#define FONT Thixel16pt7b

typedef enum : uint16_t {
    LAYOUT_HORIZONTAL,
    LAYOUT_VERTICAL,
} bitlayout_dir_t;

typedef struct {
    bitlayout_dir_t dir;

    uint16_t element_count;
    int16_t element_gap;
    bitui_point_t cursor;
} bitlayout_t;

bitui_point_t bitlayout_element(bitlayout_t *l, bitui_point_t size) {
    const bitui_point_t element_pos = l->cursor;

    uint16_t *layout_main_axis = l->dir == LAYOUT_HORIZONTAL ? &l->cursor.x :  &l->cursor.y;
    uint16_t main_axis_delta = l->dir == LAYOUT_HORIZONTAL ? size.x : size.y;

    *layout_main_axis += main_axis_delta + l->element_gap;

    l->element_count++;
    return element_pos;
}

static const char *TAG = "GUI";

struct size {
    uint16_t w, h;
};

#define MIN(A,B) (((A) < (B)) ? (A) : (B))
#define MAX(A,B) (((A) > (B)) ? (A) : (B))

static struct size measure_text(const GFXfont *font, const char *str) {
    struct size s = { .w = 0, .h = 0 };
    if (*str) s.h = font->yAdvance;

    uint16_t line_width = 0;
    for (; *str; ++str) {
        const char c = *str;
        if (c == '\n') {
            s.h += font->yAdvance;
            s.w = MAX(s.w, line_width);
            line_width = 0;
            continue;
        }

        assert(c >= font->first || c <= font->last);
        const GFXglyph *glyph = &font->glyph[c - font->first];

        line_width += glyph->xAdvance;
    }
    s.w = MAX(s.w, line_width);
    return s;
}

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

static char temp_str[80];

static void gui_render_boot(bitui_t ctx, const gui_data_t *data) {
    (void)data;
    bitui_clear(ctx, true);
    render_text(ctx, &FONT, "Connecting to Wi-Fi", 32, 48);
}

static void gui_render_wifi_init(bitui_t ctx, const gui_data_t *data) {
    bitui_clear(ctx, true);

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(data->netif, &ip_info);

    snprintf(temp_str, sizeof(temp_str), "Connected\nIP: " IPSTR "\nSyncing time...", IP2STR(&ip_info.ip));
    render_text(ctx, &FONT, temp_str, 32, 48);
}

static void gui_render_sync_time(bitui_t ctx, const gui_data_t *data) {
    (void)data;
    time_t now = 0;
    struct tm timeinfo = { 0 };
    time(&now);

    localtime_r(&now, &timeinfo);
    strftime(temp_str, sizeof(temp_str), "%A %d %b, %R", &timeinfo);

    bitui_clear(ctx, true);
    render_text(ctx, &FONT, temp_str, 16, 20);
}

static void draw_widget_outline(bitui_t ctx, bitui_rect_t bbox, const char *label)
{
    enum {
        FONT_HEIGHT = 4,
        PADDING_V = 6,
        PADDING_H = 4,
    };

    bbox.y -= FONT_HEIGHT/2 + PADDING_V;
    bbox.h += FONT_HEIGHT/2 + PADDING_V;
    ctx->color = false;
    bitui_rect(ctx, bbox);

    struct size s = measure_text(&Thixel8pt7b, label);
    ctx->color = true;
    bitui_line(ctx, bbox.x + PADDING_H, bbox.y, bbox.x + PADDING_H + PADDING_H / 2 + s.w, bbox.y);
    render_text(ctx, &Thixel8pt7b, label, bbox.x + PADDING_H + PADDING_H / 2, bbox.y + s.h / 4);
}

static void gui_render_home(bitui_t ctx, const gui_data_t *data)
{
    bitui_clear(ctx, true);

    ctx->color = false;

    const struct Forecast *forecast = &data->forecast;
    const uint64_t timestamp = forecast->updated_at;

    enum {
        WIDTH = 29,
        HEIGHT = 64,
        PADDING = 4,
        HOURS_DISPLAYED = (SCREEN_ROWS/WIDTH),
        START_X = SCREEN_ROWS / 2 - HOURS_DISPLAYED*WIDTH/2,
        START_Y = 116,
    };

    bitlayout_t list = { .dir = LAYOUT_HORIZONTAL, .element_gap = 0, .cursor = { .x = START_X, .y = START_Y } };

    ctx->color = true;
    bitui_point_t pos;
    struct size s;
    for (int i = 0; i < HOURS_DISPLAYED; i++) {
        pos = bitlayout_element(&list, (bitui_point_t) { .x = WIDTH, .y = HEIGHT });

        snprintf(temp_str, sizeof(temp_str), "%ih", i);
        s = measure_text(&Thixel8pt7b, temp_str);
        pos.y += s.h/2;
        render_text(ctx, &Thixel8pt7b, temp_str, pos.x + WIDTH / 2 - s.w / 2, pos.y);

        pos.y += PADDING;
        pos.y += Meteocons.yAdvance;
        const enum Meteocon icon = meteocon_from_wmo_code(forecast->hourly.weather_code_2m[i], METEOCON_SUNRISE);
        const GFXglyph glyph = Meteocons.glyph[icon];
        bitui_paste_bitstream(ctx, Meteocons.bitmap + glyph.bitmapOffset, glyph.width, glyph.height, pos.x + WIDTH / 2 - (glyph.xOffset + glyph.width) / 2, pos.y + glyph.yOffset);

        snprintf(temp_str, sizeof(temp_str), "%.1f", forecast->hourly.temperature_2m[i]);
        s = measure_text(&Thixel8pt7b, temp_str);
        pos.y += PADDING + s.h/2;
        render_text(ctx, &Thixel8pt7b, temp_str, pos.x + WIDTH / 2 - s.w / 2, pos.y);
    }
    draw_widget_outline(ctx, (bitui_rect_t){ .x = 1, .y = START_Y, .w = SCREEN_ROWS - 2, .h = SCREEN_COLS-1-START_Y }, "WEATHER");
}

typedef void (*gui_screeen_renderer_t)(bitui_t ctx, const gui_data_t *data);

const gui_screeen_renderer_t GUI_SCREEN_RENDERERERS[GUI_COUNT] = {
    [GUI_BOOT] = gui_render_boot,
    [GUI_WIFI_INIT] = gui_render_wifi_init,
    [GUI_SYNC_TIME] = gui_render_sync_time,
    [GUI_HOME] = gui_render_home,
};

void gui_render(bitui_t ctx, const gui_data_t *data)
{
    GUI_SCREEN_RENDERERERS[data->current_screen](ctx, data);
}
