idf_component_register(SRCS "eink-dashboard.c"
                    INCLUDE_DIRS "."
                    REQUIRES ssd1680
                    REQUIRES esp_driver_spi
                    REQUIRES esp_driver_gpio
                    REQUIRES bitui
                      )

