#pragma once
/// @brief CSV trace emission and the seven global invariants, checked every
///        tick of every scenario. See design.md "Assertion Model".

#include <stdbool.h>
#include <stdint.h>
#include "sim_plant.h"

void sim_trace_header(void);
// Emits one CSV row and evaluates all invariants for this tick. Returns NULL
// on success, or a static diagnostic string identifying the first violation
// (the run stops at the first violation, per design.md's fail-loud stance).
const char *sim_trace_tick(uint32_t t_ms, const sim_plant_t *plant, int active_lane);

// Liveness (invariant 3) backstop, in simulated ms. One universal value, not
// a per-state table — see design.md "Why liveness, not per-state timeouts".
// Kept comfortably above BL_WATCHDOG_DEFAULT_MS (30000 — sync.c) so a
// scenario deliberately exercising that real 30s watchdog doesn't race this
// generic backstop; found via sem_bl_watchdog_timeout tripping this
// invariant ~160ms before the watchdog itself fired.
#define SIM_LIVENESS_BACKSTOP_MS 45000u
// Non-oscillation (invariant 5): max entries into any one sync state per run.
#define SIM_MAX_STATE_ENTRIES 50
// Saturation bound (invariant 6), in simulated ms.
#define SIM_MAX_SATURATION_MS 20000u
// Event rate ceiling (invariant 7): max cmd_event calls in any one tick.
#define SIM_MAX_EVENTS_PER_TICK 16
