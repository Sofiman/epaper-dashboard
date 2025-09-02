#pragma once

#include "sensirion_common.h"

#include <stdint.h>
#include <stdbool.h>

#define SCD4x_I2C_ADDR 0x62

typedef enum {
    SCD4x_I2C_STANDARD_MODE  = 100000,  // 100 Khz
    SCD4x_I2C_FAST_MODE      = 400000,  // 400 Khz
} scd4x_i2c_speed_t;

#ifndef SCD4x_LP_CORE_I2C
#include "driver/i2c_master.h"

typedef i2c_master_dev_handle_t scd4x_handle_t;

static inline i2c_device_config_t scd4x_i2c_config(scd4x_i2c_speed_t speed) {
    return (i2c_device_config_t) {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SCD4x_I2C_ADDR,
        .scl_speed_hz = speed,
    };
}
#else
#include "ulp_lp_core_i2c.h"
#include "ulp_lp_core_utils.h"

typedef i2c_port_t scd4x_handle_t;
#endif

typedef enum : uint16_t {
    // Basic commands
    SCD4x_START_PERIODIC_MEASUREMENT              = 0x21B1,
    SCD4x_READ_MEASUREMENT                        = 0xEC05,
    SCD4x_STOP_PERIODIC_MEASUREMENT               = 0x3F86,

    // On-chip output signal compensation
    SCD4x_SET_TEMPERATURE_OFFSET                  = 0x241D,
    SCD4x_GET_TEMPERATURE_OFFSET                  = 0x2318,
    SCD4x_SET_SENSOR_ALTITUDE                     = 0x2427,
    SCD4x_GET_SENSOR_ALTITUDE                     = 0x2322,
    SCD4x_SET_GET_AMBIENT_PRESSURE                = 0xE000,

    // Field calibration
    SCD4x_PERFORM_FORCED_RECALIBRATION            = 0x362F,
    SCD4x_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED  = 0x2416,
    SCD4x_GET_AUTOMATIC_SELF_CALIBRATION_ENABLED  = 0x2313,
    SCD4x_SET_AUTOMATIC_SELF_CALIBRATION_TARGET   = 0x243A,
    SCD4x_GET_AUTOMATIC_SELF_CALIBRATION_TARGET   = 0x233F,

    // Low power periodic measurement mode
    SCD4x_START_LOW_POWER_PERIODIC_MEASUREMENT    = 0x21AC,
    SCD4x_GET_DATA_READY_STATUS                   = 0xE4B8,

    // Advanced features
    SCD4x_PERSIST_SETTINGS                        = 0x3615,
    SCD4x_GET_SERIAL_NUMBER                       = 0x3682,
    SCD4x_PERFORM_SELF_TEST                       = 0x3639,
    SCD4x_PERFORM_FACTORY_RESET                   = 0x3632,
    SCD4x_REINIT                                  = 0x3646,
    SCD4x_GET_SENSOR_VARIANT                      = 0x202F,

    // Single shot measurement mode (SCD41 only)
    SCD4x_MEASURE_SINGLE_SHOT                     = 0x219D,
    SCD4x_MEASURE_SINGLE_SHOT_RHT_ONLY            = 0x2196,
    SCD4x_POWER_DOWN                              = 0x36E0,
    SCD4x_WAKE_UP                                 = 0x36F6, // Note: SCD4x does not acknowledge the wake_up command
    SCD4x_SET_AUTOMATIC_SELF_CALIBRATION_INITIAL_PERIOD  = 0x2445,
    SCD4x_GET_AUTOMATIC_SELF_CALIBRATION_INITIAL_PERIOD  = 0x2340,
    SCD4x_SET_AUTOMATIC_SELF_CALIBRATION_STANDARD_PERIOD = 0x244E,
    SCD4x_GET_AUTOMATIC_SELF_CALIBRATION_STANDARD_PERIOD = 0x234B

    // Basic commands
    #define SCD4x_START_PERIODIC_MEASUREMENT_DURATION_MS 0
    #define SCD4x_READ_MEASUREMENT_DURATION_MS 1
    #define SCD4x_STOP_PERIODIC_MEASUREMENT_DURATION_MS 500

    // Field calibration
    #define SCD4x_PERFORM_FORCED_RECALIBRATION_DURATION_MS 400

    // Low power periodic measurement mode
    #define SCD4x_START_LOW_POWER_PERIODIC_MEASUREMENT_DURATION_MS 0

    // Advanced features
    #define SCD4x_PERSIST_SETTINGS_DURATION_MS 800
    #define SCD4x_PERFORM_SELF_TEST_DURATION_MS 10_000
    #define SCD4x_PERFORM_FACTORY_RESET_DURATION_MS 1200
    #define SCD4x_REINIT_DURATION_MS 30

    // Single shot measurement mode (SCD41 & SCD43)
    #define SCD4x_MEASURE_SINGLE_SHOT_DURATION_MS 5000
    #define SCD4x_MEASURE_SINGLE_SHOT_RHT_ONLY_DURATION_MS 50
    #define SCD4x_POWER_DOWN_DURATION_MS 1
    #define SCD4x_WAKE_UP_DURATION_MS 30
} scd4x_cmd_t;

