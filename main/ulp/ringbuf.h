#pragma once

#include <stdint.h>

#define RingBufStatic(T, Cap) struct ringbuf__ ## T ## __ ## Cap { \
    uint16_t start; \
    uint16_t count; \
    T items[(Cap)]; \
}

#define ringbuf_cap(RingBuf) (sizeof((RingBuf)->items)/sizeof(*(RingBuf)->items))
#define ringbuf_check_cap_power_of_2(Cap) (sizeof(struct {_Static_assert(((Cap) != 0) && (((Cap) & ((Cap) - 1)) == 0), "ringbuf capacity must be a power of 2"); }) + (Cap))
_Static_assert(ringbuf_check_cap_power_of_2(2) == 2);
#define ringbuf_emplace(RingBuf) ((RingBuf)->items + ringbuf_push(&(RingBuf)->start, &(RingBuf)->count, ringbuf_check_cap_power_of_2(ringbuf_cap(RingBuf)) - 1))
#define ringbuf_newest_nth(RingBuf, I) (((RingBuf)->start + (RingBuf)->count - (1 + (I))) & (ringbuf_cap(RingBuf) - 1))
#define ringbuf_newest(RingBuf) ringbuf_newest_nth((RingBuf), 0)
#define ringbuf_next(RingBuf, I) (((I) + 1) & (ringbuf_cap(RingBuf) - 1))

static inline uint16_t ringbuf_push(volatile uint16_t *start, volatile uint16_t *count, uint16_t cap_power_of_2_minus_one) {
    uint16_t count_val = *count;
    if (count_val <= cap_power_of_2_minus_one) {
        *count = count_val + 1;
        return count_val;
    }
    uint16_t new_item = *start;
    *start = (new_item + 1) & cap_power_of_2_minus_one;
    return new_item;
}
