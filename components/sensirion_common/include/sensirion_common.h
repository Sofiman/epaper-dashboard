#pragma once

#include <stdint.h>

typedef struct {
    // uint8_t must be used to keep the alignment to 1 byte
    uint8_t data[sizeof(uint16_t)];
    uint8_t crc;
} sensirion_word_t;

#define sensirion_common_calculate_crc8(Word) _Generic((Word), \
        uint16_t: sensirion_common_calculate_crc8_u16, \
        sensirion_word_t: sensirion_common_calculate_crc8_word \
        )(Word)
uint8_t sensirion_common_calculate_crc8_u16(uint16_t word);
static inline uint8_t sensirion_common_calculate_crc8_word(sensirion_word_t word) {
    return sensirion_common_calculate_crc8_u16(((uint16_t)word.data[0]) | ((uint16_t)word.data[1]) << 8);
}
