#pragma once

#include "driver/gpio.h"

#define PIN_GUARD(X) struct __pin_guard__ ## X {}
#define PIN(Name, X) PIN_GUARD(X); static const gpio_num_t Name = GPIO_NUM_ ## X

#ifdef LP_PINS
#include "ulp_lp_core_gpio.h"
#define LP_PIN(Name, X) PIN_GUARD(X); static const lp_io_num_t Name = LP_IO_NUM_ ## X
#else
#define LP_PIN(Name, X) PIN_GUARD(X)
#endif

// EPD screen
PIN(PIN_EPD_SPI_MOSI, 3);
PIN(PIN_EPD_SPI_SCLK, 2);
PIN(PIN_EPD_BUSY, 19);
PIN(PIN_EPD_RESET, 14);
PIN(PIN_EPD_DC, 18);
PIN(PIN_EPD_CS, 4);

// SHT4x and SCD4x sensors are connected to peripheral LP_I2C_NUM_0
PIN(PIN_LP_I2C_SDA, 6);
PIN(PIN_LP_I2C_SCL, 7);

// Heartbeat LED
LP_PIN(PIN_HB_LED, 5);

#undef PIN
