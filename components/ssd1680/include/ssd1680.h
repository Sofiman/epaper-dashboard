#pragma once

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

typedef enum {
    SSD1680_BLACK = 0,
    SSD1680_WHITE = 1,
} ssd1680_color_t;

/// Configurations of the spi_ssd1680
typedef struct {
    spi_host_device_t host; ///< The SPI host used, set before calling `spi_ssd1680_init()`
    gpio_num_t cs_pin;       ///< CS gpio number, set before calling `spi_ssd1680_init()`
    gpio_num_t dc_pin;     ///< D/C gpio number, set before calling `spi_ssd1680_init()`
    gpio_num_t busy_pin;     ///< BUSY gpio number, set before calling `spi_ssd1680_init()`
    gpio_num_t reset_pin;     ///< RESET gpio number, set before calling `spi_ssd1680_init()`
    uint16_t cols; // X axis, 1 to 176
    uint16_t rows; // Y axis, 16 to 296
    uint8_t *framebuffer;
} ssd1680_config_t;

typedef struct ssd1680_context_t* ssd1680_handle_t;

esp_err_t ssd1680_init(const ssd1680_config_t *config, ssd1680_handle_t* out_handle);

esp_err_t ssd1680_deinit(ssd1680_handle_t *ctx);

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} ssd1680_rect_t;

esp_err_t ssd1680_flush(ssd1680_handle_t handle, ssd1680_rect_t rect);

esp_err_t ssd1680_full_refresh(ssd1680_handle_t handle);