typedef uint16_t scd4x_cmd_word_t;
typedef struct {
    uint16_t pressure_hpa; // hPa
} scd4x_cmd_ambient_pressure_t;
_Static_assert(sizeof(scd4x_cmd_ambient_pressure_t) == sizeof(scd4x_cmd_word_t), "");

typedef struct {
    uint16_t t_offset;
} scd4x_cmd_temp_offset_t;
_Static_assert(sizeof(scd4x_cmd_temp_offset_t) == sizeof(scd4x_cmd_word_t), "");
#define SCD4x_T_OFFSET_FROM_CELCIUS(TempCel) ((uint16_t)(((float)(TempCel) * UINT16_MAX)/175.0f))
#define SCD4x_T_CELCIUS_FROM_OFFSET(TempOffset) (((float)(TempOffset) * 175.0f)/UINT16_MAX)

typedef struct {
    uint16_t altitude_m;
} scd4x_cmd_altitude_t;
_Static_assert(sizeof(scd4x_cmd_altitude_t) == sizeof(scd4x_cmd_word_t), "");

typedef struct {
    uint16_t asc_enabled;
} scd4x_cmd_asc_t;
_Static_assert(sizeof(scd4x_cmd_asc_t) == sizeof(scd4x_cmd_word_t), "");

typedef struct {
    uint16_t asc_co2_target_ppm;
} scd4x_cmd_asc_target_t;
_Static_assert(sizeof(scd4x_cmd_asc_target_t) == sizeof(scd4x_cmd_word_t), "");

typedef struct {
    uint16_t asc_initial_period_hours;
} scd4x_cmd_asc_initial_period_t;
_Static_assert(sizeof(scd4x_cmd_asc_initial_period_t) == sizeof(scd4x_cmd_word_t), "");

typedef struct {
    uint16_t asc_standard_period_hours;
} scd4x_cmd_asc_standard_period_t;
_Static_assert(sizeof(scd4x_cmd_asc_standard_period_t) == sizeof(scd4x_cmd_word_t), "");

typedef struct {
    uint16_t co2_ppm;
    uint16_t raw_temperature;
    uint16_t raw_humidity;
} scd4x_cmd_read_measurement_t;
#define SCD4x_RAW_TEMPERATURE_TO_CELCIUS(RawTemperature) (-45.0f + ((float)(RawTemperature) * 175.0f)/UINT16_MAX)
#define SCD4x_RAW_HUMIDITY_TO_RH(RawHumidity) (((float)(RawHumidity) * 100.0f)/UINT16_MAX)

typedef struct {
    uint16_t status;
} scd4x_cmd_data_ready_status_t;
_Static_assert(sizeof(scd4x_cmd_data_ready_status_t) == sizeof(scd4x_cmd_word_t), "");
#define SCD4x_IS_DATA_READY(Status) (((Status) & 0x7fff) != 0)

typedef struct {
    uint16_t serial_number_parts[3];
} scd4x_cmd_serial_number_t;
#define SCD4x_FULL_SERIAL_NUMBER(SerialNumberParts) ((uint64_t)((SerialNumberParts)[0]) << 32 \
        | (uint64_t)((SerialNumberParts)[1]) << 16 | (uint64_t)((SerialNumberParts)[2]) << 0)

