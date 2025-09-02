#include "scd4x.h"

#ifndef SCD4x_LP_CORE_I2C
#define scd4x_i2c_write i2c_master_transmit
#define scd4x_i2c_read i2c_master_receive
#include <freertos/FreeRTOS.h>
#define scd4x_delay_ms(Ms) vTaskDelay(pdMS_TO_TICKS(Ms))
#else
#define scd4x_i2c_write(Handle, Buf, Len, Timeout) lp_core_i2c_master_write_to_device((Handle), SCD4x_I2C_ADDR, (Buf), (Len), (Timeout))
#define scd4x_i2c_read(Handle, Buf, Len, Timeout) lp_core_i2c_master_read_from_device((Handle), SCD4x_I2C_ADDR, (Buf), (Len), (Timeout))
#define scd4x_delay_ms(Ms) ulp_lp_core_delay_us((Ms) * 1000)
#endif

esp_err_t scd4x_cmd_(scd4x_handle_t handle, scd4x_cmd_t cmd, uint16_t cmd_max_duration_ms)
{
    const uint16_t address_be = __builtin_bswap16(cmd);
    _Static_assert(sizeof(cmd) == sizeof(address_be));
    esp_err_t ret = scd4x_i2c_write(handle, (const uint8_t*)&address_be, sizeof(address_be), -1);

    if (ret == ESP_OK && cmd_max_duration_ms > 0)
        scd4x_delay_ms(cmd_max_duration_ms);

    return ret;
}

struct [[gnu::packed]] scd4x_write_seq {
    // Data sent to and received from the sensor consists of a sequence of 16-bit commands and/or 16-bit words (each to be interpreted
    // as unsigned integer with the most significant byte transmitted first)
    // -> Address and data are in big endian

    uint16_t address_be;
    uint16_t data_be;
    uint8_t data_crc8;
};
_Static_assert(sizeof(struct scd4x_write_seq) == 5); // Page 7 of SD4x Data Sheet

#define SCD4x_CMD_SET_DURATION_MS 1
#define SCD4x_CMD_GET_DURATION_MS 1

esp_err_t scd4x_set_(scd4x_handle_t handle, scd4x_cmd_t cmd, scd4x_cmd_word_t value)
{
    struct scd4x_write_seq sequence = {
        .address_be = __builtin_bswap16(cmd),
        .data_be = __builtin_bswap16(value),
    };
    sequence.data_crc8 = sensirion_common_calculate_crc8(sequence.data_be);

    esp_err_t ret = scd4x_i2c_write(handle, (const uint8_t*)&sequence, sizeof(sequence), -1);

    if (ret == ESP_OK)
        scd4x_delay_ms(SCD4x_CMD_SET_DURATION_MS);

    return ret;
}

esp_err_t scd4x_get_(scd4x_handle_t handle, scd4x_cmd_t cmd, scd4x_cmd_word_t *out_words, uint8_t word_count)
{
#define SCD4x_GET_MAX_WORD_COUNT 3
    if (word_count == 0 || word_count > SCD4x_GET_MAX_WORD_COUNT)
        return ESP_ERR_INVALID_ARG;
    sensirion_word_t res[SCD4x_GET_MAX_WORD_COUNT];

    esp_err_t ret = scd4x_cmd_(handle, cmd, SCD4x_CMD_GET_DURATION_MS);
    if (ret != ESP_OK) return ret;

    ret = scd4x_i2c_read(handle, (uint8_t*)&res, word_count * sizeof(res[0]), -1);
    if (ret != ESP_OK) return ret;

    for (int i = 0; i < word_count; i++) {
        if (sensirion_common_calculate_crc8(res[i]) != res[i].crc) ret = ESP_ERR_INVALID_CRC;
        out_words[i] = ((uint16_t)res[i].data[1]) | ((uint16_t)res[i].data[0]) << 8; // Big Endian to Little Endian
    }

    return ret;
}
