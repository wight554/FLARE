#pragma once
/* Host shim for pico/stdlib.h — supplies only what firmware/include headers
 * reference: `uint`, `absolute_time_t`, and the two time-conversion calls.
 * Real values come from tests/host/sim_fakes.c, driven by simulated g_now_ms.
 * No wall-clock read here or anywhere else in tests/host/. */

#include <stdint.h>

typedef unsigned int uint;
typedef uint64_t absolute_time_t;

absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t t);
void sleep_ms(uint32_t ms);

/* absolute_time_t counts simulated microseconds since boot (get_absolute_time()
 * returns g_now_ms * 1000), so this is exact subtraction — no real hardware
 * timer to model. */
static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return (int64_t)to - (int64_t)from;
}
