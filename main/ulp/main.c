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

#define sht4x_i2c_write(Handle, Buf, Len, Timeout) lp_core_i2c_master_write_to_device((Handle), SHT4x_I2C_ADDR, (Buf), (Len), (Timeout))
#define sht4x_i2c_read(Handle, Buf, Len, Timeout) lp_core_i2c_master_read_from_device((Handle), SHT4x_I2C_ADDR, (Buf), (Len), (Timeout))
static inline sht4x_result_t sht4x_measure(void)
{
    sht4x_result_t res = { 0 };

    const uint8_t reg = 0xFD;
    res.err = sht4x_i2c_write(LP_I2C_NUM_0, &reg, sizeof(reg), /* TODO */ 5000);
    if (res.err != ESP_OK) return res;

    ulp_lp_core_delay_cycles(134000); // 1142/16000000 = ~8.375ms

    sht4x_word_t frame[2];
    _Static_assert(sizeof(frame) == 6, "SHT4x frame is always 6 bytes");

    res.err = sht4x_i2c_read(LP_I2C_NUM_0, (uint8_t*)&frame, sizeof(frame), /* TODO */ 5000);
    if (res.err != ESP_OK) return res;

    if (!sht4x_verify_crc8(frame[0])) res.err = ESP_ERR_INVALID_CRC;
    if (!sht4x_verify_crc8(frame[1])) res.err = ESP_ERR_INVALID_CRC;
    if (res.err != ESP_OK) return res;

    res.measurement.raw_temperature = ((uint16_t)frame[0].data[0] << 8) | ((uint16_t)frame[0].data[1]);
    res.measurement.raw_humidity    = ((uint16_t)frame[1].data[0] << 8) | ((uint16_t)frame[1].data[1]);

    return res;
}

int main(void)
{
    ulp_lp_core_gpio_init(HB_LED_PIN);
    ulp_lp_core_gpio_set_output_mode(HB_LED_PIN, RTCIO_LL_OUTPUT_NORMAL);
    ulp_lp_core_gpio_output_enable(HB_LED_PIN);
    ulp_lp_core_gpio_set_level(HB_LED_PIN, 1);

    sample_t cur_sample = { 0 };

    sht4x_result_t sht4x_res = sht4x_measure();
    cur_sample.sht4x_raw_sample = sht4x_res.measurement;

    uint64_t rtc_timer_val = ulp_lp_core_rtc_timer_read();
    cur_sample.ext_timestamp = build_ext_timestamp(rtc_timer_val, sht4x_res.err & 0xff, -1);

    *ringbuf_emplace(&sample_ringbuf) = cur_sample;

    last_lp_core_wakeup_rtc_ticks = rtc_timer_val;

    ulp_lp_core_gpio_set_level(HB_LED_PIN, 0);
    ulp_lp_core_wakeup_main_processor();

    return 0; /* ulp_lp_core_halt() is called automatically when main exits */
}
