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

typedef enum {
    // Soft and hard reset take max 1ms
    SHT4x_SOFT_RESET,
    SHT4x_READ_SERIAL_NUMBER,

    // Measurement takes max 9ms
    SHT4x_MEASURE_HIGH_PRECISION,
    // Measurement takes max 5ms
    SHT4x_MEASURE_MEDIUM_PRECISION,
    // Measurement takes max 2ms
    SHT4x_MEASURE_LOW_PRECISION,

    SHT4x_HEAT__20mw__100ms_MEASURE_HIGH_PRECISION,
    SHT4x_HEAT__20mw_1000ms_MEASURE_HIGH_PRECISION,
    SHT4x_HEAT_100mw__100ms_MEASURE_HIGH_PRECISION,
    SHT4x_HEAT_100mw_1000ms_MEASURE_HIGH_PRECISION,
    SHT4x_HEAT_200mw__100ms_MEASURE_HIGH_PRECISION,
    SHT4x_HEAT_200mw_1000ms_MEASURE_HIGH_PRECISION,
} sht4x_cmd_t;

extern const uint8_t SHT4x_CMD_TO_REGISTER[];
extern const int8_t SHT4x_CMD_MAX_DURATION_MS[];
#define SHT4x_CMD_DURATION_HEATER__100ms -1
#define SHT4x_CMD_DURATION_HEATER_1000ms -2
#define SHT4x_MAX_DURATION_MS_OF_CMD(Cmd) (SHT4x_CMD_MAX_DURATION_MS[(Cmd)] >= 0 ? (uint16_t)SHT4x_CMD_MAX_DURATION_MS[Cmd] : (9 + (SHT4x_CMD_MAX_DURATION_MS[Cmd] == SHT4x_CMD_DURATION_HEATER__100ms) ? 110 : 1100))

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
    union {
        sht4x_raw_sample_t measurement;
        uint32_t serial_number;
    };
} sht4x_result_t;

sht4x_result_t sht4x_cmd(sht4x_handle_t handle, sht4x_cmd_t cmd);
