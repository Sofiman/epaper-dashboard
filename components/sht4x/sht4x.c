#include "sht4x.h"

#include <freertos/FreeRTOS.h>

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

const uint8_t SHT4x_CMD_TO_REGISTER[] = {
    [SHT4x_SOFT_RESET] = 0x94,
    [SHT4x_READ_SERIAL_NUMBER] = 0x89,

    [SHT4x_MEASURE_HIGH_PRECISION] = 0xFD,
    [SHT4x_MEASURE_MEDIUM_PRECISION] = 0xF6,
    [SHT4x_MEASURE_LOW_PRECISION] = 0xE0,

    [SHT4x_HEAT__20mw__100ms_MEASURE_HIGH_PRECISION] = 0x15,
    [SHT4x_HEAT__20mw_1000ms_MEASURE_HIGH_PRECISION] = 0x1E,
    [SHT4x_HEAT_100mw__100ms_MEASURE_HIGH_PRECISION] = 0x24,
    [SHT4x_HEAT_100mw_1000ms_MEASURE_HIGH_PRECISION] = 0x2F,
    [SHT4x_HEAT_200mw__100ms_MEASURE_HIGH_PRECISION] = 0x32,
    [SHT4x_HEAT_200mw_1000ms_MEASURE_HIGH_PRECISION] = 0x39,
};

const uint16_t SHT4x_CMD_MAX_DURATION_MS[] = {
    // Operations timings in ms ............ typ  max
    [SHT4x_SOFT_RESET] = 1,               // 0.3    1
    [SHT4x_READ_SERIAL_NUMBER] = 0,       // ---  ---

    [SHT4x_MEASURE_HIGH_PRECISION] = 9,   // 6.9  8.3
    [SHT4x_MEASURE_MEDIUM_PRECISION] = 5, // 3.7  4.5
    [SHT4x_MEASURE_LOW_PRECISION] = 2,    // 1.3  1.6

    // [heater ON duration ± 10%] + [SHT4x_MEASURE_HIGH_PRECISION duration]
    [SHT4x_HEAT__20mw__100ms_MEASURE_HIGH_PRECISION] =  110 + 9,
    [SHT4x_HEAT__20mw_1000ms_MEASURE_HIGH_PRECISION] = 1100 + 9,
    [SHT4x_HEAT_100mw__100ms_MEASURE_HIGH_PRECISION] =  110 + 9,
    [SHT4x_HEAT_100mw_1000ms_MEASURE_HIGH_PRECISION] = 1100 + 9,
    [SHT4x_HEAT_200mw__100ms_MEASURE_HIGH_PRECISION] =  110 + 9,
    [SHT4x_HEAT_200mw_1000ms_MEASURE_HIGH_PRECISION] = 1100 + 9,
};

sht4x_result_t sht4x_cmd(sht4x_handle_t handle, sht4x_cmd_t cmd)
{
    sht4x_result_t res;

    const uint8_t reg = SHT4x_CMD_TO_REGISTER[cmd];
    res.err = i2c_master_transmit(handle, &reg, sizeof(reg), /* TODO */ 10);
    if (res.err != ESP_OK) return res;

    const uint16_t delay_ms = SHT4x_CMD_MAX_DURATION_MS[cmd];
    if (delay_ms > 0)
        vTaskDelay(delay_ms);

    if (cmd == SHT4x_SOFT_RESET) return res;

    sht4x_word_t frame[2];
    _Static_assert(sizeof(frame) == 6, "SHT4x frame is always 6 bytes");

    res.err = i2c_master_receive(handle, (uint8_t*)&frame, sizeof(frame), /* TODO */ 10);
    if (res.err != ESP_OK) return res;

    if (!sht4x_verify_crc8(frame[0])) res.err = ESP_ERR_INVALID_CRC;
    if (!sht4x_verify_crc8(frame[1])) res.err = ESP_ERR_INVALID_CRC;
    if (res.err != ESP_OK) return res;

    if (cmd == SHT4x_READ_SERIAL_NUMBER) {
        res.serial_number = ((uint32_t)frame[0].data[0]) << 24
            | ((uint32_t)frame[0].data[1]) << 16
            | ((uint32_t)frame[1].data[0]) <<  8
            | ((uint32_t)frame[1].data[1]) <<  0;
    } else {
        res.measurement.raw_temperature = ((uint16_t)frame[0].data[0] << 8) | ((uint16_t)frame[0].data[1]);
        res.measurement.raw_humidity    = ((uint16_t)frame[1].data[0] << 8) | ((uint16_t)frame[1].data[1]);
    }

    return res;
}
