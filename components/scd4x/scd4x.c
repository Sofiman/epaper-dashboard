#include "scd4x.h"

#include <string.h>

#ifndef SCD4x_LP_CORE_I2C
#define scd4x_i2c_write i2c_master_transmit
#define scd4x_i2c_read i2c_master_receive
#include <freertos/FreeRTOS.h>
#define scd4x_delay_ms(Ms) vTaskDelay(Ms)
#else
#define scd4x_i2c_write(Handle, Buf, Len, Timeout) lp_core_i2c_master_write_to_device((Handle), SCD4x_I2C_ADDR, (Buf), (Len), (Timeout))
#define scd4x_i2c_read(Handle, Buf, Len, Timeout) lp_core_i2c_master_read_from_device((Handle), SCD4x_I2C_ADDR, (Buf), (Len), (Timeout))
#define scd4x_delay_ms(Ms) ulp_lp_core_delay_us((Ms) * 1000)
#endif

esp_err_t scd4x_cmd_(scd4x_handle_t handle, scd4x_cmd_t cmd, uint16_t cmd_max_duration_ms)
{
    _Static_assert(sizeof(cmd) == 2);
    esp_err_t ret = scd4x_i2c_write(handle, (uint8_t*)&cmd, sizeof(cmd), -1);
    if (ret != ESP_OK) return ret;

    if (cmd_max_duration_ms > 0)
        scd4x_delay_ms(cmd_max_duration_ms);

    return ret;
}

esp_err_t scd4x_set_(scd4x_handle_t handle, scd4x_cmd_t cmd, scd4x_cmd_word_t value)
{
    uint8_t buf[sizeof(cmd) + sizeof(sensirion_word_t)];

    memcpy(buf,                 &cmd,   sizeof(cmd));
    memcpy(buf + sizeof(cmd), &value, sizeof(value));
    buf[sizeof(cmd) + sizeof(value)] = sensirion_common_calculate_crc8((uint16_t)value);

    _Static_assert(sizeof(buf) == 5);
    esp_err_t ret = scd4x_i2c_write(handle, buf, sizeof(buf), -1);
    if (ret != ESP_OK) return ret;

    scd4x_delay_ms(1);

    return ret;
}

esp_err_t scd4x_get_(scd4x_handle_t handle, scd4x_cmd_t cmd, scd4x_cmd_word_t *out_words, uint8_t word_count)
{
#define SCD4x_GET_MAX_WORD_COUNT 3
    if (word_count == 0 || word_count > SCD4x_GET_MAX_WORD_COUNT)
        return ESP_ERR_INVALID_ARG;
    sensirion_word_t res[SCD4x_GET_MAX_WORD_COUNT];

    esp_err_t ret = scd4x_cmd_(handle, cmd, 1);
    if (ret != ESP_OK) return ret;

    ret = scd4x_i2c_read(handle, (uint8_t*)&res, word_count * sizeof(res[0]), -1);
    if (ret != ESP_OK) return ret;

    for (int i = 0; i < word_count; i++) {
        const uint16_t word = res[i].data[1] << 16 | res[i].data[0];
        if (sensirion_common_calculate_crc8(word) != res[i].crc) ret = ESP_ERR_INVALID_CRC;
        out_words[i] = word;
    }

    return ret;
}
