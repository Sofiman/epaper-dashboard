#pragma once

typedef int esp_err_t;
typedef void* i2c_master_dev_handle_t;
enum {
    I2C_ADDR_BIT_LEN_7
};
typedef struct {
    int dev_addr_length;
    int device_address;
    int scl_speed_hz;
} i2c_device_config_t;
