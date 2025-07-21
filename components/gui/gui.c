#include "gui.h"

#include <stdio.h>
#include <esp_log.h>
#include <assert.h>
#include "gfxfont.h"
#define PROGMEM
#include "WeatherIcons.h"
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

static struct size measure_text(bitui_t ctx, const GFXfont *font, const char *str) {
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
    time_t now = 0;
    struct tm timeinfo = { 0 };
    time(&now);

    localtime_r(&now, &timeinfo);
    strftime(temp_str, sizeof(temp_str), "%A %d %b, %R", &timeinfo);

    bitui_clear(ctx, true);
    render_text(ctx, &FONT, temp_str, 16, 20);
}

static void gui_render_home(bitui_t ctx, const gui_data_t *data)
{
    bitui_clear(ctx, true);

        ctx->color = false;
    bitui_fill_rect(ctx, (bitui_rect_t){ .x = 0, .y = 1, .w = 10, .h = 20 });
    return;

    const struct Forecast *forecast = &data->forecast;
    const uint64_t timestamp = forecast->updated_at;

    enum {
        WIDTH = 30,
        BORDER = 1,
        PADDING = 1,
        INSET = BORDER + PADDING,
        ACTUAL_WIDTH = WIDTH - BORDER * 2,
    };

    bitlayout_t list = { .dir = LAYOUT_HORIZONTAL, .element_gap = -1, .cursor.y = 10 };

    bitui_point_t pos;
    for (int i = 0; i < 12; i++) {
        pos = bitlayout_element(&list, (bitui_point_t) { .x = WIDTH, .y = 64 });
        ctx->color = false;
        bitui_rect(ctx, (bitui_rect_t){ .x = pos.x, .y = pos.y, .w = WIDTH, .h = 64 });

        //enum WeatherIcon icon = weather_icon_from_wmo_code(forecast->hourly.weather_code_2m[i]);
        bitui_paste_bitmap(ctx, WEATHER_ICONS[i], WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, pos.x + ACTUAL_WIDTH / 2 - WEATHER_ICON_SIZE / 2, pos.y + INSET);

        ctx->color = true;
        snprintf(temp_str, sizeof(temp_str), "%.1f", forecast->hourly.temperature_2m[i]);
        struct size s = measure_text(ctx, &Thixel8pt7b, temp_str);
        render_text(ctx, &Thixel8pt7b, temp_str, pos.x + PADDING + ACTUAL_WIDTH / 2 - s.w / 2, pos.y + INSET + WEATHER_ICON_SIZE + PADDING + s.h / 2);

        snprintf(temp_str, sizeof(temp_str), "%ih", i);
        s = measure_text(ctx, &Thixel8pt7b, temp_str);
        render_text(ctx, &Thixel8pt7b, temp_str, pos.x + INSET + ACTUAL_WIDTH / 2 - s.w / 2, pos.y + 4);
    }

    /*
    enum {
        GRAPH_START_X = SCREEN_ROWS - 2 * FORECAST_HOURLY_POINT_COUNT,
        BAR_START_Y = 6,
        BAR_MAX_HEIGHT = 48,
        BAR_MIN_HEIGHT = 2,
        CURSOR_HEIGHT = 4,
        CURSOR_PADDING = 2,
    };
    size_t cursor = 0;
    while (cursor < FORECAST_HOURLY_POINT_COUNT && forecast->hourly.time[cursor] < timestamp) ++cursor;
    if (cursor == FORECAST_HOURLY_POINT_COUNT) return;

    ESP_LOGI(TAG, "Current time : %lu, Closest data point : %lu", (long)timestamp, (long)forecast->hourly.time[cursor]);

    float temp_min = 256.0f;
    float temp_max = -256.0f;
    for (size_t i = 0; i < FORECAST_HOURLY_POINT_COUNT; ++i) {
        if (forecast->hourly.temperature_2m[i] < temp_min) temp_min = forecast->hourly.temperature_2m[i];
        if (forecast->hourly.temperature_2m[i] > temp_max) temp_max = forecast->hourly.temperature_2m[i];
    }

    ctx->color = false;
    for (size_t i = 0; i < FORECAST_HOURLY_POINT_COUNT; ++i) {
        float fill = (forecast->hourly.temperature_2m[i] - temp_min) / (temp_max - temp_min);
        bitui_hline(ctx, GRAPH_START_X + i*2, BAR_START_Y, BAR_START_Y + BAR_MIN_HEIGHT + (uint16_t)(fill * BAR_MAX_HEIGHT));
    }

    bitui_point(ctx, GRAPH_START_X + cursor*2 - 1, 127);
    bitui_point(ctx, GRAPH_START_X + cursor*2 + 1, 127);
    bitui_hline(ctx, GRAPH_START_X + cursor*2 + 0, 0, CURSOR_HEIGHT);

    uint16_t h = (uint16_t)((forecast->hourly.temperature_2m[cursor] - temp_min) / (temp_max - temp_min) * BAR_MAX_HEIGHT);
    bitui_point(ctx, GRAPH_START_X + cursor*2 - 1, BAR_START_Y + BAR_MIN_HEIGHT + BAR_MAX_HEIGHT + CURSOR_PADDING + CURSOR_HEIGHT + 4);
    bitui_point(ctx, GRAPH_START_X + cursor*2 + 1, BAR_START_Y + BAR_MIN_HEIGHT + BAR_MAX_HEIGHT + CURSOR_PADDING + CURSOR_HEIGHT + 4);
    bitui_hline(ctx, GRAPH_START_X + cursor*2 + 0, BAR_START_Y + BAR_MIN_HEIGHT + h + CURSOR_PADDING, BAR_START_Y + BAR_MIN_HEIGHT + BAR_MAX_HEIGHT + 2 + CURSOR_HEIGHT);

    snprintf(temp_str, sizeof(temp_str), "Now %.1f C\nMin %.1f C\nMax %.1f C", forecast->hourly.temperature_2m[cursor], temp_min, temp_max);

    ctx->color = true;
    render_text(ctx, &FONT, temp_str, 16, 72);
    */
}

typedef void (*gui_screeen_renderer_t)(bitui_t ctx, const gui_data_t *data);

const gui_screeen_renderer_t GUI_SCREEN_RENDERERERS[GUI_COUNT] = {
    [GUI_BOOT] = gui_render_home,
    [GUI_WIFI_INIT] = gui_render_wifi_init,
    [GUI_SYNC_TIME] = gui_render_sync_time,
    [GUI_HOME] = gui_render_home,
};

void gui_render(bitui_t ctx, const gui_data_t *data)
{
    GUI_SCREEN_RENDERERERS[data->current_screen](ctx, data);
}