typedef struct {
    uint16_t malfunction_detected;
} scd4x_cmd_self_test_t;
#define SCD4x_NO_MALFUNCTION_DETECTED 0

enum scd4x_sensor_variant {
    SCD40 = 0x0,
    SCD41 = 0x1,
    SCD43 = 0x5,
};

typedef struct {
    uint16_t raw_variant;
} scd4x_cmd_sensor_variant_t;
#define SCD4x_SENSOR_VARIANT_FROM_RAW(RawVariant) ((enum scd4x_sensor_variant)((RawVariant) >> 12))

// If you have encountered a compiler error "... use of undeclared identifier
// ... _DURATION_MS" means that you are using the wrong function:
// - For *GET* commands use scd4x_get with the appropriate struct
// - For *SET* commands use scd4x_set with the appropriate struct
#define scd4x_cmd(Handle, Cmd) scd4x_cmd_((Handle), (Cmd), (Cmd ## _DURATION_MS))
esp_err_t scd4x_cmd_(scd4x_handle_t handle, scd4x_cmd_t cmd, uint16_t cmd_max_duration_us);

#define scd4x_set(Handle, Payload) scd4x_set_((Handle), _Generic((Payload), \
            scd4x_cmd_temp_offset_t: SCD4x_SET_TEMPERATURE_OFFSET, \
            scd4x_cmd_altitude_t: SCD4x_SET_SENSOR_ALTITUDE, \
            scd4x_cmd_ambient_pressure_t: SCD4x_SET_GET_AMBIENT_PRESSURE, \
            scd4x_cmd_asc_t: SCD4x_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED, \
            scd4x_cmd_asc_target_t: SCD4x_SET_AUTOMATIC_SELF_CALIBRATION_TARGET, \
            scd4x_cmd_asc_initial_period_t: SCD4x_SET_AUTOMATIC_SELF_CALIBRATION_INITIAL_PERIOD, \
            scd4x_cmd_asc_standard_period_t: SCD4x_SET_AUTOMATIC_SELF_CALIBRATION_STANDARD_PERIOD \
            ), *(scd4x_cmd_word_t*)&(Payload))
esp_err_t scd4x_set_(scd4x_handle_t handle, scd4x_cmd_t cmd, scd4x_cmd_word_t value);

#define scd4x_get(Handle, PayloadPtr) scd4x_get_((Handle), _Generic(*(PayloadPtr), \
            scd4x_cmd_read_measurement_t: SCD4x_READ_MEASUREMENT, \
            scd4x_cmd_temp_offset_t: SCD4x_GET_TEMPERATURE_OFFSET, \
            scd4x_cmd_altitude_t: SCD4x_GET_SENSOR_ALTITUDE, \
            scd4x_cmd_ambient_pressure_t: SCD4x_SET_GET_AMBIENT_PRESSURE, \
            scd4x_cmd_asc_t: SCD4x_GET_AUTOMATIC_SELF_CALIBRATION_ENABLED, \
            scd4x_cmd_asc_target_t: SCD4x_GET_AUTOMATIC_SELF_CALIBRATION_TARGET, \
            scd4x_cmd_data_ready_status_t: SCD4x_GET_DATA_READY_STATUS, \
            scd4x_cmd_serial_number_t: SCD4x_GET_SERIAL_NUMBER, \
            scd4x_cmd_sensor_variant_t: SCD4x_GET_SENSOR_VARIANT, \
            scd4x_cmd_asc_initial_period_t: SCD4x_GET_AUTOMATIC_SELF_CALIBRATION_INITIAL_PERIOD, \
            scd4x_cmd_asc_standard_period_t: SCD4x_GET_AUTOMATIC_SELF_CALIBRATION_STANDARD_PERIOD \
            ), (scd4x_cmd_word_t*)(PayloadPtr), sizeof(*(PayloadPtr))/sizeof(scd4x_cmd_word_t))
esp_err_t scd4x_get_(scd4x_handle_t handle, scd4x_cmd_t cmd, scd4x_cmd_word_t *out_words, uint8_t word_count);
