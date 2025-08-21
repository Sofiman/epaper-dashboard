#include "gui.h"

#include <esp_log.h>
#include <assert.h>
#include "gfxfont.h"
#define PROGMEM
#include "Meteocons.h"
#include "DigitalDisco16pt7b.h"
#include "Blocktopia8pt7b.h"
#include "Icons.h"
#define FONT_BIG DigitalDisco16pt7b
#define FONT_SMALL Blocktopia8pt7b

// For INFINITY macros
#include <math.h>

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
#define tmp_sprintf(...) (snprintf(temp_str, sizeof(temp_str), __VA_ARGS__), temp_str)

static void gui_render_boot(bitui_t ctx, const gui_data_t *data) {
    bitui_clear(ctx, true);

    const char *title = "Connecting to Wi-Fi";
    struct size s = measure_text(&FONT_BIG, title);
    uint16_t start_y = ctx->height / 2 - (s.h + 17 + 17) / 2 + s.h;
    render_text(ctx, &FONT_BIG, title, ctx->width / 2 - s.w / 2, start_y);

    const GFXglyph glyph = Icons.glyph[ICON_HOURGLASS_FILLED_20 + data->tick % 5];
    bitui_paste_bitstream(ctx, Icons.bitmap + glyph.bitmapOffset, glyph.width, glyph.height, ctx->width / 2 - (glyph.xOffset + glyph.width) / 2, start_y + 17);
}

static void gui_render_wifi_init(bitui_t ctx, const gui_data_t *data) {
    bitui_clear(ctx, true);

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_default_netif(), &ip_info);

    tmp_sprintf("Connected\nIP: " IPSTR "\nSyncing time...", IP2STR(&ip_info.ip));
    render_text(ctx, &FONT_BIG, temp_str, 32, 48);
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

    struct size s = measure_text(&FONT_SMALL, label);
    ctx->color = true;
    bitui_line(ctx, bbox.x + PADDING_H, bbox.y, bbox.x + PADDING_H + PADDING_H / 2 + s.w, bbox.y);
    render_text(ctx, &FONT_SMALL, label, bbox.x + PADDING_H + PADDING_H / 2, bbox.y + s.h / 4);
}

static size_t find_closest(const int64_t *haystack, size_t count, int64_t needle) {
    // TODO: binary search
    size_t i = 0;
    while (i < count && haystack[i] < needle) {
        ++i;
    }
    return i;
}

static bool is_day(const struct Forecast *forecast, int64_t now, int64_t *sun_event_time) {
    const size_t cur_day = find_closest(forecast->daily.time, FORECAST_DURATION_DAYS, now) - 1;
    assert(cur_day < FORECAST_DURATION_DAYS && "`now` is somehow earlier than `daily.time[0]`");

    const int64_t diff_sunrise = now - forecast->daily.sunrise[cur_day];
    const int64_t  diff_sunset = now - forecast->daily.sunset [cur_day];

    // In the following ASCII art:
    // - The sign of diff_sunrise is to the left, and diff_sunset, to the right.
    // - `.*^` represents sunrise, and `^*.`, sunset.
    //
    // NIGHTNIGHT  .*^  DAYDAYDAYDAYDAY  ^*.  NIGHTNIGHT
    //     --      0-         +-         +0       ++
    //             ^^^^^^^^^^^^^^^^^^^^
    //                   is_day=1
    // Note that during the day, both substractions have opposite signs.
    const bool is_day = (diff_sunrise ^ diff_sunset) < 0;
    if (sun_event_time)
        *sun_event_time = is_day ? forecast->daily.sunrise[cur_day]
            : forecast->daily.sunset[cur_day];
    return is_day;
}

