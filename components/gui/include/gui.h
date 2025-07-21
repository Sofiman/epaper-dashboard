#pragma once

#include "bitui.h"

#include <time.h>
#include <esp_netif.h>

#define SCREEN_COLS 168
#define SCREEN_STRIDE ((SCREEN_COLS - 1) / 8 + 1)
#define SCREEN_ROWS 384

#define FORECAST_DURATION_DAYS 2
#define FORECAST_HOURLY_POINT_COUNT FORECAST_DURATION_DAYS * 24
struct Forecast {
    float latitude;
    float longitude;
    uint32_t utc_offset_seconds;
    const char *timezone_abbreviation;
    struct Hourly {
        uint64_t time[FORECAST_HOURLY_POINT_COUNT];
        float temperature_2m[FORECAST_HOURLY_POINT_COUNT];
        uint8_t weather_code_2m[FORECAST_HOURLY_POINT_COUNT];
    } hourly;
    time_t updated_at;
};

typedef enum {
    GUI_BOOT = 0,
    GUI_WIFI_INIT,
    GUI_SYNC_TIME,
    GUI_HOME,
    GUI_COUNT
} gui_screen_t;

typedef struct {
    gui_screen_t current_screen;
    const struct Forecast forecast;
    esp_netif_t* netif;
} gui_data_t;

void gui_render(bitui_t ctx, const gui_data_t *data);
