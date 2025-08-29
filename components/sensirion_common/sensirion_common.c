#include "sensirion_common.h"

uint8_t sensirion_common_calculate_crc8_(uint16_t word)
{
    // CRC8(0xBEEF) = 0x92
    enum {
        CRC8_INITIALIZATION = 0xff,
        CRC8_POLYNOMIAL = 0x31, // x^8 + x^5 + x^4 + 1)
        CRC8_FINAL_XOR = 0,
    };

    uint8_t crc = CRC8_INITIALIZATION;
    for (int i = 0; i < sizeof(word); i++, word >>= 8) {
        crc ^= word & 0xff;
        for (int j = 0; j < 8; j++) {
            uint8_t new_crc = crc << 1;
            if ((crc & 0x80) != 0)
                new_crc ^= CRC8_POLYNOMIAL;
            crc = new_crc;
        }
    }
    crc ^= CRC8_FINAL_XOR;
    return crc;
}
