#include <stdint.h>
#include <stdbool.h>
#include "ulp_lp_core.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_gpio.h"

#include "sht4x.h"
#include "ringbuf.h"

#define HB_LED_PIN LP_IO_NUM_5

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

typedef uint64_t ext_timestamp_t;
static inline ext_timestamp_t build_ext_timestamp(uint64_t timestamp_48bits, uint8_t opt1, uint8_t opt2) {
    return ((uint64_t)opt2 << 56) | ((uint64_t)opt1 << 48) | (timestamp_48bits/* & 0xffffffffffff*/);
}

typedef struct [[gnu::packed]] {
    ext_timestamp_t ext_timestamp;
    sht4x_raw_sample_t sht4x_raw_sample;
    uint16_t raw_co2;
} sample_t;
_Static_assert(sizeof(sample_t) == 14);
volatile RingBufStatic(sample_t, 32) sample_ringbuf;

volatile uint64_t last_lp_core_wakeup_rtc_ticks;

int main(void)
{
    ulp_lp_core_gpio_init(HB_LED_PIN);
    ulp_lp_core_gpio_set_output_mode(HB_LED_PIN, RTCIO_LL_OUTPUT_NORMAL);
    ulp_lp_core_gpio_output_enable(HB_LED_PIN);
    ulp_lp_core_gpio_set_level(HB_LED_PIN, 1);

    sample_t cur_sample = { 0 };

    sht4x_result_t sht4x_res = sht4x_cmd(LP_I2C_NUM_0, SHT4x_MEASURE_HIGH_PRECISION);
    cur_sample.sht4x_raw_sample = sht4x_raw_measurement(sht4x_res);

    uint64_t rtc_timer_val = ulp_lp_core_rtc_timer_read();
    cur_sample.ext_timestamp = build_ext_timestamp(rtc_timer_val, sht4x_res.err & 0xff, -1);

    *ringbuf_emplace(&sample_ringbuf) = cur_sample;

    last_lp_core_wakeup_rtc_ticks = rtc_timer_val;

    ulp_lp_core_gpio_set_level(HB_LED_PIN, 0);
    ulp_lp_core_wakeup_main_processor();

    return 0; /* ulp_lp_core_halt() is called automatically when main exits */
}
