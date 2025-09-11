#include <stdint.h>
#include "ulp_lp_core.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_memory_shared.h"
#include "ulp_lp_core_lp_timer_shared.h"

#define LP_PINS
#include "../pins.h"
#include "sht4x.h"
#include "scd4x.h"
#include "ringbuf.h"
#include "common.h"

volatile ulp_sample_ringbuf_t sample_ringbuf;
volatile uint64_t last_lp_core_wakeup_rtc_ticks;

enum ulp_task {
    ULP_TRIGGER_MEASUREMENT = 0,
    ULP_COLLECT_MEASUREMENT,
};
volatile enum ulp_task current_task;

#ifdef SOC_LP_TIMER_SUPPORTED
#define ULP_SET_NEXT_WAKE_UP_TICKS(Ticks) do { \
    ulp_lp_core_memory_shared_cfg_get()->sleep_duration_ticks = (Ticks); \
} while (0)
#else
#error "LP TIMER is required for sleeping ULP while waiting for sensor measurements. See esp-idf/components/ulp/lp_core/lp_core/lp_core_startup.c"
#endif

static inline void collect_measurement(void) {
    ulp_sample_t cur_sample = { 0 };
    esp_err_t sht4x_err;
    sht4x_result_t sht4x_res;
    esp_err_t scd4x_err;
    scd4x_cmd_read_measurement_t scd4x_res = { 0 };

    sht4x_err = sht4x_cmd_(LP_I2C_NUM_0, SHT4x_MEASURE_HIGH_PRECISION, 0 /* no wait */);

    { // While waiting for SHT4x
        scd4x_err = scd4x_get(LP_I2C_NUM_0, &scd4x_res);
        scd4x_cmd(LP_I2C_NUM_0, SCD4x_POWER_DOWN);
        cur_sample.co2_ppm = scd4x_res.co2_ppm;
    }

    if (sht4x_err == ESP_OK) {
        ulp_lp_core_delay_us(SHT4x_MEASURE_HIGH_PRECISION_MAX_DURATION_US - SCD4x_READ_MEASUREMENT_DURATION_MS * 1000u - SCD4x_POWER_DOWN_DURATION_MS * 1000u + 10u);

        sht4x_err = sht4x_read(LP_I2C_NUM_0, &sht4x_res);
        cur_sample.sht4x_raw_sample = sht4x_res.sample;
    }

    if (sht4x_err != ESP_OK && scd4x_err == ESP_OK) {
        cur_sample.sht4x_raw_sample = (sht4x_raw_sample_t){
            .raw_temperature = scd4x_res.raw_temperature, // OK: Both have the same convert formula
            .raw_humidity = scd4x_res.raw_humidity // TODO: Both sensors have different convert formulas
        };
    }

    // Push sample
    uint64_t rtc_timer_val = ulp_lp_core_lp_timer_get_cycle_count();
    cur_sample.flags = ulp_sample_flags_from_parts(rtc_timer_val, sht4x_err & 0xff, scd4x_err & 0xff);
    *ringbuf_emplace(&sample_ringbuf) = cur_sample;

    // Wake up main processor
    ulp_lp_core_wakeup_main_processor();

    // ULP task cycling:
    // .............[trigger_measurement].......[collect_measurement]
    //    10min     ^                      5s
    //              |
    //              last_lp_core_wakeup_rtc_ticks

    ULP_SET_NEXT_WAKE_UP_TICKS(ulp_lp_core_lp_timer_calculate_sleep_ticks(ULP_WAKEUP_PERIOD_US - SCD4x_MEASURE_SINGLE_SHOT_DURATION_MS * 1000));

    current_task = ULP_TRIGGER_MEASUREMENT;
}

static inline void trigger_measurement(void) {
    // If [Internal 90–150 kHz (depending on chip) RC oscillator] is used
    // The actual RC_SLOW frequency is 136kHz. The time will be measured at 7.352 μs resolution.
    // <!> The register only contains the number of ticks elasped, not the GMT time.
    last_lp_core_wakeup_rtc_ticks = ulp_lp_core_lp_timer_get_cycle_count();

    esp_err_t ret;

    lp_core_i2c_master_set_ack_check_en(LP_I2C_NUM_0, false);
    ret = scd4x_cmd(LP_I2C_NUM_0, SCD4x_WAKE_UP);
    lp_core_i2c_master_set_ack_check_en(LP_I2C_NUM_0, true);
    if (ret != ESP_OK) goto skip_collect_delay;

    // TODO: To further reduce the sensor’s power consumption, the sensor may be power cycled between measurements either by cutting/re-
    //       applying the supply and I2C voltages or by using the power_down/wake_up commands. Note that for power-cycled single shot
    //       operation, ASC functionality is not available in either case.
    //       --> Maybe we don't need to disable ASC
    ret = scd4x_set(LP_I2C_NUM_0, (scd4x_cmd_asc_t) { .asc_enabled = false });
    if (ret != ESP_OK) goto skip_collect_delay;

    ret = scd4x_cmd_(LP_I2C_NUM_0, SCD4x_MEASURE_SINGLE_SHOT, 0 /* no wait */);
    if (ret != ESP_OK) goto skip_collect_delay;

    // ULP:   ...[trigger_measurement]....................[           collect_measurement            ]
    // SCD4x: ...[power_on seq]...[  measure_single_shot ]..[read_measurement][power_down]............
    // SHT4x: ............................................[        measure_high_precision         ]...

    ULP_SET_NEXT_WAKE_UP_TICKS(ulp_lp_core_lp_timer_calculate_sleep_ticks(SCD4x_MEASURE_SINGLE_SHOT_DURATION_MS * 1000 + 10u));
    current_task = ULP_COLLECT_MEASUREMENT;
    return;

skip_collect_delay:
    collect_measurement(); // CO2 measurement is expected to fail with NACK
}

int main(void)
{
    // Power up
    ulp_lp_core_gpio_init(PIN_HB_LED);
    ulp_lp_core_gpio_set_output_mode(PIN_HB_LED, RTCIO_LL_OUTPUT_NORMAL);
    ulp_lp_core_gpio_output_enable(PIN_HB_LED);
    ulp_lp_core_gpio_set_level(PIN_HB_LED, 1);

    switch (current_task) {
        case ULP_TRIGGER_MEASUREMENT: trigger_measurement(); break;
        case ULP_COLLECT_MEASUREMENT: collect_measurement(); break;
    }

    ulp_lp_core_gpio_set_level(PIN_HB_LED, 0);

    return 0; /* ulp_lp_core_halt() is called automatically when main exits */
}
