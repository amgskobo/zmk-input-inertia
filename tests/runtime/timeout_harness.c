/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#ifdef INERTIA_TIMEOUT_32
typedef uint32_t k_ticks_t;
#else
typedef int64_t k_ticks_t;
#endif
typedef struct { k_ticks_t ticks; } k_timeout_t;
static uint32_t clock_hz;
#define CONFIG_SYS_CLOCK_TICKS_PER_SEC clock_hz
#define K_TICKS(value) ((k_timeout_t){(value)})

/* DRIVER_FUNCTIONS */

#ifdef INERTIA_TIMEOUT_WIDTH_PROBE
k_ticks_t probe_timeout(uint16_t ms, uint32_t hz) {
    clock_hz = hz;
    return inertia_timeout_ms(ms).ticks;
}
#else
int main(void) {
    static const uint32_t rates[] = {
        0, 1, 10, 100, 128, 1000, 1024, 32768, 65535, 65536, 65537,
        1000000, INT32_MAX, UINT32_MAX,
    };
    for (unsigned h = 0; h < sizeof(rates) / sizeof(rates[0]); ++h) {
        clock_hz = rates[h];
        for (uint32_t ms = 0; ms <= UINT16_MAX; ++ms) {
            const k_ticks_t expected = (k_ticks_t)(((uint64_t)ms * clock_hz + 999U) / 1000U);
            assert(inertia_ticks_from_ms((uint16_t)ms, clock_hz) == expected);
            assert(inertia_timeout_ms((uint16_t)ms).ticks == expected);
        }
    }
    puts("inertia timeout all65536 waits at14 clock rates, exact ceil and kernel tick width: PASS");
    return 0;
}
#endif
