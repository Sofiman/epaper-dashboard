#include "sht4x.h"

#ifndef SHT4x_LP_CORE_I2C
#define sht4x_i2c_write i2c_master_transmit
#define sht4x_i2c_read i2c_master_receive
#include <freertos/FreeRTOS.h>
#define sht4x_delay_us(Us) vTaskDelay(((Us) - 1) / 1000 + 1)
#else
#define sht4x_i2c_write(Handle, Buf, Len, Timeout) lp_core_i2c_master_write_to_device((Handle), SHT4x_I2C_ADDR, (Buf), (Len), (Timeout))
#define sht4x_i2c_read(Handle, Buf, Len, Timeout) lp_core_i2c_master_read_from_device((Handle), SHT4x_I2C_ADDR, (Buf), (Len), (Timeout))
#define sht4x_delay_us(Us) ulp_lp_core_delay_us((Us) + 10)
#endif

#ifndef SHT4x_I2C_TIMEOUT
#define SHT4x_I2C_TIMEOUT 5000 // TODO: Unit is different depending SHT4x_LP_CORE_I2C. Clock cycles vs ms
#endif // !SHT4x_I2C_TIMEOUT

esp_err_t sht4x_cmd_(sht4x_handle_t handle, sht4x_cmd_t cmd, uint16_t cmd_max_duration_us)
{
    _Static_assert(sizeof(cmd) == 1);
    esp_err_t err = sht4x_i2c_write(handle, &cmd, sizeof(cmd), SHT4x_I2C_TIMEOUT);

    if (err == ESP_OK && cmd_max_duration_us > 0)
        sht4x_delay_us(cmd_max_duration_us);

    return err;
}

esp_err_t sht4x_read(sht4x_handle_t handle, sht4x_result_t *res)
{
    sensirion_word_t frames[2];

    _Static_assert(sizeof(frames) == 6, "SHT4x frame is always 6 bytes");
    esp_err_t err = sht4x_i2c_read(handle, (uint8_t*)&frames, sizeof(frames), SHT4x_I2C_TIMEOUT);
    if (err != ESP_OK) return err;

    if (sensirion_common_calculate_crc8(frames[0]) != frames[0].crc) return ESP_ERR_INVALID_CRC;
    if (sensirion_common_calculate_crc8(frames[1]) != frames[1].crc) return ESP_ERR_INVALID_CRC;

    res->_word0 = ((uint16_t)frames[0].data[0] << 8) | ((uint16_t)frames[0].data[1]);
    res->_word1 = ((uint16_t)frames[1].data[0] << 8) | ((uint16_t)frames[1].data[1]);

    return ESP_OK;
}
