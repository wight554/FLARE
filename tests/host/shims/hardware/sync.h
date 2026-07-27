#pragma once
/* Host shim for hardware/sync.h. settings_store.c brackets its flash write
 * with these; the sim is single-threaded so they are no-ops. */

#include <stdint.h>

static inline uint32_t save_and_disable_interrupts(void) {
    return 0;
}

static inline void restore_interrupts(uint32_t saved) {
    (void)saved;
}
