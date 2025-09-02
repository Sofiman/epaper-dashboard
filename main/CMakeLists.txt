idf_component_register(SRCS "eink-dashboard.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_spi
                    REQUIRES esp_driver_gpio
                    REQUIRES esp_wifi
                    REQUIRES esp-tls
                    REQUIRES esp_timer
                    REQUIRES driver
                    REQUIRES ulp

                    REQUIRES ssd1680
                    REQUIRES bitui
                    REQUIRES immjson
                    REQUIRES gui
                    REQUIRES sht4x
                    REQUIRES scd4x
                      )

ulp_add_project("ulp_eink_dashboard" "${CMAKE_SOURCE_DIR}/main/ulp/")
