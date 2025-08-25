#pragma once

#include "bitui.h"
#include "sht4x.h"
#include "../../../main/ulp/ringbuf.h"

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

typedef struct [[gnu::packed]] {
    uint64_t ext_timestamp;
    sht4x_raw_sample_t sht4x_raw_sample;
    uint16_t co2_ppm;
} ulp_sample_t;
_Static_assert(sizeof(ulp_sample_t) == 14);
typedef RingBufStatic(ulp_sample_t, 32) ulp_sample_ringbuf_t;

typedef enum {
    GUI_BOOT = 0,
    GUI_WIFI_INIT,
    GUI_HOME,
    GUI_COUNT
} gui_screen_t;

typedef struct {
    gui_screen_t current_screen;
    uint32_t tick;

    // Home screen
    const struct Forecast *forecast;
    const ulp_sample_ringbuf_t *samples;
} gui_data_t;

void gui_render(bitui_t ctx, const gui_data_t *data);
