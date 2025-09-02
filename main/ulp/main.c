#include <stdint.h>
#include "ulp_lp_core.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_gpio.h"

#define LP_PINS
#include "../pins.h"
#include "sht4x.h"
#include "scd4x.h"
#include "ringbuf.h"
#include "common.h"

#define RTC_TIMER_BASE 0x600B0C00
#define RTC_TIMER_UPDATE_REG (RTC_TIMER_BASE + 0x0010)
#define RTC_TIMER_MAIN_BUF0_LOW_REG (RTC_TIMER_BASE + 0x0014)
#define RTC_TIMER_MAIN_BUF0_HIGH_REG (RTC_TIMER_BASE + 0x0018)

uint64_t ulp_lp_core_rtc_timer_read(void)
{
    // If [Internal 90–150 kHz (depending on chip) RC oscillator] is used
    // The actual RC_SLOW frequency is 136kHz. The time will be measured at 7.352 μs resolution.
    // <!> The register only contains the number of ticks elasped, not the GMT time.
    SET_PERI_REG_MASK(RTC_TIMER_UPDATE_REG, BIT(28));
    uint32_t lo = READ_PERI_REG(RTC_TIMER_MAIN_BUF0_LOW_REG);
    uint32_t hi = READ_PERI_REG(RTC_TIMER_MAIN_BUF0_HIGH_REG);
    CLEAR_PERI_REG_MASK(RTC_TIMER_UPDATE_REG, BIT(28));
    return (((uint64_t)hi & 0xffff) << 32) | lo;
}

volatile ulp_sample_ringbuf_t sample_ringbuf;
volatile uint64_t last_lp_core_wakeup_rtc_ticks;

static inline esp_err_t measure_scd41(ulp_sample_t *sample) {
    esp_err_t ret;

    lp_core_i2c_master_set_ack_check_en(LP_I2C_NUM_0, false);
    ret = scd4x_cmd(LP_I2C_NUM_0, SCD4x_WAKE_UP);
    lp_core_i2c_master_set_ack_check_en(LP_I2C_NUM_0, true);
    if (ret != ESP_OK) goto defer;

    // TODO: To further reduce the sensor’s power consumption, the sensor may be power cycled between measurements either by cutting/re-
    //       applying the supply and I2C voltages or by using the power_down/wake_up commands. Note that for power-cycled single shot
    //       operation, ASC functionality is not available in either case.
    //       --> Maybe we don't need to disable ASC
    ret = scd4x_set(LP_I2C_NUM_0, (scd4x_cmd_asc_t) { .asc_enabled = false });
    if (ret != ESP_OK) goto defer;

    ret = scd4x_cmd(LP_I2C_NUM_0, SCD4x_MEASURE_SINGLE_SHOT);
    if (ret != ESP_OK) goto defer;

    scd4x_cmd_read_measurement_t scd4x_res = { 0 };
    ret = scd4x_get(LP_I2C_NUM_0, &scd4x_res);
    if (ret != ESP_OK) goto defer;

    sample->co2_ppm = scd4x_res.co2_ppm;
    /*if (sht4x_res.err != ESP_OK) {
        cur_sample.sht4x_raw_sample = (sht4x_raw_sample_t){
            .raw_temperature = scd4x_res.raw_temperature,
                .raw_humidity = scd4x_res.raw_humidity
        };
    }*/

defer:
    scd4x_cmd(LP_I2C_NUM_0, SCD4x_POWER_DOWN);
    return ret;
}

int main(void)
{
    // Power up
    ulp_lp_core_gpio_init(PIN_HB_LED);
    ulp_lp_core_gpio_set_output_mode(PIN_HB_LED, RTCIO_LL_OUTPUT_NORMAL);
    ulp_lp_core_gpio_output_enable(PIN_HB_LED);
    ulp_lp_core_gpio_set_level(PIN_HB_LED, 1);

    // Sample sensors
    ulp_sample_t cur_sample = { 0 };

    esp_err_t scd4x_err = measure_scd41(&cur_sample);

    sht4x_result_t sht4x_res = sht4x_cmd(LP_I2C_NUM_0, SHT4x_MEASURE_HIGH_PRECISION);
    cur_sample.sht4x_raw_sample = sht4x_raw_measurement(sht4x_res);

    // Push sample
    uint64_t rtc_timer_val = ulp_lp_core_rtc_timer_read();
    cur_sample.flags = ulp_sample_flags_from_parts(rtc_timer_val, sht4x_res.err & 0xff, scd4x_err & 0xff);
    *ringbuf_emplace(&sample_ringbuf) = cur_sample;
    last_lp_core_wakeup_rtc_ticks = rtc_timer_val;

    // Wake up main processor
    ulp_lp_core_gpio_set_level(PIN_HB_LED, 0);
    ulp_lp_core_wakeup_main_processor();

    return 0; /* ulp_lp_core_halt() is called automatically when main exits */
}
