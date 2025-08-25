#include "sht4x.h"

int sht4x_verify_crc8(const sht4x_word_t word)
{
    // CRC8(0xBEEF) = 0x92
    enum {
        SHT4x_CRC8_INITIALIZATION = 0xff,
        SHT4x_CRC8_POLYNOMIAL = 0x31, // x^8 + x^5 + x^4 + 1)
        SHT4x_CRC8_FINAL_XOR = 0,
    };

    uint8_t crc = SHT4x_CRC8_INITIALIZATION;
    for (size_t i = 0; i < sizeof(word.data); i++) {
        crc ^= word.data[i];
        for (size_t j = 0; j < 8; j++) {
            uint8_t new_crc = crc << 1;
            if ((crc & 0x80) != 0)
                new_crc ^= SHT4x_CRC8_POLYNOMIAL;
            crc = new_crc;
        }
    }
    crc ^= SHT4x_CRC8_FINAL_XOR;
    return crc == word.crc;
}

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

sht4x_result_t _sht4x_cmd(sht4x_handle_t handle, sht4x_cmd_t cmd, uint16_t cmd_max_duration_us)
{
    sht4x_result_t res = { 0 };

    _Static_assert(sizeof(cmd) == 1);
    res.err = sht4x_i2c_write(handle, &cmd, sizeof(cmd), -1);
    if (res.err != ESP_OK) return res;

    if (cmd_max_duration_us > 0)
        sht4x_delay_us(cmd_max_duration_us);

    if (cmd == SHT4x_SOFT_RESET) return res;

    _Static_assert(sizeof(res.frames) == 6, "SHT4x frame is always 6 bytes");
    res.err = sht4x_i2c_read(handle, (uint8_t*)&res.frames, sizeof(res.frames), -1);
    if (res.err != ESP_OK) return res;

    if (!sht4x_verify_crc8(res.frames[0])) res.err = ESP_ERR_INVALID_CRC;
    if (!sht4x_verify_crc8(res.frames[1])) res.err = ESP_ERR_INVALID_CRC;

    return res;
}
