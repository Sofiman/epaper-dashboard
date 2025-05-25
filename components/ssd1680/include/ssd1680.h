#pragma once

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

/// Configurations of the spi_ssd1680
typedef struct {
    spi_host_device_t host; ///< The SPI host used, set before calling `spi_ssd1680_init()`
    uint16_t lines; // 16 to 296
    gpio_num_t cs_pin;       ///< CS gpio number, set before calling `spi_ssd1680_init()`
    gpio_num_t dc_pin;     ///< D/C gpio number, set before calling `spi_ssd1680_init()`
    gpio_num_t busy_pin;     ///< BUSY gpio number, set before calling `spi_ssd1680_init()`
    gpio_num_t reset_pin;     ///< RESET gpio number, set before calling `spi_ssd1680_init()`
} ssd1680_config_t;

typedef struct ssd1680_context_t* ssd1680_handle_t;

esp_err_t ssd1680_init(const ssd1680_config_t *config, ssd1680_handle_t* out_handle);

esp_err_t ssd1680_full_refresh(ssd1680_handle_t handle);

esp_err_t ssd1680_test_pattern(ssd1680_handle_t h);

esp_err_t ssd1680_deinit(ssd1680_handle_t *ctx);
