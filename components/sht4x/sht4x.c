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

sht4x_result_t sht4x_cmd_(sht4x_handle_t handle, sht4x_cmd_t cmd, uint16_t cmd_max_duration_us)
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

    if (sensirion_common_calculate_crc8(&res.frames[0]) != res.frames[0].crc) res.err = ESP_ERR_INVALID_CRC;
    if (sensirion_common_calculate_crc8(&res.frames[1]) != res.frames[1].crc) res.err = ESP_ERR_INVALID_CRC;

    return res;
}
