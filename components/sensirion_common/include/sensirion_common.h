#pragma once

#include <stdint.h>

typedef struct {
    // uint8_t must be used to keep the alignment to 1 byte
    uint8_t data[sizeof(uint16_t)];
    uint8_t crc;
} sensirion_word_t;

#define sensirion_common_calculate_crc8(Word) sensirion_common_calculate_crc8_(_Generic((Word), \
        uint16_t: (Word), \
        const sensirion_word_t*: ((Word)->data[0] | (Word)->data[1] << 16), \
        sensirion_word_t*: ((Word)->data[0] | (Word)->data[1] << 16) \
        ))
uint8_t sensirion_common_calculate_crc8_(uint16_t word);