static void widget_weather(bitui_t ctx, const gui_data_t *data)
{
    const struct Forecast *forecast = data->forecast;

    enum {
        COL_WIDTH = 32,
        COL_HEIGHT = 54,
        PADDING = 4,
        HOURS_DISPLAYED = (SCREEN_ROWS - 6 - 1) / COL_WIDTH,
        START_X = 1,
        START_Y = 114,
        LABEL_INTERVAL = 2,
    };

    ctx->color = true;
    draw_widget_outline(ctx, (bitui_rect_t){ .x = 1, .y = START_Y, .w = SCREEN_ROWS - 2, .h = SCREEN_COLS-1-START_Y }, "WEATHER");

    struct size s;
    if (forecast->updated_at <= 0) {
        const bool is_error = forecast->updated_at < 0;
        const char *status = "loading";
        if (is_error) {
            tmp_sprintf("E%llx", -forecast->updated_at);
            status = temp_str;
        }

        s = measure_text(&FONT_SMALL, status);
        uint16_t start_y = START_Y + COL_HEIGHT / 2 - (17 + PADDING * 3 + s.h/2) / 2;
        render_text(ctx, &FONT_SMALL, status, SCREEN_ROWS / 2 - s.w / 2, start_y + 17 + PADDING * 3);

        const GFXglyph glyph = Icons.glyph[is_error ? ICON_WARNING : (ICON_HOURGLASS_FILLED_20 + data->tick % 5)];
        bitui_paste_bitstream(ctx, Icons.bitmap + glyph.bitmapOffset, glyph.width, glyph.height, SCREEN_ROWS / 2 - (glyph.xOffset + glyph.width) / 2, start_y);
        return;
    }

    bitlayout_t list = { .dir = LAYOUT_HORIZONTAL, .element_gap = 3, .cursor = { .x = START_X, .y = START_Y + 2 } };

    time_t now = time(NULL);
    _Static_assert(FORECAST_HOURLY_POINT_COUNT >= HOURS_DISPLAYED);
    size_t cur_hour = find_closest(forecast->hourly.time, FORECAST_HOURLY_POINT_COUNT - HOURS_DISPLAYED, now);

    // Calculate the label offset to prevent overlapping text when displaying
    // the exact sunrise/sunset hours.
    int hour_label_offset = 0;
    int prev_is_day = is_day(forecast, forecast->hourly.time[cur_hour], NULL);
    for (size_t i = 0; i < HOURS_DISPLAYED; i++) {
        now = forecast->hourly.time[cur_hour + i];
        int cur_is_day = is_day(forecast, forecast->hourly.time[cur_hour + i], NULL);
        if (prev_is_day ^ cur_is_day) {
            hour_label_offset = LABEL_INTERVAL - i % LABEL_INTERVAL;
            break;
        }

        prev_is_day = cur_is_day;
    }

    bitui_point_t pos;
    struct tm timeinfo = { 0 };
    prev_is_day = is_day(forecast, forecast->hourly.time[cur_hour], NULL);
    for (int i = 0; i < HOURS_DISPLAYED; i++, cur_hour++) {
        int cur_is_day = is_day(forecast, forecast->hourly.time[cur_hour], &now);
        if (cur_is_day ^ prev_is_day) {
            strftime(temp_str, sizeof(temp_str), "%H:%M", localtime_r(&now, &timeinfo));

            pos = bitlayout_element(&list, (bitui_point_t) { .x = COL_WIDTH, .y = COL_HEIGHT });
            s = measure_text(&FONT_SMALL, temp_str);
            pos.y += s.h/2;
            uint16_t text_x = pos.x + (pos.x + COL_WIDTH / 2 < s.w / 2 ? 2 : COL_WIDTH / 2 - s.w / 2);
            render_text(ctx, &FONT_SMALL, temp_str, text_x, pos.y);

            pos.y += PADDING;
            pos.y += Meteocons.yAdvance;
            const GFXglyph glyph = Meteocons.glyph[METEOCON_SUNSET_SUNRISE];
            bitui_paste_bitstream(ctx, Meteocons.bitmap + glyph.bitmapOffset, glyph.width, glyph.height, pos.x + COL_WIDTH / 2 - (glyph.xOffset + glyph.width) / 2, pos.y + glyph.yOffset);

            i++;
        }
        prev_is_day = cur_is_day;

        pos = bitlayout_element(&list, (bitui_point_t) { .x = COL_WIDTH, .y = COL_HEIGHT });

        pos.y += FONT_SMALL.yAdvance/2;
        if ((i + hour_label_offset) % LABEL_INTERVAL == 0) {
            now = forecast->hourly.time[cur_hour];
            strftime(temp_str, sizeof(temp_str), "%H:00", localtime_r(&now, &timeinfo));
            s = measure_text(&FONT_SMALL, temp_str);
            uint16_t text_x = pos.x + COL_WIDTH / 2 - s.w / 2;
            if (text_x > pos.x + COL_WIDTH / 2 + s.w) text_x = pos.x + 2;
            if (text_x + s.w >= SCREEN_ROWS - 2) text_x -= 3;
            render_text(ctx, &FONT_SMALL, temp_str, text_x, pos.y);
        }

        pos.y += PADDING + Meteocons.yAdvance;
        const enum Meteocon icon = meteocon_from_wmo_code(forecast->hourly.weather_code[cur_hour], cur_is_day);
        const GFXglyph glyph = Meteocons.glyph[icon];
        bitui_paste_bitstream(ctx, Meteocons.bitmap + glyph.bitmapOffset, glyph.width, glyph.height, pos.x + COL_WIDTH / 2 - (glyph.xOffset + glyph.width) / 2, pos.y + glyph.yOffset);

        tmp_sprintf("%.1f", forecast->hourly.temperature_2m[cur_hour]);
        s = measure_text(&FONT_SMALL, temp_str);
        pos.y += PADDING + s.h/2;
        assert(s.w / 2 <= pos.x + COL_WIDTH / 2 && "temp label is too wide");
        render_text(ctx, &FONT_SMALL, temp_str, pos.x + COL_WIDTH / 2 - s.w / 2, pos.y);
    }
}

