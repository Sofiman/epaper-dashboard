#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "driver/uart.h"

#define LD2410S_BAUDRATE 115200

typedef enum : uint16_t {
    LD2410S_READ_FIRMWARE_VERSION = 0x0000,

    LD2410S_AUTO_UPDATE_THRESHOLD = 0x0009,
    LD2410S_WRITE_SERIAL_NUMBER = 0x0010,
    LD2410S_READ_SERIAL_NUMBER = 0x0011,

    LD2410S_WRITE_GENERIC_PARAMS = 0x0070,
    LD2410S_READ_GENERIC_PARAMS = 0x0071,
    LD2410S_WRITE_TRIGGER_THRESHOLD = 0x0072,
    LD2410S_READ_TRIGGER_THRESHOLD = 0x0073,
    LD2410S_WRITE_SNR_THRESHOLD = 0x0074,
    LD2410S_READ_SNR_THRESHOLD = 0x0075,
    LD2410S_WRITE_HOLD_THRESHOLD = 0x0076,
    LD2410S_READ_HOLD_THRESHOLD = 0x0077,

    LD2410S_SWITCH_OUTPUT_FORMAT = 0x007A,

    LD2410S_END_CONFIGURATION = 0x00FE,
    LD2410S_BEGIN_CONFIGURATION = 0x00FF,
} ld2410s_cmd_t;
#define LD2410S_CMD_ACK(Cmd) ((Cmd) | 0x0100)

typedef enum : uint16_t {
    LD2410S_STANDARD_REPORTING = 0x0000,
    LD2410S_MINIMAL_REPORTING = 0x0001,
} ld2410s_report_mode_t;

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} ld2410s_firmware_ver_t;

#define LD2410S_ENABLE_CONFIGURATION_VALUE ((uint16_t)0x0001)

typedef enum : uint16_t {
                                             // Range of value
    LD2410S_P_FARTHEST_DISTANCE_GATE = 0x05, // 1-16
    LD2410S_P_NEAREST_DISTANCE_GATE  = 0x0A, // 0-16
    LD2410S_P_UNMANNED_DELAY_TIME    = 0x06, // 10-120 s
    LD2410S_P_STATUS_REPORT_FREQ     = 0x02, // 0.5-8 (0.5 step) Hz multiplied by 10. Ex: 4.0 Hz -> 40
    LD2410S_P_DISTANCE_REPORT_FREQ   = 0x0C, // 0.5-8 (0.5 step) Hz multiplied by 10. Ex: 4.0 Hz -> 40
    LD2410S_P_RESPONSE_SPEED         = 0x0B, // ld2410s_response_speed_t
} ld2410s_param_word_t;

typedef enum : uint32_t {
    LD2410S_RESPONSE_NORMAL = 5,
    LD2410S_RESPONSE_FAST = 10,
} ld2410s_response_speed_t;

typedef struct [[gnu::packed]] {
    union {
        uint16_t gate_idx;
        ld2410s_param_word_t word;
    };
    uint32_t value;
} ld2410s_param_t;
_Static_assert(sizeof(ld2410s_param_t) == 6, "");

typedef struct ld2410s_cfg *ld2410s_cfg_t;
typedef struct ld2410s_handle *ld2410s_t;

esp_err_t ld2410s_init(uart_port_t uart, ld2410s_t *handle, int tx_pin, int rx_pin);

ld2410s_cfg_t ld2410s_cfg_begin(ld2410s_t handle);
esp_err_t ld2410s_cfg_set_reporting_mode(ld2410s_cfg_t cfg, ld2410s_report_mode_t mode);
#define ld2410s_cfg_write_params(Cfg, Cmd, Params, ParamsSize) ld2410s_cfg_write_params_((Cfg), (Cmd), (Params), (ParamsSize) + sizeof(struct { _Static_assert(((Cmd) & 1) == 0 && (Cmd) >= LD2410S_WRITE_GENERIC_PARAMS && (Cmd) <= LD2410S_WRITE_HOLD_THRESHOLD, "ld2410s_cfg_write_params must be used with a WRITE command"); }))
esp_err_t ld2410s_cfg_write_params_(ld2410s_cfg_t cfg, ld2410s_cmd_t cmd, const ld2410s_param_t *params, uint8_t params_size);
//esp_err_t ld2410s_cfg_auto_update_threshold(ld2410s_cfg_t cfg, uint16_t trigger_factor, uint16_t retention_factor, uint16_t scanning_time_s);
esp_err_t ld2410s_cfg_end(ld2410s_cfg_t cfg);

typedef enum : uint8_t {
    LD2410S_UNOCCUPIED   = 0,
    LD2410S_UNOCCUPIED_1 = 1,
    LD2410S_OCCUPIED     = 2,
    LD2410S_OCCUPIED_2   = 3,
} ld2410s_target_state_t;

typedef struct [[gnu::packed]] {
    ld2410s_target_state_t target_state;
    uint16_t target_distance_cm;
} ld2410s_minimal_report_t;
esp_err_t ld2410s_poll_minimal_report(ld2410s_t handle, ld2410s_minimal_report_t *report);
