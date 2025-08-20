#pragma once

#include "bitui.h"

#include <time.h>
#include <esp_netif.h>

#define SCREEN_COLS 168
#define SCREEN_ROWS 384
#define SCREEN_STRIDE ((SCREEN_COLS - 1) / 8 + 1)

#define FORECAST_DURATION_DAYS 2
#define FORECAST_HOURLY_POINT_COUNT FORECAST_DURATION_DAYS * 24
struct Forecast {
    float latitude;
    float longitude;
    struct ForecastHourly {
        // Unix time is signed
        int64_t time[FORECAST_HOURLY_POINT_COUNT];
        float temperature_2m[FORECAST_HOURLY_POINT_COUNT];
        uint8_t weather_code[FORECAST_HOURLY_POINT_COUNT];
    } hourly;
    struct ForecastDaily {
        // Unix time is signed
        int64_t time[FORECAST_DURATION_DAYS];
        int64_t sunrise[FORECAST_DURATION_DAYS];
        int64_t sunset[FORECAST_DURATION_DAYS];
    } daily;
    time_t updated_at;
};
_Static_assert(sizeof(((struct Forecast*)NULL)->hourly.time[0]) == sizeof(time_t));
_Static_assert(sizeof(((struct Forecast*)NULL)->daily.time[0]) == sizeof(time_t));

#define RingBuf(T, N) struct { \
    size_t start, count; \
    T items[N]; \
}

typedef RingBuf(float, 32) TempData;
typedef RingBuf(float, 32) RelHumData;
typedef RingBuf(uint16_t, 32) CO2Data;

#define for_ringbuf(RingBuf) for (long __rem = (RingBuf)->count, it = (RingBuf)->start; __rem > 0; __rem--, it = (it + 1) % 32)
#define ringbuf_newest(RingBuf) ((RingBuf)->items[((RingBuf)->start + (RingBuf)->count - 1) % 32])

typedef enum {
    GUI_BOOT = 0,
    GUI_WIFI_INIT,
    GUI_HOME,
    GUI_COUNT
} gui_screen_t;

typedef struct {
    gui_screen_t current_screen;
    uint32_t tick;

    // Boot screen
    esp_netif_t* netif;

    // Home screen
    const struct Forecast *forecast;
    const TempData *temp_data;
    const TempData *rel_hum_data;
    const CO2Data *co2_data;
} gui_data_t;

void gui_render(bitui_t ctx, const gui_data_t *data);