static void widget_time(bitui_t ctx, const gui_data_t *data)
{
    time_t now = 0;
    struct tm timeinfo = { 0 };
    time(&now);

    localtime_r(&now, &timeinfo);
    strftime(temp_str, sizeof(temp_str), "%A %d %b, %R", &timeinfo);

    struct size s = measure_text(&FONT_BIG, temp_str);
    render_text(ctx, &FONT_BIG, temp_str, SCREEN_ROWS / 2 - s.w / 2, 12 + s.h / 2);
}

static void widget_temp(bitui_t ctx, int i, const char *label, const char *unit, const TempData *data, bool higher_is_better)
{
    enum {
        MARGIN = 2,
        WIDTH = (SCREEN_ROWS - 2 * MARGIN) / 3,
        HEIGHT = 44,
        START_Y = 52,
        USABLE_HEIGHT = 32,
        BARS_COUNT = (WIDTH - 8) / 4 + 1,
    };

    uint16_t start_x = i * (MARGIN + WIDTH) + 1;

    ctx->color = true;
    draw_widget_outline(ctx, (bitui_rect_t){ .x = start_x, .y = START_Y, .w = WIDTH, .h = HEIGHT }, label);

    struct size s;
    const char *current_val = data && data->count > 0 ? tmp_sprintf("%.1f %s", ringbuf_newest(data), unit) : tmp_sprintf("? %s", unit);
    s = measure_text(&FONT_SMALL, current_val);
    render_text(ctx, &FONT_SMALL, current_val, start_x + WIDTH - MARGIN - s.w, START_Y - MARGIN + s.h/2);

    const int error = 1;
    if (error < 0) {
        tmp_sprintf("E%x", -error);

        s = measure_text(&FONT_SMALL, temp_str);
        uint16_t start_y = START_Y + HEIGHT / 2 + MARGIN - (17 + MARGIN + s.h/2) / 2;
        render_text(ctx, &FONT_SMALL, temp_str, start_x + WIDTH / 2 - s.w/2, start_y + 17 + MARGIN + s.h/2);

        const GFXglyph glyph = Icons.glyph[ICON_WARNING];
        bitui_paste_bitstream(ctx, Icons.bitmap + glyph.bitmapOffset, glyph.width, glyph.height, start_x + WIDTH/2 - 17/2, start_y);
        return;
    }

    if (data == NULL || data->count == 0) {
        s = measure_text(&FONT_SMALL, "no history");
        render_text(ctx, &FONT_SMALL, "no history", start_x + WIDTH / 2 - s.w / 2, START_Y + HEIGHT / 2 - MARGIN + s.h/2);
        return;
    }

    float max = -INFINITY, min = INFINITY;
    if (data->count == 1) {
        max = data->items[data->start]*2;
        min = 0;
    } else {
        for_ringbuf(data) {
            float val = data->items[it];
            if (val > max) max = val;
            if (val < min) min = val;
        }
    }
    //max += 1/(max-min);
    //min -= 1/(max-min);

    ctx->color = false;
    int dx = 4;
    int to_skip = data->count - BARS_COUNT;
    for_ringbuf(data) {
        if (to_skip > 0) {
            --to_skip;
            continue;
        }
        float p = (data->items[it] - min) / (max-min);
        //if (higher_is_better) p = 1.0 - p;
        uint16_t bar_height = (USABLE_HEIGHT - 2) * p + 2;
        bitui_line(ctx, start_x + dx    , START_Y + HEIGHT - MARGIN, start_x + dx    , START_Y + HEIGHT - MARGIN - bar_height);
        bitui_line(ctx, start_x + dx + 1, START_Y + HEIGHT - MARGIN, start_x + dx + 1, START_Y + HEIGHT - MARGIN - bar_height);
        dx += 4;
        if (dx >= WIDTH - 4) break;
    }
}

static void gui_render_home(bitui_t ctx, const gui_data_t *data)
{
    bitui_clear(ctx, true);

    ctx->color = false;

    widget_weather(ctx, data);
    widget_time(ctx, data);
    widget_temp(ctx, 0, "TEMP", "C", data->temp_data, false);
    widget_temp(ctx, 1, "RH", "%", data->rel_hum_data, true);
    widget_temp(ctx, 2, "CO2", "ppm", NULL, false);
}

typedef void (*gui_screeen_renderer_t)(bitui_t ctx, const gui_data_t *data);

const gui_screeen_renderer_t GUI_SCREEN_RENDERERERS[GUI_COUNT] = {
    [GUI_BOOT] = gui_render_boot,
    [GUI_WIFI_INIT] = gui_render_wifi_init,
    [GUI_HOME] = gui_render_home,
};

void gui_render(bitui_t ctx, const gui_data_t *data)
{
    GUI_SCREEN_RENDERERERS[data->current_screen](ctx, data);
}
