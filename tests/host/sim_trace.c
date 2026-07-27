#include "sim_trace.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "controller_shared.h"
#include "sync.h"
#include "sync_internal.h"

#include "sim_fakes.h"

static const char *state_name(sync_state_t s) {
    switch (s) {
    case SYNC_OFF:
        return "OFF";
    case SYNC_ACTIVE:
        return "ACTIVE";
    case SYNC_RETRACT_ASSIST:
        return "RETRACT_ASSIST";
    case SYNC_RELIEF_PAUSE:
        return "RELIEF_PAUSE";
    case SYNC_FAULT_HOLD:
        return "FAULT_HOLD";
    }
    return "?";
}

static bool is_transient(sync_state_t s) {
    return s == SYNC_RETRACT_ASSIST || s == SYNC_RELIEF_PAUSE;
}

// Per-process trace/invariant state — one scenario per process (design.md
// "One scenario per process"), so this never needs resetting between runs.
static sync_state_t s_prev_state = SYNC_OFF;
static uint32_t s_state_entered_ms = 0;
static int s_state_entry_count[5] = {0};
static uint32_t s_saturation_since_ms = 0;
static bool s_first_tick = true;
static char s_diag[256];

void sim_trace_header(void) {
    printf("ts_ms,bp_mm,zone,feed_sps,demand_mm_s,sync_state,sat,events\n");
}

const char *sim_trace_tick(uint32_t t_ms, const sim_plant_t *plant, int active_lane) {
    // ---- CSV row ----
    const char *sat = plant->sat_compression ? "C" : (plant->sat_tension ? "T" : "");
    char events[512] = {0};
    size_t off = 0;
    for (int i = 0; i < g_sim_event_count && off < sizeof(events) - 1; i++) {
        int n = snprintf(events + off, sizeof(events) - off, "%s%s%s", off ? ";" : "",
                         g_sim_events[i].critical ? "!" : "", g_sim_events[i].text);
        if (n > 0)
            off += (size_t)n;
    }

    // events itself contains commas ("TYPE,DATA[;TYPE,DATA...]") — CSV-quote
    // it so a plain csv.DictReader doesn't split it into extra columns and
    // shift every field after it on any row with an event. No literal
    // double-quotes ever appear in event text, so no escaping is needed.
    printf("%u,%.4f,%s,%d,%.4f,%s,%s,\"%s\"\n", t_ms, plant->slack_mm,
          buf_state_name(buf_state_raw()), g_sync_current_sps, plant->last_demand_mm_s,
          state_name(g_sync_state), sat, events);

    g_sim_event_count = 0; // drained

    // ---- Invariant 1: finiteness ----
    if (!isfinite(plant->slack_mm) || !isfinite((double)g_sync_current_sps) ||
        !isfinite(g_extruder_est_sps) || !isfinite(g_buf_pos)) {
        snprintf(s_diag, sizeof(s_diag), "invariant 1 (finiteness) violated at t=%u", t_ms);
        return s_diag;
    }

    // ---- Invariant 2: bounds. sync.c:1898 clamps the final value to
    // [0, max_sps], not [min_sps, max_sps] — g_sync_min_sps is only an
    // intermediate target floor in specific paths (assist/demand floors,
    // type-P smoothing ramp-up), so transient sub-min values are legitimate,
    // not a bug. The hard invariant the firmware actually holds is the upper
    // clamp and non-negativity. ----
    if (g_sync_current_sps < 0 || g_sync_current_sps > g_sync_max_sps) {
        snprintf(s_diag, sizeof(s_diag),
                "invariant 2 (bounds) violated at t=%u: feed_sps=%d outside [0,%d]", t_ms,
                g_sync_current_sps, g_sync_max_sps);
        return s_diag;
    }

    // ---- Invariant 5: non-oscillation + state-entry bookkeeping for invariant 3 ----
    bool just_entered = s_first_tick || g_sync_state != s_prev_state;
    if (just_entered) {
        s_state_entry_count[g_sync_state]++;
        s_state_entered_ms = t_ms;
        if (s_state_entry_count[g_sync_state] > SIM_MAX_STATE_ENTRIES) {
            snprintf(s_diag, sizeof(s_diag),
                    "invariant 5 (non-oscillation) violated at t=%u: state %s entered %d times",
                    t_ms, state_name(g_sync_state), s_state_entry_count[g_sync_state]);
            return s_diag;
        }
    }
    s_first_tick = false;
    s_prev_state = g_sync_state;

    // ---- Invariant 3: liveness (transient states only) ----
    if (is_transient(g_sync_state) && (t_ms - s_state_entered_ms) > SIM_LIVENESS_BACKSTOP_MS) {
        snprintf(s_diag, sizeof(s_diag),
                "invariant 3 (liveness) violated at t=%u: stuck in %s since t=%u", t_ms,
                state_name(g_sync_state), s_state_entered_ms);
        return s_diag;
    }

    // ---- Invariant 4: fault quiescence. The entry tick's own announcement
    // event (e.g. "SYNC,FAULT_HOLD") is expected and exempted; it is
    // continued emission on later ticks while still in FAULT_HOLD that is
    // the deadlock-loop signature. ----
    if (g_sync_state == SYNC_FAULT_HOLD) {
        if (g_sync_current_sps != 0 || (off > 0 && !just_entered)) {
            snprintf(s_diag, sizeof(s_diag),
                    "invariant 4 (fault quiescence) violated at t=%u: feed_sps=%d events=%s",
                    t_ms, g_sync_current_sps, events);
            return s_diag;
        }
    }

    // ---- Invariant 6: saturation bound ----
    if (plant->sat_compression || plant->sat_tension) {
        if (s_saturation_since_ms == 0)
            s_saturation_since_ms = t_ms;
        if ((t_ms - s_saturation_since_ms) > SIM_MAX_SATURATION_MS) {
            snprintf(s_diag, sizeof(s_diag),
                    "invariant 6 (saturation bound) violated at t=%u: saturated since t=%u", t_ms,
                    s_saturation_since_ms);
            return s_diag;
        }
    } else {
        s_saturation_since_ms = 0;
    }

    // ---- Invariant 7: event rate ----
    if ((int)off > 0) {
        int n_events = 1;
        for (size_t i = 0; i < off; i++)
            if (events[i] == ';')
                n_events++;
        if (n_events > SIM_MAX_EVENTS_PER_TICK) {
            snprintf(s_diag, sizeof(s_diag),
                    "invariant 7 (event rate) violated at t=%u: %d events in one tick", t_ms,
                    n_events);
            return s_diag;
        }
    }

    (void)active_lane;
    return NULL;
}
