#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_task_wdt.h"

#include "sdkconfig.h"
#include "esp_log.h"
#include "ssd1680.h"

static const char *TAG = "main";

#define SCREEN_ROWS 296
#define SCREEN_COLS 128
static uint8_t framebuffer[((SCREEN_COLS - 1) / 8 + 1) * SCREEN_ROWS];

void app_main(void)
{
    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_NUM_7,
        .miso_io_num = -1,
        .sclk_io_num = GPIO_NUM_6,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    //Initialize the SPI bus
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    ssd1680_config_t ssd1680_config = {
        .host = SPI2_HOST,
        .busy_pin = GPIO_NUM_0,
        .reset_pin = GPIO_NUM_1,
        .dc_pin = GPIO_NUM_5,
        .cs_pin = GPIO_NUM_10,

        .rows = SCREEN_ROWS,
        .cols = SCREEN_COLS,
        .framebuffer = framebuffer
    };

    ssd1680_handle_t ssd1680_handle;

    ESP_LOGI(TAG, "Initializing device...");
    ret = ssd1680_init(&ssd1680_config, &ssd1680_handle);
    ESP_ERROR_CHECK(ret);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    memset(framebuffer, 0xff, sizeof(framebuffer));
    ret = ssd1680_flush(ssd1680_handle, (ssd1680_rect_t){
        .x = 0, .y = 0,
        .w = SCREEN_COLS, .h = SCREEN_ROWS
    });
    ESP_ERROR_CHECK(ret);
    ret = ssd1680_full_refresh(ssd1680_handle);
    ESP_ERROR_CHECK(ret);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    memset(framebuffer, 0xaa, sizeof(framebuffer));
    ret = ssd1680_flush(ssd1680_handle, (ssd1680_rect_t){
        .x = 32, .y = 116,
        .w = 64, .h = 64
    });
    ESP_ERROR_CHECK(ret);
    ret = ssd1680_full_refresh(ssd1680_handle);
    ESP_ERROR_CHECK(ret);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    ret = ssd1680_deinit(&ssd1680_handle);
    ESP_ERROR_CHECK(ret);

    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
