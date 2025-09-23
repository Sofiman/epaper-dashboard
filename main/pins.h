#pragma once

#include "driver/gpio.h"

#define PIN_GUARD(X) struct __pin_guard__ ## X {}
#define PIN(X, Name) PIN_GUARD(X); static const gpio_num_t Name = GPIO_NUM_ ## X

#ifdef LP_PINS
#include "ulp_lp_core_gpio.h"
#define LP_PIN(X, Name) PIN_GUARD(X); static const lp_io_num_t Name = LP_IO_NUM_ ## X
#else
#define LP_PIN(X, Name) PIN_GUARD(X)
#endif

// ESP32C6 strapping pins as shown in Table 2-5 in the chip's datasheet
//
// Summary of the strapping pins:
// 1. Chip boot mode pins as shown in Table 3-3:
//     - GPIO8
//     - GPIO9 [Weak pull-up]
// 2. SDIO JTAG Sampling and Driving Clock Edge:
//     - GPIO4 : MTMS
//     - GPIO5 : MTDI
//     - GPIO6 : MTCK (not enabled by default)
//     - GPIO7 : MTDO (not enabled by default)
// 3. GPIO8 control ROM messages printing to UART0 as shown in Table 3-5
// 4. GPIO15 can be used to control the source of JTAG signals during the early boot process as shown in Table 3-7

// ESP32C6 Super mini used pins
PIN( 8, PIN_WS2812);
PIN(15, PIN_ONBOARD_LED);

// EPD screen
PIN( 3, PIN_EPD_SPI_MOSI);
PIN( 2, PIN_EPD_SPI_SCLK);
PIN(18, PIN_EPD_BUSY);
PIN(14, PIN_EPD_RESET);
PIN( 4, PIN_EPD_DC); // Uses a strapping pin but is fine as D/C is only relevant when PIN_EPD_CS is high
PIN( 1, PIN_EPD_CS);

// SHT4x and SCD4x sensors are connected to peripheral LP_I2C_NUM_0
// Other pins CANNOT BE USED because of limitions of the low power I2C peripheral
PIN( 6, PIN_LP_I2C_SDA);
PIN( 7, PIN_LP_I2C_SCL);

// Heartbeat LED
LP_PIN(5, PIN_HB_LED); // Strapping pin but led is alright

// LD2410s human radar sensor
LP_PIN(0, PIN_LD2410S_OCCUPIED);
PIN(19, PIN_LD2410S_TX);
PIN(20, PIN_LD2410S_RX);

#undef LP_PIN
#undef PIN
