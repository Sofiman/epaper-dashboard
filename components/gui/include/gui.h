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

#define RingBuf(T) struct { \
    T* items; \
    size_t capacity; \
    size_t start, end; \
}

typedef RingBuf(uint16_t) TempData;
typedef RingBuf(uint16_t) RelHumData;
typedef RingBuf(uint16_t) CO2Data;

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
    struct Forecast forecast;
    const TempData *temp_data;
    const RelHumData *rel_hum_data;
    const CO2Data *co2_data;
} gui_data_t;

void gui_render(bitui_t ctx, const gui_data_t *data);
