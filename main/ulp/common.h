#pragma once

#include "ringbuf.h"

typedef uint64_t ulp_sample_flags_t;
static inline ulp_sample_flags_t ulp_sample_flags_from(uint64_t timestamp_48bits, uint8_t sht4x_err, uint8_t scd4x_err) {
    return ((uint64_t)scd4x_err << 56) | ((uint64_t)sht4x_err << 48) | timestamp_48bits;
}

static inline uint8_t ulp_sample_flags_timestamp(ulp_sample_flags_t t) {
    return t & 0xffffffffffff;
}

static inline uint8_t ulp_sample_flags_sht4x(ulp_sample_flags_t t) {
    return (t >> 48) & 0xff;
}

static inline uint8_t ulp_sample_flags_scd4x(ulp_sample_flags_t t) {
    return (t >> 56) & 0xff;
}

typedef struct [[gnu::packed]] {
    ulp_sample_flags_t flags;
    sht4x_raw_sample_t sht4x_raw_sample;
    uint16_t co2_ppm;
} ulp_sample_t;
_Static_assert(sizeof(ulp_sample_t) == 14);
typedef RingBufStatic(ulp_sample_t, 32) ulp_sample_ringbuf_t;
