idf_component_register(SRCS "eink-dashboard.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_spi
                    REQUIRES esp_driver_gpio
                    REQUIRES esp_wifi
                    REQUIRES esp-tls

                    REQUIRES ssd1680
                    REQUIRES bitui
                    REQUIRES immjson
                    REQUIRES gui
                      )

