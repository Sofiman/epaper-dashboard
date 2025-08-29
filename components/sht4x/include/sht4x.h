#pragma once

#include <stdint.h>

#define SHT4x_I2C_ADDR 0x44

typedef enum {
    SHT4x_I2C_STANDARD_MODE  = 100000,  // 100 Khz
    SHT4x_I2C_FAST_MODE      = 400000,  // 400 Khz
    SHT4x_I2C_FAST_MODE_PLUS = 1000000, //   1 Mhz
} sht4x_i2c_speed_t;

#ifndef SHT4x_LP_CORE_I2C
#include "driver/i2c_master.h"

typedef i2c_master_dev_handle_t sht4x_handle_t;

static inline i2c_device_config_t sht4x_i2c_config(sht4x_i2c_speed_t speed) {
    return (i2c_device_config_t) {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT4x_I2C_ADDR,
        .scl_speed_hz = speed,
    };
}
#else
#include "ulp_lp_core_i2c.h"
#include "ulp_lp_core_utils.h"

typedef i2c_port_t sht4x_handle_t;
#endif

typedef enum : uint8_t {
    /* Misc commands */

    SHT4x_SOFT_RESET = 0x94,
    SHT4x_READ_SERIAL_NUMBER = 0x89,

    /* Measurement commands */

    SHT4x_MEASURE_HIGH_PRECISION = 0xFD,
    SHT4x_MEASURE_MEDIUM_PRECISION = 0xF6,
    SHT4x_MEASURE_LOW_PRECISION = 0xE0,

    /* Heater & Measurement commands */

    SHT4x_HEAT__20mw__100ms_MEASURE_HIGH_PRECISION = 0x15,
    SHT4x_HEAT__20mw_1000ms_MEASURE_HIGH_PRECISION = 0x1E,
    SHT4x_HEAT_100mw__100ms_MEASURE_HIGH_PRECISION = 0x24,
    SHT4x_HEAT_100mw_1000ms_MEASURE_HIGH_PRECISION = 0x2F,
    SHT4x_HEAT_200mw__100ms_MEASURE_HIGH_PRECISION = 0x32,
    SHT4x_HEAT_200mw_1000ms_MEASURE_HIGH_PRECISION = 0x39,

#define SHT4x_SOFT_RESET_MAX_DURATION_US 1000u
#define SHT4x_READ_SERIAL_NUMBER_MAX_DURATION_US 0u

#define SHT4x_MEASURE_HIGH_PRECISION_MAX_DURATION_US 8300u
#define SHT4x_MEASURE_MEDIUM_PRECISION_DURATION_US 4500u
#define SHT4x_MEASURE_LOW_PRECISION_DURATION_US 1600u

#define SHT4x_HEAT__20mw__100ms_MEASURE_HIGH_PRECISION_MAX_DURATION_US (110000u + SHT4x_MEASURE_HIGH_PRECISION_MAX_DURATION_US)
#define SHT4x_HEAT_100mw__100ms_MEASURE_HIGH_PRECISION_MAX_DURATION_US (110000u + SHT4x_MEASURE_HIGH_PRECISION_MAX_DURATION_US)
#define SHT4x_HEAT_200mw__100ms_MEASURE_HIGH_PRECISION_MAX_DURATION_US (110000u + SHT4x_MEASURE_HIGH_PRECISION_MAX_DURATION_US)

#define SHT4x_HEAT__20mw_1000ms_MEASURE_HIGH_PRECISION_MAX_DURATION_US (1000000u + SHT4x_MEASURE_HIGH_PRECISION_MAX_DURATION_US)
#define SHT4x_HEAT_100mw_1000ms_MEASURE_HIGH_PRECISION_MAX_DURATION_US (1000000u + SHT4x_MEASURE_HIGH_PRECISION_MAX_DURATION_US)
#define SHT4x_HEAT_200mw_1000ms_MEASURE_HIGH_PRECISION_MAX_DURATION_US (1000000u + SHT4x_MEASURE_HIGH_PRECISION_MAX_DURATION_US)
} sht4x_cmd_t;

typedef struct {
    // uint8_t must be used to keep the alignment to 1 byte
    uint8_t data[sizeof(uint16_t)];
    uint8_t crc;
} sht4x_word_t;

int sht4x_verify_crc8(sht4x_word_t word);

typedef struct {
    uint16_t raw_temperature;
    uint16_t raw_humidity;
} sht4x_raw_sample_t;

typedef struct {
    float temperature_celcius;
    float relative_humidity;
} sht4x_sample_t;

// TODO: Use fixed point arithmetic
static inline sht4x_sample_t sht4x_convert(sht4x_raw_sample_t in) {
    sht4x_sample_t sample;
    sample.temperature_celcius = -45.0f + 175.0f * (float)(in.raw_temperature)/65535.0f;
    sample.relative_humidity = -6.0f + 125.0f * (float)(in.raw_humidity)/65535.0f;
    return sample;
}

typedef struct {
    esp_err_t err;
    sht4x_word_t frames[2];
} sht4x_result_t;

#define sht4x_cmd(Handle, Cmd) sht4x_cmd_((Handle), (Cmd), (Cmd ## _MAX_DURATION_US))
sht4x_result_t sht4x_cmd_(sht4x_handle_t handle, sht4x_cmd_t cmd, uint16_t cmd_max_duration_us);

static inline sht4x_raw_sample_t sht4x_raw_measurement(sht4x_result_t result) {
    sht4x_raw_sample_t measurement;
    measurement.raw_temperature = ((uint16_t)result.frames[0].data[0] << 8) | ((uint16_t)result.frames[0].data[1]);
    measurement.raw_humidity    = ((uint16_t)result.frames[1].data[0] << 8) | ((uint16_t)result.frames[1].data[1]);
    return measurement;
}

static inline uint32_t sht4x_serial_number(sht4x_result_t result) {
    return ((uint32_t)result.frames[0].data[0]) << 24
        | ((uint32_t)result.frames[0].data[1])  << 16
        | ((uint32_t)result.frames[1].data[0])  <<  8
        | ((uint32_t)result.frames[1].data[1])  <<  0;
}
