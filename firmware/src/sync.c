/// @file sync.c
/// @brief Sync orchestration: the per-tick sync controller, buffer-lock service,
///        boot stabilization, and sync auto-toggle. Buffer sensing lives in
///        sync_buf.c; Type-D law in sync_relay.c; Type-P law in sync_analog.c.
/// @details Drives lane feed so the buffer rests in-band against extruder demand.
///          See BEHAVIOR.md "Buffer Lock", "Sync mode auto-toggle"; spec sync-refactor.

#include "sync_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cutter.h"
#include "motion.h"
#include "protocol.h"
#include "toolchange.h"

enum {
    BUFFER_STABILIZE_WINDOW_MS = 10000u,
    FLOW_BIAS_MILLI_SCALE = 1000,
    FLOW_BIAS_MILLI_MAX = 700,
    BASELINE_SETTLE_MIN_DWELL_MS = 500,
    SETTLE_HISTORY_MAX_COUNT = 16,
    SETTLE_HISTORY_LAST_INDEX = SETTLE_HISTORY_MAX_COUNT - 1,
    SYNC_FAST_BRAKE_MS = 250u,
    SYNC_POST_COMPRESSION_BOOST_MS = 300u,
    SYNC_MMU_DWELL_SAMPLE_MAX = 10000,
    SYNC_MMU_DWELL_DECAY_DIVISOR = 2,
    SYNC_TENSION_DWELL_WARN_INTERVAL_MS = 10000u,
    SYNC_TENSION_RISK_WARN_INTERVAL_MS = 30000u,
    SYNC_STATUS_EVENT_INTERVAL_MS = 500u,
    SYNC_STATUS_EVENT_MAX = 48,
    SYNC_RECENT_NEGATIVE_RELEASE_MARGIN_MS = 300u,
};

static const float TYPE_P_RAIL_NORM = 0.99f;
static const float SECONDS_PER_MINUTE_F = 60.0f;
static const float ROUND_TO_NEAREST_F = 0.5f;
static const float BASELINE_MIN_MEAN_SPS = 0.1f;
static const float TYPE_D_NEUTRAL_DEMAND_MARGIN = 1.05f;
static const float BL_FALLBACK_TRAVEL_CAP_MM = 25.0f;
static const float BL_FALLBACK_HALF_TRAVEL_MM = BL_FALLBACK_TRAVEL_CAP_MM * HALF_F;
static const float BL_FOLLOW_DT_MIN_S = 0.0001f;
static const float BL_FOLLOW_DT_MAX_S = 0.1f;
static const float TYPE_P_AUTO_START_POS_NORM = -0.6f;
static const float TYPE_P_AUTO_START_VEL_NORM = -0.1f;
static const float DRIFT_WALL_TAPER_MULT = 2.0f;
static const float DRIFT_WALL_TAPER_MIN_MM = 0.5f;
static const float DRIFT_CONFIDENCE_BIAS_FRAC = 0.8f;
static const float RESERVE_INTEGRAL_MIN_CF = 0.7f;
static const float RESERVE_INTEGRAL_WARN_FRAC = 0.5f;
static const float VARIANCE_BLEND_REF_MIN_MM = 0.05f;
static const float VARIANCE_BLEND_REF_FALLBACK_MM = 1.0f;
static const float BUFFER_THRESHOLD_MIN_MM = 0.001f;
static const float TYPE_D_DEADBAND_NORM = 0.1f;
static const float UNCERTAINTY_PROBE_BIAS_SPS = 6.0f;
static const float PSF_FILTER_MIN_MM = 0.01f;

sync_state_t g_sync_state = SYNC_OFF;
bool g_sync_auto_started = false;
bool g_sync_tail_assist_active = false;
uint32_t g_sync_idle_since_ms = 0;
int g_sync_current_sps = 0;

int g_baseline_target_sps = CONF_BASELINE_SPS;
int g_baseline_sps = CONF_BASELINE_SPS;
float g_baseline_alpha = FLARE_INT_BASELINE_ALPHA;
const flow_schedule_point_t G_FLOW_SCHED_CONFIG[CONF_FLOW_SCHED_CAP] = CONF_FLOW_SCHED;
flow_schedule_point_t g_flow_sched_runtime[CONF_FLOW_SCHED_CAP] = CONF_FLOW_SCHED;
int g_flow_sched_live_delta[CONF_FLOW_SCHED_CAP] = {0};
int g_flow_sched_len = CONF_FLOW_SCHED_LEN;
uint32_t g_sync_fast_brake_until_ms = 0;

bool g_sync_compression_recovery_active = false;
uint32_t g_sync_continuous_compression_since_ms = 0;
uint32_t g_sync_post_compression_boost_until_ms = 0;
uint32_t g_sync_recent_negative_until_ms = 0;
uint32_t g_sync_tension_pin_since_ms = 0;

bool g_sync_cannot_refill_warned = false;
bool g_sync_cannot_relieve_warned = false;

uint32_t g_sync_last_tick_ms = 0;
uint32_t g_sync_last_evt_ms = 0;
float g_extruder_est_sps = 0.0f;
float g_extruder_est_prev_sps = 0.0f;
uint32_t g_extruder_est_last_update_ms = 0;
uint32_t g_last_slope_update_ms = 0;

/* Buffer-lock (BL) lifecycle sub-states — active while g_sync_state == SYNC_RETRACT_ASSIST */
typedef enum {
    BL_IDLE = 0,
    BL_PRIME,  /* driving lane toward armed extreme at SYNC_MAX_SPS */
    BL_LOCKED, /* holding at extreme; motor energized, zero net feed */
    BL_FOLLOW, /* event-triggered follow-on retract concurrent with extruder */
} bl_sub_state_t;

static bl_sub_state_t g_bl_sub_state = BL_IDLE;
static buf_state_t g_bl_target_state = BUF_TENSION;
buf_state_t g_bl_goal_override = BUF_NEUTRAL;
static uint32_t g_bl_prime_start_ms = 0;     /* when prime search began */
static float g_bl_prime_mm_per_s = 0.0f;     /* speed in mm/s */
static float g_bl_prime_cap_mm = 0.0f;       /* outer safety cap = BUF_MAX_TRAVEL_MM */
static int g_bl_prime_cur_sps = 0;           /* PRIME current ramped rate */
static int g_bl_prime_target_sps = 0;        /* PRIME target rate (clamped) */
static uint32_t g_bl_prime_ramp_tick_ms = 0; /* last PRIME ramp step */
static float g_bl_prime_traveled_mm = 0.0f;  /* tracked travel distance in mm */
static float g_bl_follow_mm = 0.0f;          /* armed follow-on distance; 0 = disabled */
static float g_bl_follow_rate_mmpm = 0.0f;   /* armed follow-on rate (mm/min) */
static uint32_t g_bl_follow_start_ms = 0;    /* when FOLLOW motion began */
static float g_bl_follow_mm_per_s = 0.0f;    /* FOLLOW commanded speed in mm/s */
static uint32_t g_bl_watchdog_ms = 0;
static float g_bl_follow_traveled_mm = 0.0f;
static uint32_t g_bl_last_tick_ms = 0;
static int g_bl_follow_cur_sps = 0;           /* FOLLOW current ramped rate */
static int g_bl_follow_target_sps = 0;        /* FOLLOW target rate (clamped) */
static uint32_t g_bl_follow_ramp_tick_ms = 0; /* last FOLLOW ramp step */
static uint32_t g_bl_arm_timeout_ms = 0;      /* configured watchdog window; 0 = default */
static int g_bl_follow_seed_sps = 0;          /* FOLLOW seed rate (floor for rate servo) */
static bool g_bl_lock_engaged = false;        /* lock confirmed in target rail zone */
static float g_bl_lock_extreme = 0.0f;        /* type-P: deepest g_buf_pos seen since LOCKED
                                                 (min for TENSION, max for COMPRESSION) */

/* Type-P bounded relief (D-01/D-02/D-22/REVIEW-07): tracks the deepest
   g_buf_pos observed since BUF_TENSION entry, independent of the BL
   extreme above (different lifecycle -- BL is armed/locked per event, this
   tracks continuously whenever sync is enabled). Reset at sync_disable(),
   sync_rearm_active(), and the AUTO_START path in
   sync_tick_auto_start_stop() -- a stale deep extreme from a previous
   window would make the relief zone unreachable and silently disable
   relief (the 51bdca8 failure class). */
static float g_sync_tension_extreme = 0.0f;
static bool g_sync_tension_extreme_valid = false;
static bool g_sync_relief_active = false; /* edge detection for RELIEF_ON/RELIEF_OFF */

bool g_bl_autostart_suppressed = false;
bool g_sync_tension_transitioned = false;

/* Phase 13 Plan 02 (D-08/D-09/D-10/D-12): gates both the new mm distance trip
   and the pre-existing ms dwell trip so they arm/disarm in lockstep off one
   shared flag. Set in sync_on_transition() on any observed buffer-state edge
   while g_sync_auto_started; cleared in sync_disable()/sync_rearm_active()/
   the AUTO_START path (the same three re-entry points g_sync_tension_extreme
   resets at) and on the falling edge of a deliberate rail hold (REVIEW-04,
   sync_trip_track_hold_edge() below). */
bool g_sync_trip_armed = false;
/* REVIEW-04: previous-tick value of sync_type_p_hold_in_progress(), sampled
   unconditionally at the very top of sync_tick() -- see the call site and
   sync_trip_track_hold_edge() comments below for why it must run above every
   early return in that function. */
static bool g_sync_prev_hold_active = false;

/* Phase 13 Plan 03 (D-15/D-16/D-19): type-P feed probe. Resolves the "+1.0
   tension" ambiguity -- filament IN sensor present but the buffer has been
   pinned in tension for a while -- by observing one probe distance's worth
   of pinned relief feed and deciding, from the BEST deflection observed
   across the whole window (REVIEW-02, g_sync_probe_peak_pos below), whether
   the buffer genuinely moved back off its deepest reading (CONSUMER: a real
   consumer is pulling and being refilled) or never did (NO_CONSUMER: the
   lane feeds against nothing, e.g. a jam or an empty spool). Numeric values
   are exactly D-19's 0/1/2/3 PR: encoding. */
typedef enum {
    SYNC_PROBE_NONE = 0,
    SYNC_PROBE_RUNNING = 1,
    SYNC_PROBE_CONSUMER = 2,
    SYNC_PROBE_NO_CONSUMER = 3,
} sync_probe_state_t;

static sync_probe_state_t g_sync_probe_state = SYNC_PROBE_NONE;
/* Once-per-episode latch (D-17): the probe decides at most once per pinned
   episode, even though the buffer may re-cross the probe threshold again
   before the episode ends (e.g. a subsequent BL hold, or the mm trip's own
   later crossing at 2x this distance). Reset alongside g_sync_probe_state at
   every site that resets the arm flag. */
static bool g_sync_probe_decided = false;
/* REVIEW-02: window-max deflection latch -- the least-tension-side (highest,
   since g_buf_pos is +compression/-tension) g_buf_pos observed since the
   probe window opened. The verdict compares THIS, not the instantaneous
   g_buf_pos at the threshold-crossing tick, against the tracked tension
   extreme -- a single boundary-tick sample is a coin flip on spring bounce,
   ADC noise, or a momentary demand pulse. Monotone in favour of CONSUMER by
   construction (fmaxf only ever raises it), so its own failure direction is
   "the probe declines to escalate", already backstopped by the distance and
   dwell trips. Re-seeded to the CURRENT g_buf_pos (never a constant) at
   every site that resets the probe latch -- a peak carried over from a
   previous episode reads as motion that never happened in this one and
   manufactures the mirror-image false CONSUMER. */
static float g_sync_probe_peak_pos = 0.0f;

bool g_boot_stabilizing = false;
uint32_t g_boot_stabilize_deadline_ms = 0;
lane_t *g_boot_stabilize_lane = NULL;
bool g_boot_stabilize_forward = false;
static bool g_buffer_stabilize_emit_events = false;
static uint32_t g_boot_stabilize_started_ms = 0;
static uint32_t g_stab_stagnant_since_ms = 0;
static float g_boot_stabilize_start_pos = 0.0f;

static buffer_service_mode_t g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
static uint32_t g_idle_compression_since_ms = 0;

int g_settle_history[SETTLE_HISTORY_LEN] = {0};
uint8_t g_settle_history_count = 0;
uint32_t g_last_baseline_update_ms = 0;
float g_last_baseline_update_mm = 0.0f;
float g_sync_mmu_total_mm = 0.0f;

// ============================================================================
// Boot & buffer stabilization — park the buffer at goal before/after printing
// ============================================================================

void sync_init(uint32_t now_ms) {
    buf_state_t raw = buf_state_raw();
    buf_force_stable_state(raw, now_ms);

    g_sync_tension_transitioned = false; /* Never false-trigger tension auto-start on boot! */
    g_sync_state = SYNC_OFF;
    g_sync_auto_started = false;
    g_sync_tail_assist_active = false;
    g_sync_current_sps = 0;
    g_buf_pos_prev = g_buf_pos;
    g_vel_norm = 0.0f;
    g_vel_norm_f = 0.0f;
}

void boot_stabilize_stop(void) {
    if (g_boot_stabilize_lane) {
        motor_stop(&g_boot_stabilize_lane->m);
    }
    g_boot_stabilizing = false;
    g_boot_stabilize_deadline_ms = 0;
    g_boot_stabilize_lane = NULL;
    g_buffer_stabilize_emit_events = false;
    g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
}

void boot_stabilize_disarm(void) {
    g_boot_stabilizing = false;
    g_boot_stabilize_deadline_ms = 0;
    g_boot_stabilize_lane = NULL;
    g_buffer_stabilize_emit_events = false;
    g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
}

bool buffer_stabilize_controller_idle(void) {
    /* TC_ERROR: TC concluded (failed), motors stopped — allow stabilize. */
    /* RELIEF_PAUSE is a sync-recovery state, not idle: block stabilize so
       g_boot_stabilizing cannot black out sync_tick's re-arm path. */
    if ((g_tc_ctx.state != TC_IDLE && g_tc_ctx.state != TC_ERROR) || cutter_busy() ||
        sync_enabled || g_sync_state == SYNC_RELIEF_PAUSE)
        return false;
    if (g_lane_l1.task != TASK_IDLE || g_lane_l2.task != TASK_IDLE)
        return false;
    return true;
}

bool buffer_negative_sync_eligible(void) {
    lane_t *active = lane_ptr(g_active_lane);
    return active && lane_out_present(active);
}

bool buffer_stabilize_start_internal(uint32_t now_ms, bool emit_events,
                                     buffer_service_mode_t mode) {
    if (g_boot_stabilizing) {
        if (emit_events)
            g_buffer_stabilize_emit_events = true;
        return true;
    }
    if (!buffer_stabilize_controller_idle())
        return false;
    if (g_buf_sensor_type != BUF_SENSOR_TYPE_D) {
        /* D23 Gate A: type-P idle stabilize. Drive toward goal via the shared
           goal-relative path below (buf_state_raw() zones pick direction, the
           stabilize tick stops at BUF_NEUTRAL = near goal), but only when
           filament is present on a board-local sensor. Unloaded, the buffer
           rests at the tension/home rail by design, so driving the motor would
           dry-spin against the home stop. */
        lane_t *pl = pick_boot_stabilize_lane();
        if (!(lane_out_present(pl) || lane_in_present(pl))) {
            if (emit_events)
                cmd_event("BUF_STAB", "DONE");
            return true;
        }
    }
    if (mode == BUFFER_SERVICE_NEG_SYNC && sync_guard_active)
        return true;

    buf_state_t buf_state = buf_state_raw();
    lane_t *stab_lane = NULL;
    bool forward = false;

    if (mode == BUFFER_SERVICE_NEG_SYNC) {
        if (buf_state != BUF_COMPRESSION || !buffer_negative_sync_eligible())
            return true;
        stab_lane = lane_ptr(g_active_lane);
        forward = false;
    } else {
        if (buf_state != BUF_COMPRESSION && buf_state != BUF_TENSION) {
            /* Already NEUTRAL: nothing to drive. Type-D snaps the virtual
               position to the zone centre; type-P measures position directly,
               so zeroing its EMA (and, when idle, g_extruder_est_sps — see
               fix-typep-relief-pause-rearm-strand D3) only injects a velocity
               transient into the next PD sample. */
            if (g_buf_sensor_type != BUF_SENSOR_TYPE_P)
                buf_force_stable_state(BUF_NEUTRAL, now_ms);
            if (emit_events)
                cmd_event("BUF_STAB", "DONE");
            return true;
        }
        stab_lane = pick_boot_stabilize_lane();
        forward = (buf_state == BUF_TENSION);
    }

    if (!stab_lane || g_buf_stab_sps <= 0)
        return false;

    g_boot_stabilizing = true;
    g_boot_stabilize_deadline_ms = now_ms + BUFFER_STABILIZE_WINDOW_MS;
    g_boot_stabilize_lane = stab_lane;
    g_buffer_stabilize_emit_events = emit_events;
    g_buffer_service_mode = mode;
    g_idle_compression_since_ms = 0;
    g_boot_stabilize_started_ms = now_ms;
    g_stab_stagnant_since_ms = now_ms;
    g_boot_stabilize_start_pos = g_buf_pos;

    motor_enable(&stab_lane->m, true);
    motor_set_dir(&stab_lane->m, forward);
    motor_set_rate_sps(&stab_lane->m, g_buf_stab_sps);
    g_boot_stabilize_forward = forward;

    if (g_buffer_stabilize_emit_events)
        cmd_event("BUF_STAB", "START");
    return true;
}

bool buffer_stabilize_request(uint32_t now_ms) {
    g_idle_compression_since_ms = 0;
    /* BS (and BL timeout, which routes here) ends the buffer-lock context: drop
       the BL-implied goal override back to the configured BUF_GOAL. */
    g_bl_goal_override = BUF_NEUTRAL;
    return buffer_stabilize_start_internal(now_ms, true, BUFFER_SERVICE_STABILIZE);
}

void buffer_stabilize_cancel(void) {
    boot_stabilize_stop();
}

/* True once a stabilize has settled the buffer at goal. A successful DONE forces
   g_buf_stable_state = BUF_NEUTRAL (predict parks a hair tension-side of goal, by
   design); a STAGNANT abort does not. Lets the boot retry stop on the first real
   success instead of chasing raw position past the predict's safe early stop. */
bool boot_stabilize_settled(void) {
    return !g_boot_stabilizing && g_buf.state == BUF_NEUTRAL;
}

void boot_stabilize_start(uint32_t now_ms) {
    /* emit_events=true: boot-stab shares the BS path; keeping it observable
       (BUF_STAB:START/DONE/STAGNANT) is essential for diagnosing boot behavior. */
    (void)buffer_stabilize_start_internal(now_ms, true, BUFFER_SERVICE_STABILIZE);
}

static bool boot_stabilize_tick_type_p(uint32_t now_ms) {
    if (g_buf_sensor_type != BUF_SENSOR_TYPE_P)
        return false;

    if (g_boot_stabilizing) {
        bool at_rail =
            (g_buf_analog_saturated_since_ms != 0) || (fabsf(g_buf_pos) >= TYPE_P_RAIL_NORM);
        if (at_rail) {
            if ((int32_t)(now_ms - g_boot_stabilize_started_ms) >= g_psf_stab_rail_break_ms) {
                if (g_buffer_stabilize_emit_events)
                    cmd_event("BUF_STAB", "STAGNANT_TIMEOUT");
                boot_stabilize_stop();
                return true;
            }
            g_boot_stabilize_start_pos = g_buf_pos;
            g_stab_stagnant_since_ms = now_ms;
        } else if ((int32_t)(now_ms - g_stab_stagnant_since_ms) >= g_psf_stab_stagnant_ms) {
            float change = fabsf(g_buf_pos - g_boot_stabilize_start_pos);
            if (change < g_psf_stab_stagnant_norm) {
                if (g_buffer_stabilize_emit_events)
                    cmd_event("BUF_STAB", "STAGNANT_TIMEOUT");
                boot_stabilize_stop();
                return true;
            } else {
                g_boot_stabilize_start_pos = g_buf_pos;
                g_stab_stagnant_since_ms = now_ms;
            }
        }
    }

    if (g_boot_stabilize_lane) {
        float goal = psf_goal_norm();
        float predicted = g_buf_pos + SYNC_STAB_PREDICT_LEAD_S * g_vel_norm_f;
        bool reached = g_boot_stabilize_forward ? (predicted >= goal) : (predicted <= goal);
        if (reached) {
            buf_force_stable_state(BUF_NEUTRAL, now_ms);
            if (g_buffer_stabilize_emit_events)
                cmd_event("BUF_STAB", "DONE");
            boot_stabilize_stop();
            return true;
        }
    }
    return false;
}

void buffer_stabilize_tick(uint32_t now_ms) {
    if (sync_guard_active && g_boot_stabilizing &&
        g_buffer_service_mode == BUFFER_SERVICE_NEG_SYNC) {
        boot_stabilize_stop();
        return;
    }

    if (!g_boot_stabilizing) {
        bool can_start_negative_sync = buffer_stabilize_controller_idle() &&
                                       buf_state_raw() == BUF_COMPRESSION &&
                                       buffer_negative_sync_eligible();
        if (can_start_negative_sync) {
            if (g_idle_compression_since_ms == 0)
                g_idle_compression_since_ms = now_ms;
            if (g_post_print_stab_delay_ms <= 0 ||
                (now_ms - g_idle_compression_since_ms) >= (uint32_t)g_post_print_stab_delay_ms) {
                (void)buffer_stabilize_start_internal(now_ms, true, BUFFER_SERVICE_NEG_SYNC);
            }
        } else {
            g_idle_compression_since_ms = 0;
        }
    }

    if (!g_boot_stabilizing)
        return;

    if (!g_boot_stabilize_lane) {
        boot_stabilize_disarm();
        return;
    }

    if (g_boot_stabilize_lane->task != TASK_IDLE) {
        boot_stabilize_disarm();
        return;
    }

    if (!buffer_stabilize_controller_idle()) {
        boot_stabilize_stop();
        return;
    }

    if (boot_stabilize_tick_type_p(now_ms))
        return;

    buf_state_t raw_state = buf_state_raw();

    if (g_buffer_service_mode == BUFFER_SERVICE_NEG_SYNC) {
        if (raw_state == BUF_NEUTRAL) {
            buf_force_stable_state(BUF_NEUTRAL, now_ms);
            if (g_buffer_stabilize_emit_events)
                cmd_event("BUF_STAB", "DONE");
            boot_stabilize_stop();
            return;
        }

        if (raw_state == BUF_TENSION) {
            g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
            g_boot_stabilize_deadline_ms = now_ms + BUFFER_STABILIZE_WINDOW_MS;
            motor_enable(&g_boot_stabilize_lane->m, true);
            motor_set_dir(&g_boot_stabilize_lane->m, true);
            motor_set_rate_sps(&g_boot_stabilize_lane->m, g_buf_stab_sps);
            g_boot_stabilize_forward = true;
            return;
        }
    } else {
        if (raw_state == BUF_NEUTRAL) {
            buf_force_stable_state(BUF_NEUTRAL, now_ms);
            if (g_buffer_stabilize_emit_events)
                cmd_event("BUF_STAB", "DONE");
            boot_stabilize_stop();
            return;
        }

        bool need_forward = (raw_state == BUF_TENSION);
        if (g_boot_stabilize_forward != need_forward) {
            motor_set_dir(&g_boot_stabilize_lane->m, need_forward);
            g_boot_stabilize_forward = need_forward;
            g_boot_stabilize_deadline_ms = now_ms + BUFFER_STABILIZE_WINDOW_MS;
            if (g_buffer_stabilize_emit_events)
                cmd_event("BUF_STAB", "REVERSE");
        }
    }

    if ((int32_t)(now_ms - g_boot_stabilize_deadline_ms) >= 0) {
        if (g_buffer_stabilize_emit_events)
            cmd_event("BUF_STAB", "TIMEOUT");
        boot_stabilize_stop();
    }
}

// ============================================================================
// Flow schedule — map estimated extruder demand to per-segment control params
// ============================================================================

void flow_schedule_refresh_scalar(void) {
    g_flow_sched_len = 1;
    g_flow_sched_runtime[0].flow_sps = g_baseline_target_sps;
    g_flow_sched_runtime[0].baseline_sps = g_baseline_target_sps;
    g_flow_sched_runtime[0].bias_milli = clamp_i(
        (int)(g_sync_compression_bias_frac * (float)FLOW_BIAS_MILLI_SCALE + ROUND_TO_NEAREST_F), 0,
        FLOW_BIAS_MILLI_MAX);
    for (int i = 0; i < CONF_FLOW_SCHED_CAP; i++) {
        g_flow_sched_live_delta[i] = 0;
    }
}

void flow_schedule_reset_runtime(void) {
    g_flow_sched_len = CONF_FLOW_SCHED_LEN;
    if (g_flow_sched_len < 1)
        g_flow_sched_len = 1;
    if (g_flow_sched_len > CONF_FLOW_SCHED_CAP)
        g_flow_sched_len = CONF_FLOW_SCHED_CAP;

    for (int i = 0; i < g_flow_sched_len; i++) {
        g_flow_sched_runtime[i] = G_FLOW_SCHED_CONFIG[i];
    }
    for (int i = 0; i < CONF_FLOW_SCHED_CAP; i++) {
        g_flow_sched_live_delta[i] = 0;
    }

    if (CONF_FLOW_SCHED_LEN <= 1) {
        flow_schedule_refresh_scalar();
    }
}

int lerp_i(int a, int b, int x, int x0, int x1) {
    int span = x1 - x0;
    if (span <= 0)
        return a;
    return a + (int)(((int64_t)(b - a) * (int64_t)(x - x0)) / (int64_t)span);
}

int flow_active_segment(int flow_sps) {
    int len = flow_sched_len_clamped();
    flow_schedule_point_t *sched = g_flow_sched_runtime;
    if (len <= 1 || flow_sps <= sched[0].flow_sps)
        return 0;

    int last = len - 1;
    if (flow_sps >= sched[last].flow_sps)
        return last;

    for (int i = 0; i < last; i++) {
        if (flow_sps <= sched[i + 1].flow_sps)
            return i;
    }
    return last;
}

flow_param_t flow_param(int flow_sps) {
    int len = flow_sched_len_clamped();
    flow_schedule_point_t *sched = g_flow_sched_runtime;
    int segment = flow_active_segment(flow_sps);
    flow_param_t p;

    if (len <= 1 || flow_sps <= sched[0].flow_sps) {
        p = (flow_param_t){sched[0].baseline_sps, sched[0].bias_milli};
        p.baseline_sps += g_flow_sched_live_delta[segment];
        return p;
    }

    int last = len - 1;
    if (flow_sps >= sched[last].flow_sps) {
        p = (flow_param_t){sched[last].baseline_sps, sched[last].bias_milli};
        p.baseline_sps += g_flow_sched_live_delta[segment];
        return p;
    }

    for (int i = 0; i < last; i++) {
        int x0 = sched[i].flow_sps;
        int x1 = sched[i + 1].flow_sps;
        if (flow_sps >= x0 && flow_sps <= x1) {
            p.baseline_sps =
                lerp_i(sched[i].baseline_sps, sched[i + 1].baseline_sps, flow_sps, x0, x1);
            p.bias_milli = lerp_i(sched[i].bias_milli, sched[i + 1].bias_milli, flow_sps, x0, x1);
            p.baseline_sps += g_flow_sched_live_delta[segment];
            return p;
        }
    }

    p = (flow_param_t){sched[last].baseline_sps, sched[last].bias_milli};
    p.baseline_sps += g_flow_sched_live_delta[segment];
    return p;
}

int lane_motion_sps(lane_t *lane) {
    if (!lane)
        return 0;
    if (lane->current_sps > 0)
        return lane->current_sps;
    if (g_tc_ctx.state == TC_RELOAD_FOLLOW && g_tc_ctx.reload_current_sps > 0)
        return g_tc_ctx.reload_current_sps;
    return g_sync_current_sps;
}

lane_t *pick_boot_stabilize_lane(void) {
    lane_t *stab_lane = lane_ptr(g_active_lane);
    if (stab_lane && (lane_out_present(stab_lane) || lane_in_present(stab_lane)))
        return stab_lane;
    bool l1_has = lane_out_present(&g_lane_l1) || lane_in_present(&g_lane_l1);
    bool l2_has = lane_out_present(&g_lane_l2) || lane_in_present(&g_lane_l2);
    if (l1_has && !l2_has)
        return &g_lane_l1;
    if (l2_has && !l1_has)
        return &g_lane_l2;
    return &g_lane_l1;
}

int flow_sched_len_clamped(void) {
    if (g_flow_sched_len < 1)
        return 1;
    if (g_flow_sched_len > CONF_FLOW_SCHED_CAP)
        return CONF_FLOW_SCHED_CAP;
    return g_flow_sched_len;
}

// ============================================================================
// Baseline & neutral-band floors — learned resting feed and anti-starve floors
// ============================================================================

void baseline_update_on_settle(uint32_t neutral_dwell_ms, uint32_t now_ms) {
    if (neutral_dwell_ms <= BASELINE_SETTLE_MIN_DWELL_MS) {
        g_settle_history_count = 0;
        return;
    }

    uint8_t n = CONF_BASELINE_SETTLE_COUNT;
    if (n > SETTLE_HISTORY_MAX_COUNT)
        n = SETTLE_HISTORY_MAX_COUNT;
    if (n < 1)
        n = 1;

    for (int i = SETTLE_HISTORY_LAST_INDEX; i > 0; i--) {
        g_settle_history[i] = g_settle_history[i - 1];
    }
    g_settle_history[0] = g_sync_current_sps;

    if (g_settle_history_count < n) {
        g_settle_history_count++;
    }

    if (g_settle_history_count >= n) {
        int min_sps = g_settle_history[0];
        int max_sps = g_settle_history[0];
        int sum_sps = 0;
        for (int i = 0; i < n; i++) {
            if (g_settle_history[i] < min_sps)
                min_sps = g_settle_history[i];
            if (g_settle_history[i] > max_sps)
                max_sps = g_settle_history[i];
            sum_sps += g_settle_history[i];
        }

        float mean_sps = (float)sum_sps / (float)n;
        float variance_frac =
            (mean_sps > BASELINE_MIN_MEAN_SPS) ? ((float)(max_sps - min_sps) / mean_sps) : 0.0f;

        if (variance_frac <= CONF_BASELINE_VARIANCE_REJECT_FRAC) {
            uint32_t elapsed_ms = now_ms - g_last_baseline_update_ms;
            float elapsed_mm = g_sync_mmu_total_mm - g_last_baseline_update_mm;

            if (g_last_baseline_update_ms == 0 || (elapsed_ms >= CONF_BASELINE_COOLDOWN_MS &&
                                                   elapsed_mm >= CONF_BASELINE_COOLDOWN_MM)) {

                int flow_sps = (int)g_extruder_est_sps;
                int segment = flow_active_segment(flow_sps);
                flow_param_t fp = flow_param(flow_sps);
                int new_baseline =
                    (int)(FLARE_INT_BASELINE_ALPHA * (float)g_sync_current_sps +
                          (1.0f - FLARE_INT_BASELINE_ALPHA) * (float)fp.baseline_sps);
                if (new_baseline > fp.baseline_sps) {
                    g_flow_sched_live_delta[segment] += new_baseline - fp.baseline_sps;
                    g_baseline_sps = new_baseline;
                }

                g_last_baseline_update_ms = now_ms;
                g_last_baseline_update_mm = g_sync_mmu_total_mm;
                g_settle_history_count = 0;
            }
        } else {
            g_settle_history_count = 0;
        }
    }
}

int baseline_control_floor_sps(void) {
    flow_param_t fp = flow_param((int)g_extruder_est_sps);
    return (fp.baseline_sps > g_baseline_target_sps) ? fp.baseline_sps : g_baseline_target_sps;
}

int sync_neutral_anti_tension_floor_sps(buf_state_t s, lane_t *lane, float error_norm,
                                        float deadband_norm, int neutral_target_sps,
                                        uint32_t now_ms) {
    if (g_sync_state != SYNC_ACTIVE || s != BUF_NEUTRAL || !lane)
        return 0;
    if (lane->task != TASK_FEED || lane->fault != FAULT_NONE)
        return 0;
    (void)now_ms;

    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D) {
        if (error_norm >= -deadband_norm)
            return 0;

        int demand_floor_sps = (int)(g_extruder_est_sps * TYPE_D_NEUTRAL_DEMAND_MARGIN);
        if (demand_floor_sps < g_sync_min_sps)
            demand_floor_sps = g_sync_min_sps;
        if (neutral_target_sps > 0 && demand_floor_sps > neutral_target_sps) {
            demand_floor_sps = neutral_target_sps;
        }

        int baseline_floor_sps = baseline_control_floor_sps();
        int assist_floor_sps =
            (int)((float)baseline_floor_sps * SYNC_NEUTRAL_ANTI_TENSION_FLOOR_FRAC);
        if (assist_floor_sps > demand_floor_sps)
            assist_floor_sps = demand_floor_sps;
        if (assist_floor_sps <= g_sync_min_sps)
            return 0;
        return assist_floor_sps;
    }

    if (error_norm > deadband_norm)
        return 0;

    /* Type-P legacy path: unconditional refill floor. The floor is baseline-
     * derived so it can never itself drive the buffer toward TENSION. */
    int baseline_floor_sps = baseline_control_floor_sps();
    int assist_floor_sps = (int)((float)baseline_floor_sps * SYNC_NEUTRAL_ANTI_TENSION_FLOOR_FRAC);
    if (assist_floor_sps <= g_sync_min_sps)
        return 0;

    return assist_floor_sps;
}

// ============================================================================
// Sync state & retract-assist control — enable/disable, mode, assist gating
// ============================================================================

int sync_clamp_max_sps(int requested_sps) {
    return motion_clamp_rate_sps(requested_sps);
}

void sync_set_state(sync_state_t new_state) {
    if (g_sync_state == new_state)
        return;
    if (new_state == SYNC_OFF) {
        for (int i = 0; i < CONF_FLOW_SCHED_CAP; i++) {
            g_flow_sched_live_delta[i] = 0;
        }
    }
    /* Seed the type-P smoothing filter to the current feed on (re)entering active
       sync so it doesn't slew from a stale value left by a prior session. */
    if (new_state == SYNC_ACTIVE)
        g_psf_target_filt = (float)g_sync_current_sps;
    g_sync_state = new_state;
    g_sync_tension_transitioned = false;
    g_sync_refill_effort_mm = 0.0f;
    g_sync_relieve_effort_mm = 0.0f;
    g_sync_cannot_refill_warned = false;
    g_sync_cannot_relieve_warned = false;
}

void sync_retract_assist_set(bool enabled) {
    lane_t *lane = lane_ptr(g_active_lane);
    if (enabled) {
        g_sync_current_sps = 0;
        g_sync_auto_started = false;
        g_sync_tail_assist_active = false;
        g_sync_idle_since_ms = 0;
        g_bl_autostart_suppressed = false;
        sync_set_state(SYNC_RETRACT_ASSIST);
        if (lane && lane->task == TASK_FEED)
            lane_stop(lane);
    } else {
        if (g_sync_state == SYNC_RETRACT_ASSIST) {
            /* Stop any BL-driven motor motion before transitioning to SYNC_OFF */
            if (g_bl_sub_state != BL_IDLE && lane) {
                motor_set_rate_sps(&lane->m, 0);
                motor_enable(&lane->m, false);
            }
            g_bl_sub_state = BL_IDLE;
            g_bl_prime_start_ms = 0;
            g_bl_prime_mm_per_s = 0.0f;
            g_bl_prime_cap_mm = 0.0f;
            g_bl_prime_cur_sps = 0;
            g_bl_prime_target_sps = 0;
            g_bl_prime_ramp_tick_ms = 0;
            g_bl_prime_traveled_mm = 0.0f;
            g_bl_follow_mm = 0.0f;
            g_bl_follow_rate_mmpm = 0.0f;
            g_bl_follow_start_ms = 0;
            g_bl_follow_mm_per_s = 0.0f;
            g_bl_follow_seed_sps = 0;
            g_bl_watchdog_ms = 0;
            g_bl_arm_timeout_ms = 0;
            g_bl_lock_engaged = false;
            sync_set_state(SYNC_OFF);
            /* Suppress auto-start: buffer is at the BL extreme, not extruder-driven. */
            g_bl_autostart_suppressed = true;
        }
    }
}

void sync_retract_assist_release(uint32_t now_ms) {
    bool was_active = (g_sync_state == SYNC_RETRACT_ASSIST);
    sync_retract_assist_set(false);
    if (was_active) {
        (void)buffer_stabilize_start_internal(now_ms, true, BUFFER_SERVICE_NEG_SYNC);
    }
}

void handle_bl_watchdog_timeout(uint32_t now_ms) {
    cmd_event_critical("BL", "TIMEOUT");
    sync_retract_assist_release(now_ms);
    sync_bl_clear_autostart_suppress();
    (void)buffer_stabilize_request(now_ms);
}

void sync_bl_clear_autostart_suppress(void) {
    g_bl_autostart_suppressed = false;
}

bool sync_retract_assist_enabled(void) {
    return g_sync_state == SYNC_RETRACT_ASSIST;
}

// ============================================================================
// Buffer lock service (BL:) — drive the buffer to a rail then follow on
// ============================================================================

void sync_buffer_lock_arm(buf_state_t target, float follow_mm, float follow_rate_mmpm,
                          uint32_t now_ms, uint32_t timeout_ms) {
    lane_t *lane = lane_ptr(g_active_lane);
    if (!lane)
        return;

    /* Stop any in-progress BL motor drive before re-arming */
    if (g_bl_sub_state != BL_IDLE) {
        motor_set_rate_sps(&lane->m, 0);
        motor_enable(&lane->m, false);
    }

    /* Enter (or stay in) SYNC_RETRACT_ASSIST */
    g_sync_current_sps = 0;
    g_sync_auto_started = false;
    g_sync_tail_assist_active = false;
    g_sync_idle_since_ms = 0;
    sync_set_state(SYNC_RETRACT_ASSIST);
    if (lane->task == TASK_FEED)
        lane_stop(lane);

    g_bl_target_state = target;
    /* Park the buffer goal at the armed rail until BS/timeout clears it. */
    g_bl_goal_override = target;

    g_bl_arm_timeout_ms = (timeout_ms > 0) ? timeout_ms : BL_WATCHDOG_DEFAULT_MS;

    /* Prime runs at SYNC_MAX_SPS for both Type-D and Type-P. Type-P EMA filter
     * lag is compensated predictively via BL_PRIME_PREDICT_LEAD_S. Overtravel
     * is controlled by the two-phase gates:
     *   phase 1 — search until switch/predictive threshold fires (outer cap: BUF_MAX_TRAVEL_MM)
     *   phase 2 — lock at switch/predictive threshold (no post-settle travel). */
    int idx = lane->lane_id - 1;
    int prime_sps = sync_clamp_max_sps(g_sync_max_sps);
    int start_sps = g_ramp_step_sps;
    if (start_sps > prime_sps)
        start_sps = prime_sps;
    if (start_sps < 1)
        start_sps = 1;

    g_bl_prime_start_ms = now_ms;
    g_bl_prime_cur_sps = start_sps;
    g_bl_prime_target_sps = prime_sps;
    g_bl_prime_ramp_tick_ms = now_ms;
    g_bl_prime_mm_per_s = (float)start_sps * g_mm_per_step[idx];
    /* Bounded Prime Travel requirement: full travel cap */
    g_bl_prime_cap_mm =
        (g_buf_max_travel_mm > 0) ? (float)g_buf_max_travel_mm : BL_FALLBACK_TRAVEL_CAP_MM;
    g_bl_prime_traveled_mm = 0.0f;
    /* Subtract BUF_MAX_TRAVEL_MM/2 so explicit FOLLOW finishes near NEUTRAL rather
     * than at the switch click. After the extruder stops the MMU keeps
     * draining; parking at the switch click leaves one step before the
     * mechanical hard end. If the adjusted distance is <= 0, the macro
     * doesn't need a follow-on for such a short move — use passive lock.
     * Bare BL:T / BL:C (no follow parameters) is a passive lock: no break
     * detection, no catch. buffer-state-lock D5 removed the reactive catch
     * (motor commanded against still-taut filament = stall risk); the catch
     * arms only when the host states how far and how fast to follow. */
    {
        float half_travel = (g_buf_max_travel_mm > 0) ? ((float)g_buf_max_travel_mm * HALF_F)
                                                      : BL_FALLBACK_HALF_TRAVEL_MM;
        g_bl_follow_mm = 0.0f;
        g_bl_follow_rate_mmpm = 0.0f;
        if (follow_mm > 0.0f && follow_rate_mmpm > 0.0f) {
            float effective_follow_mm = follow_mm - half_travel;
            g_bl_follow_mm = (effective_follow_mm > 0.0f) ? effective_follow_mm : 0.0f;
            g_bl_follow_rate_mmpm = (g_bl_follow_mm > 0.0f) ? follow_rate_mmpm : 0.0f;
        }
    }
    g_bl_follow_seed_sps = 0;
    g_bl_follow_start_ms = 0;
    g_bl_follow_mm_per_s = 0.0f;
    g_bl_follow_traveled_mm = 0.0f;
    g_bl_last_tick_ms = now_ms;

    g_bl_watchdog_ms = 0;
    g_bl_lock_engaged = false;
    g_bl_lock_extreme = 0.0f;
    g_bl_sub_state = BL_PRIME;

    /* BL:T → retract (forward=false) to pull buffer toward tension extreme.
     * BL:C → extrude (forward=true) to push buffer toward compression extreme. */
    bool forward = (target == BUF_COMPRESSION);
    motor_enable(&lane->m, true);
    motor_set_dir(&lane->m, forward);
    motor_set_rate_sps(&lane->m, start_sps);

    cmd_event("BL", "PRIME");
}

const char *sync_buffer_lock_arm_str(void) {
    if (g_sync_state != SYNC_RETRACT_ASSIST || g_bl_sub_state == BL_IDLE)
        return "0";
    return (g_bl_target_state == BUF_TENSION) ? "T" : "C";
}

bool sync_buffer_lock_motor_moving(void) {
    return g_sync_state == SYNC_RETRACT_ASSIST &&
           (g_bl_sub_state == BL_PRIME || g_bl_sub_state == BL_FOLLOW);
}

static void sync_buffer_lock_prime(lane_t *lane, uint32_t now_ms) {
    bool reached = false;
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
        float predicted = g_buf_pos + BL_PRIME_PREDICT_LEAD_S * g_vel_norm_f;
        if (g_bl_target_state == BUF_TENSION)
            reached = (predicted <= -PSF_FOLLOW_RAIL_NORM);
        else if (g_bl_target_state == BUF_COMPRESSION)
            reached = (predicted >= PSF_FOLLOW_RAIL_NORM);
    } else {
        buf_state_t raw = buf_state_raw();
        reached = (raw == g_bl_target_state);
    }

    int idx = lane->lane_id - 1;
    float dt_s = (float)(now_ms - g_bl_last_tick_ms) / MS_PER_SECOND_F;
    if (dt_s < BL_FOLLOW_DT_MIN_S)
        dt_s = BL_FOLLOW_DT_MIN_S;
    if (dt_s > BL_FOLLOW_DT_MAX_S)
        dt_s = BL_FOLLOW_DT_MAX_S;
    g_bl_last_tick_ms = now_ms;

    /* Accelerate toward the target prime rate (pull-in-safe ramp). */
    if (g_bl_prime_cur_sps < g_bl_prime_target_sps &&
        (int32_t)(now_ms - g_bl_prime_ramp_tick_ms) >= g_ramp_tick_ms) {
        g_bl_prime_ramp_tick_ms = now_ms;
        g_bl_prime_cur_sps += g_ramp_step_sps;
        if (g_bl_prime_cur_sps > g_bl_prime_target_sps)
            g_bl_prime_cur_sps = g_bl_prime_target_sps;
        motor_set_rate_sps(&lane->m, g_bl_prime_cur_sps);
        g_bl_prime_mm_per_s = (float)g_bl_prime_cur_sps * g_mm_per_step[idx];
    }

    /* Integrate distance at the current (ramping) rate */
    g_bl_prime_traveled_mm += g_bl_prime_mm_per_s * dt_s;

    bool deadline_hit = (g_bl_prime_traveled_mm >= g_bl_prime_cap_mm);

    if (reached || deadline_hit) {
        /* Prime done — stop motor but keep enabled for holding torque */
        motor_set_rate_sps(&lane->m, 0);
        /* motor_enable stays true: locked hold needs energized stepper */

        if (deadline_hit) {
            cmd_event("BL", "PRIME_BOUND");
        }

        if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
            /* Type-P: the prime has driven the buffer to the armed rail by
               construction (predicted rail crossing, or the full-travel cap
               against the hard end). The analog reading at that hard end is
               whatever this rig's calibration says it is — on a rig whose
               tension end reads shallower than PSF_BREAK_THRESHOLD_NORM an
               absolute "pos <= -0.75" engage test never passes, break
               detection stays disabled, the MMU never follows a retract
               longer than the buffer and the extruder skips against the held
               MMU (tip never parks -> TC unload UNLOAD_TIMEOUT). Engage
               unconditionally and detect the break relative to the deepest
               reading actually observed at the rail (g_bl_lock_extreme). */
            g_bl_lock_engaged = true;
            g_bl_lock_extreme = g_buf_pos;
        } else {
            buf_state_t raw = buf_state_raw();
            g_bl_lock_engaged = (raw == g_bl_target_state);
        }

        g_bl_sub_state = BL_LOCKED;
        g_bl_watchdog_ms =
            now_ms + (g_bl_arm_timeout_ms ? g_bl_arm_timeout_ms : BL_WATCHDOG_DEFAULT_MS);
        cmd_event("BL", "LOCKED");
    }
}

static void sync_buffer_lock_locked(lane_t *lane, uint32_t now_ms) {
    if (g_bl_follow_mm > 0.0f) {
        bool lock_broken = false;
        if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
            /* Track the deepest reading at the rail (the EMA keeps settling
               after the prime motor stops), then break once the buffer has
               moved BL_BREAK_DELTA_NORM back toward neutral from it. With a
               perfectly calibrated rail (extreme = -1.0) this is the same
               -0.75 boundary as before; with a shallow-reading rail it still
               fires after the same physical travel. */
            if (g_bl_target_state == BUF_TENSION) {
                if (g_buf_pos < g_bl_lock_extreme)
                    g_bl_lock_extreme = g_buf_pos;
                lock_broken = (g_buf_pos > g_bl_lock_extreme + BL_BREAK_DELTA_NORM);
            } else if (g_bl_target_state == BUF_COMPRESSION) {
                if (g_buf_pos > g_bl_lock_extreme)
                    g_bl_lock_extreme = g_buf_pos;
                lock_broken = (g_buf_pos < g_bl_lock_extreme - BL_BREAK_DELTA_NORM);
            }
        } else {
            buf_state_t raw = buf_state_raw();
            lock_broken = (raw != g_bl_target_state);
        }

        if (lock_broken) {
            cmd_event("BL", "BREAK");

            int idx = lane->lane_id - 1;
            int seed_sps;
            if (g_bl_follow_rate_mmpm > 0.0f) {
                seed_sps = (int)(g_bl_follow_rate_mmpm / SECONDS_PER_MINUTE_F / g_mm_per_step[idx] +
                                 ROUND_TO_NEAREST_F);
            } else {
                seed_sps = sync_clamp_max_sps(g_sync_max_sps);
            }
            if (seed_sps < 1)
                seed_sps = 1;
            seed_sps = sync_clamp_max_sps(seed_sps);
            g_bl_follow_seed_sps = seed_sps;

            int follow_sps = seed_sps;
            if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
                float armed_rail_norm = (g_bl_target_state == BUF_TENSION) ? -1.0f : 1.0f;
                float err = fabsf(g_buf_pos - armed_rail_norm);
                float err_ratio = clamp_f(err / BL_CATCH_ERR_SPAN_NORM, 0.0f, 1.0f);
                int max_target = motion_clamp_rate_sps(g_global_max_sps);
                if (max_target > seed_sps) {
                    follow_sps = seed_sps + (int)((float)(max_target - seed_sps) * err_ratio +
                                                  ROUND_TO_NEAREST_F);
                }
            }

            bool forward = (g_bl_target_state == BUF_COMPRESSION);
            motor_set_dir(&lane->m, forward);
            int start_sps = g_ramp_step_sps;
            if (start_sps > follow_sps)
                start_sps = follow_sps;
            if (start_sps < 1)
                start_sps = 1;
            motor_set_rate_sps(&lane->m, start_sps);

            g_bl_follow_start_ms = now_ms;
            g_bl_follow_cur_sps = start_sps;
            g_bl_follow_target_sps = follow_sps;
            g_bl_follow_ramp_tick_ms = now_ms;
            g_bl_follow_mm_per_s = (float)start_sps * g_mm_per_step[idx];
            g_bl_follow_traveled_mm = 0.0f;
            g_bl_last_tick_ms = now_ms;
            g_bl_sub_state = BL_FOLLOW;
            cmd_event("BL", "FOLLOW");
            return;
        }
    }
    if (g_bl_watchdog_ms != 0 && (int32_t)(now_ms - g_bl_watchdog_ms) >= 0) {
        handle_bl_watchdog_timeout(now_ms);
    }
}

static void sync_buffer_lock_follow(lane_t *lane, uint32_t now_ms) {
    int idx = lane->lane_id - 1;
    float dt_s = (float)(now_ms - g_bl_last_tick_ms) / MS_PER_SECOND_F;
    if (dt_s < BL_FOLLOW_DT_MIN_S)
        dt_s = BL_FOLLOW_DT_MIN_S;
    if (dt_s > BL_FOLLOW_DT_MAX_S)
        dt_s = BL_FOLLOW_DT_MAX_S;

    if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
        /* Gate on the absolute rail or on the reading this rig actually
           produced at the hard end during the prime — a shallow-reading rail
           would otherwise let the open-loop follow drive into the hard stop. */
        bool rail_hit = (g_bl_target_state == BUF_TENSION)
                            ? (g_buf_pos <= -PSF_FOLLOW_RAIL_NORM ||
                               g_buf_pos <= g_bl_lock_extreme + BL_FOLLOW_GATE_MARGIN_NORM)
                            : (g_buf_pos >= PSF_FOLLOW_RAIL_NORM ||
                               g_buf_pos >= g_bl_lock_extreme - BL_FOLLOW_GATE_MARGIN_NORM);
        if (rail_hit) {
            motor_set_rate_sps(&lane->m, 0);
            g_bl_sub_state = BL_LOCKED;
            cmd_event("BL", "FOLLOW_GATED");
            g_bl_last_tick_ms = now_ms;
            return;
        }

        /* Type-P error-proportional rate escalation toward GLOBAL_MAX_SPS */
        float armed_rail_norm = (g_bl_target_state == BUF_TENSION) ? -1.0f : 1.0f;
        float err = fabsf(g_buf_pos - armed_rail_norm);
        float err_ratio = clamp_f(err / BL_CATCH_ERR_SPAN_NORM, 0.0f, 1.0f);
        int max_target = motion_clamp_rate_sps(g_global_max_sps);
        if (max_target > g_bl_follow_seed_sps) {
            g_bl_follow_target_sps =
                g_bl_follow_seed_sps +
                (int)((float)(max_target - g_bl_follow_seed_sps) * err_ratio + ROUND_TO_NEAREST_F);
        } else {
            g_bl_follow_target_sps = g_bl_follow_seed_sps;
        }
    }

    /* Accelerate or decelerate toward target follow rate (pull-in-safe ramp). */
    if ((int32_t)(now_ms - g_bl_follow_ramp_tick_ms) >= g_ramp_tick_ms) {
        if (g_bl_follow_cur_sps < g_bl_follow_target_sps) {
            g_bl_follow_ramp_tick_ms = now_ms;
            g_bl_follow_cur_sps += g_ramp_step_sps;
            if (g_bl_follow_cur_sps > g_bl_follow_target_sps)
                g_bl_follow_cur_sps = g_bl_follow_target_sps;
            motor_set_rate_sps(&lane->m, g_bl_follow_cur_sps);
            g_bl_follow_mm_per_s = (float)g_bl_follow_cur_sps * g_mm_per_step[idx];
        } else if (g_bl_follow_cur_sps > g_bl_follow_target_sps) {
            g_bl_follow_ramp_tick_ms = now_ms;
            g_bl_follow_cur_sps -= g_ramp_step_sps;
            if (g_bl_follow_cur_sps < g_bl_follow_target_sps)
                g_bl_follow_cur_sps = g_bl_follow_target_sps;
            motor_set_rate_sps(&lane->m, g_bl_follow_cur_sps);
            g_bl_follow_mm_per_s = (float)g_bl_follow_cur_sps * g_mm_per_step[idx];
        }
    }

    /* Integrate distance at the current (ramping) rate, not a fixed one. */
    g_bl_follow_traveled_mm += g_bl_follow_mm_per_s * dt_s;
    float traveled = g_bl_follow_traveled_mm;

    if (traveled >= g_bl_follow_mm) {
        motor_set_rate_sps(&lane->m, 0);
        g_bl_follow_mm = 0.0f;
        g_bl_follow_rate_mmpm = 0.0f;
        g_bl_follow_seed_sps = 0;
        g_bl_follow_start_ms = 0;
        g_bl_follow_mm_per_s = 0.0f;
        g_bl_follow_traveled_mm = 0.0f;
        g_bl_sub_state = BL_LOCKED;
        cmd_event("BL", "FOLLOW_DONE");
    } else if (g_bl_watchdog_ms != 0 && (int32_t)(now_ms - g_bl_watchdog_ms) >= 0) {
        handle_bl_watchdog_timeout(now_ms);
    }
}

void sync_buffer_lock_tick(lane_t *lane, uint32_t now_ms) {
    if (!lane)
        return;

    if (lane->fault != FAULT_NONE) {
        if (g_bl_sub_state != BL_IDLE) {
            motor_stop(&lane->m);
            lane->current_sps = 0;
            lane->target_sps = 0;
            g_bl_sub_state = BL_IDLE;
        }
        return;
    }

    if (g_bl_sub_state == BL_PRIME) {
        sync_buffer_lock_prime(lane, now_ms);
    } else if (g_bl_sub_state == BL_LOCKED) {
        sync_buffer_lock_locked(lane, now_ms);
    } else if (g_bl_sub_state == BL_FOLLOW) {
        sync_buffer_lock_follow(lane, now_ms);
    }
    g_bl_last_tick_ms = now_ms;
}

// ============================================================================
// Sync enable / disable / fault — lifecycle and bootstrap of the controller
// ============================================================================

void sync_relief_pause(void) {
    sync_set_state(SYNC_RELIEF_PAUSE);
    g_sync_current_sps = 0;
}

static uint32_t g_sync_fault_hold_entry_ms = 0;

void sync_fault_hold(void) {
    sync_set_state(SYNC_FAULT_HOLD);
    g_sync_current_sps = 0;
    g_sync_fault_hold_entry_ms = g_now_ms;
}

void sync_disable(bool reset_estimator) {
    sync_set_state(SYNC_OFF);
    g_sync_auto_started = false;
    g_sync_tail_assist_active = false;
    g_sync_current_sps = 0;
    g_sync_idle_since_ms = 0;
    g_sync_fast_brake_until_ms = 0;
    g_sync_compression_recovery_active = false;
    g_sync_continuous_compression_since_ms = 0;
    g_sync_post_compression_boost_until_ms = 0;
    g_sync_recent_negative_until_ms = 0;
    g_sync_tension_pin_since_ms = 0;
    g_buf_confidence = 1.0f;
    g_sync_reserve_integral_mm = 0.0f;
    g_buf_pos_sigma_accum_mm = 0.0f;
    g_buf_sigma_mm = 0.0f;
    g_buf_est_low_cf_emit_ms = 0;
    g_buf_tension_dwell_warn_emit_ms = 0;
    g_buf_est_fallback_emitted = false;
    if (g_bp_drift_samples > 0)
        cmd_event("BUF", "DRIFT_RESET");
    g_bp_residual_last_mm = 0.0f;
    g_bp_drift_ewma_mm = 0.0f;
    g_bp_drift_samples = 0;
    g_bp_drift_last_ms = 0;
    g_bp_drift_correction_applied_mm = 0.0f;
    memset(g_tension_pin_ts, 0, sizeof(g_tension_pin_ts));
    g_tension_pin_ts_idx = 0;
    g_tension_risk_emit_ms = 0;
    g_tension_floor_sps = 0.0f;
    g_relay_flip_travel_since_mm = 0.0f;
    g_relay_neutral_trim_sps = 0.0f;
    g_relay_trim_last_leak_ms = 0;
    g_sync_tension_extreme = 0.0f;
    g_sync_tension_extreme_valid = false;
    g_sync_relief_active = false;
    g_sync_trip_armed = false;
    g_sync_probe_state = SYNC_PROBE_NONE;
    g_sync_probe_decided = false;
    g_sync_probe_peak_pos = g_buf_pos;
    type_d_neutral_feed_reset();

    if (reset_estimator) {
        g_extruder_est_sps = 0.0f;
        g_extruder_est_prev_sps = 0.0f;
        g_extruder_est_last_update_ms = g_now_ms;
    }
}

void sync_rearm_active(lane_t *lane, uint32_t now_ms) {
    bool was_tension = (g_buf.state == BUF_TENSION);
    buf_force_stable_state(BUF_NEUTRAL, now_ms);
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D) {
        g_buf_pos = buf_target_reserve_mm();
    }
    g_sync_current_sps = sync_bootstrap_sps();
    g_sync_tension_pin_since_ms = was_tension ? now_ms : 0;
    g_sync_tension_extreme = 0.0f;
    g_sync_tension_extreme_valid = false;
    g_sync_relief_active = false;
    g_sync_trip_armed = false;
    g_sync_probe_state = SYNC_PROBE_NONE;
    g_sync_probe_decided = false;
    g_sync_probe_peak_pos = g_buf_pos;
    sync_set_state(SYNC_ACTIVE);
    g_sync_auto_started = true;
    g_sync_tail_assist_active = lane && !lane_in_present(lane) && lane_out_present(lane);
    g_sync_idle_since_ms = 0;
    cmd_event("SYNC", "AUTO_START");
}

int sync_bootstrap_sps(void) {
    int max_sps = sync_clamp_max_sps(g_sync_max_sps);
    int startup_floor_sps = g_compression_sps + g_pre_ramp_sps;
    int baseline_floor_sps = baseline_control_floor_sps();

    /* Adaptive floor: if we have a learned baseline, don't start way below it. */
    if (baseline_floor_sps > (startup_floor_sps * 2)) {
        int adaptive_floor = baseline_floor_sps / 2;
        if (adaptive_floor > startup_floor_sps)
            startup_floor_sps = adaptive_floor;
    }
    if (startup_floor_sps < g_buf_stab_sps)
        startup_floor_sps = g_buf_stab_sps;

    bool est_fresh = g_extruder_est_last_update_ms != 0 &&
                     (g_now_ms - g_extruder_est_last_update_ms) < SYNC_EST_FRESH_MS &&
                     g_extruder_est_sps >= (float)startup_floor_sps;

    int res;
    if (est_fresh) {
        res = clamp_i((int)g_extruder_est_sps, startup_floor_sps, max_sps);
    } else {
        res = clamp_i(startup_floor_sps, g_compression_sps, max_sps);
    }

    /* F2b: a post-recovery start ramps from the learned baseline, not a
     * collapsed/high estimator, so bootstrap cannot slam TENSION. The
     * startup_floor lower bound is still honored. */
    if (baseline_floor_sps >= startup_floor_sps && res > baseline_floor_sps)
        res = baseline_floor_sps;

    if (!est_fresh) {
        g_extruder_est_sps = (float)res;
        g_extruder_est_prev_sps = (float)res;
    }

    g_extruder_est_last_update_ms = g_now_ms;
    g_sync_fast_brake_until_ms = 0;
    g_sync_continuous_compression_since_ms = 0;
    return res;
}

// ============================================================================
// Sync controller — per-tick target calculation, gating, and rate application
// ============================================================================

int sync_effective_kp_sps(buf_state_t s) {
    int baseline_ref_sps = baseline_control_floor_sps();
    int baseline_limited_kp = (s == BUF_TENSION) ? (baseline_ref_sps * 2) : (baseline_ref_sps / 3);
    if (baseline_limited_kp < g_compression_sps)
        baseline_limited_kp = g_compression_sps;
    return (g_sync_kp_sps < baseline_limited_kp) ? g_sync_kp_sps : baseline_limited_kp;
}

void sync_apply_to_active(void) {
    lane_t *lane = lane_ptr(g_active_lane);
    if (!lane) {
        g_sync_current_sps = 0;
        return;
    }
    if (lane->task == TASK_MOVE)
        return;

    if (lane->fault != FAULT_NONE) {
        if (lane->task == TASK_FEED)
            lane_stop(lane);
        return;
    }

    bool is_protected_task = (lane->task == TASK_UNLOAD || lane->task == TASK_AUTOLOAD);

    if (g_sync_current_sps > 0) {
        if (is_protected_task) {
            lane->current_sps = g_sync_current_sps;
            lane->target_sps = g_sync_current_sps;
            motor_set_rate_sps(&lane->m, g_sync_current_sps);
            motor_enable(&lane->m, true);
        } else if (lane->task != TASK_FEED && lane->fault == FAULT_NONE) {
            lane_start(lane, TASK_FEED, g_sync_current_sps, true, g_now_ms, 0);
        } else {
            lane->current_sps = g_sync_current_sps;
            lane->target_sps = g_sync_current_sps;
            motor_set_rate_sps(&lane->m, g_sync_current_sps);
            motor_enable(&lane->m, true);
            motor_set_dir(&lane->m, true);
        }
    } else if (lane->task == TASK_FEED) {
        lane_stop(lane);
    }
}

void sync_on_transition(buf_state_t prev, buf_state_t now_state, uint32_t now_ms) {
    /* D-10/D-12: this is the only place a buffer-state edge is observed, so
       it is the natural place to arm the mm/ms starvation trip -- any
       TENSION/NEUTRAL/COMPRESSION edge counts, deliberately with no near-
       neutral position clause (D-10: a |pos| window would be another
       absolute-literal compare, the exact 51bdca8 failure class). Gated on
       sync_enabled (SYNC_ACTIVE) rather than g_sync_auto_started: the latter
       tracks only the organic-engage/rearm entry points and stays false for
       a directly-forced active session (a host SET:, a manual toolhead-
       insert engage with auto_mode off, or the sim's start_sync_active
       shortcut) even though sync is genuinely driving the buffer and
       observing real transitions in exactly the same way -- gating on it
       would suppress the pre-existing ms dwell trip in every one of those
       sessions, not just the false-positive case D-10 targets (empirically
       confirmed: it silently changed the settled equilibrium of an existing
       13-01 sim scenario by preventing a legitimate fault-hold/recovery
       cycle from ever occurring). "Armed only after a transition has been
       observed since sync started actively driving" is the D-10 intent;
       sync_enabled is the correct predicate for "actively driving", not
       g_sync_auto_started. Cleared back to false in sync_disable()/
       sync_rearm_active()/the AUTO_START branch of sync_tick_auto_start_stop()
       (mirroring g_sync_tension_extreme's reset sites) and on a hold's
       falling edge (REVIEW-04, below). */
    if (sync_enabled) {
        g_sync_trip_armed = true;
    }

    if (prev == BUF_TENSION && now_state == BUF_COMPRESSION) {
        g_sync_fast_brake_until_ms = now_ms + SYNC_FAST_BRAKE_MS;
    }

    if (now_state == BUF_TENSION) {
        g_sync_tension_pin_since_ms = now_ms;
        lane_t *lane = lane_ptr(g_active_lane);
        if (lane && (lane->task == TASK_IDLE || lane->task == TASK_FEED)) {
            g_sync_tension_transitioned = true;
        }
        g_tension_pin_ts[g_tension_pin_ts_idx] = now_ms;
        g_tension_pin_ts_idx = (g_tension_pin_ts_idx + 1) % TENSION_PIN_WINDOW_LEN;
    } else if (prev == BUF_TENSION) {
        g_sync_tension_pin_since_ms = 0;
        /* Buffer physically departed tension — safe to allow auto-start again. */
        g_bl_autostart_suppressed = false;
    }

    if (!g_sync_tail_assist_active) {
        if (now_state == BUF_COMPRESSION) {
            g_neutral_creep_sps = 0;
            g_sync_compression_recovery_active = true;
            g_sync_continuous_compression_since_ms = 0;
            g_sync_post_compression_boost_until_ms = 0;
        } else if (prev == BUF_COMPRESSION && now_state == BUF_NEUTRAL) {
            if (g_sync_compression_recovery_active)
                g_sync_post_compression_boost_until_ms = now_ms + SYNC_POST_COMPRESSION_BOOST_MS;
            g_sync_compression_recovery_active = false;
            g_sync_continuous_compression_since_ms = 0;
        } else if (now_state == BUF_TENSION) {
            g_sync_compression_recovery_active = false;
            g_sync_continuous_compression_since_ms = 0;
            g_sync_post_compression_boost_until_ms = 0;
        }
    }

    bool fast_brake_active =
        g_sync_fast_brake_until_ms != 0 && (int32_t)(g_sync_fast_brake_until_ms - now_ms) > 0;
    if (prev != BUF_NEUTRAL && now_state == BUF_NEUTRAL && sync_enabled && !fast_brake_active &&
        !g_sync_compression_recovery_active && !sync_guard_active) {
        baseline_update_on_settle(g_buf.dwell_ms, now_ms);
    }
}

/* Phase 13 Plan 02 (D-26/D-27): true while a deliberate rail hold is in
   progress -- a BL lock/prime/follow cycle, tail-assist feed, an active
   buffer-stabilize drive, or a RELOAD follow -- any of which can legitimately
   pin the buffer in BUF_TENSION (or keep commanding feed while it's there)
   for longer than the mm/ms trip thresholds without that being starvation.
   While true, the trip must not evaluate at all (D-26); does NOT reset on a
   paused/idle extruder by itself -- that just stops the accumulator from
   growing further, which is the specified behaviour (D-27, no reset). */
static bool sync_type_p_hold_in_progress(void) {
    return (g_bl_sub_state != BL_IDLE) || g_sync_tail_assist_active || g_boot_stabilizing ||
           (tc_state() == TC_RELOAD_FOLLOW);
}

/* REVIEW-04: the hold predicate above must be sampled unconditionally, every
   main-loop pass, from a position that runs before sync_tick()'s early
   returns can swallow it -- see the call site in sync_tick() for why. On the
   falling edge (hold WAS in progress, now is not) the trip's accumulator and
   arm flag are zeroed/cleared so the loop must observe a fresh buffer
   transition and re-accumulate the full threshold before it can trip again;
   without this, a hold that releases while the lane is bypassed, mid-
   toolchange, gated, or simply not in BUF_TENSION would leave the
   accumulator carrying the millimetres fed during the hold, tripping a
   healthy print shortly after the hold ends. The reset body is guarded on
   BUF_SENSOR_TYPE_P (type-D accounting is untouched); the previous-value
   store is NOT guarded, so a sensor-type change mid-session cannot leave a
   stale `true` latched. */
static void sync_trip_track_hold_edge(void) {
    bool hold_now = sync_type_p_hold_in_progress();
    if (g_sync_prev_hold_active && !hold_now) {
        if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
            g_sync_refill_effort_mm = 0.0f;
            g_sync_trip_armed = false;
            /* Phase 13 Plan 03: the probe shares the mm trip's arm/accumulator
               reset sites (D-16's ordering is only structural if the two
               never drift apart) -- a hold releasing mid-episode must leave
               the probe undecided too, re-seeded to the CURRENT reading so a
               stale peak from before the hold cannot manufacture a false
               CONSUMER in the next episode. */
            g_sync_probe_state = SYNC_PROBE_NONE;
            g_sync_probe_decided = false;
            g_sync_probe_peak_pos = g_buf_pos;
        }
    }
    g_sync_prev_hold_active = hold_now;
}

/* psf-runout-escalation-race-fix: a genuine runout (lane present, RELOAD
   enabled, feeding, toolchange idle, both lane sensors clear) hands off to
   the same RUNOUT/RELOAD escalation motion.c uses instead of looping
   FAULT_HOLD/AUTO_START forever (see sync_check_tension_dwell_and_ramp's
   original comment). Shared by both type-P fault-hold entry paths — the
   fast (~1s) analog-rail-saturation path in sync_tick_type_p_rail_guard
   and the slower (~6s) tension-dwell path below — so whichever timer
   fires first still escalates a genuine runout rather than fault-holding.
   Returns true if it escalated (caller must not also fault-hold/return its
   own value that tick). */
static bool sync_try_runout_escalation(lane_t *lane, uint32_t now_ms) {
    if (!(lane && g_reload_mode && lane->task == TASK_FEED && tc_state() == TC_IDLE &&
          !lane_in_present(lane) && !lane_out_present(lane))) {
        return false;
    }
    char lane_s[2];
    lane_id_str(lane_s, lane->lane_id);
    cmd_event("RUNOUT", lane_s);
    set_toolhead_filament(false);
    lane_stop(lane);
    reload_trigger(lane->lane_id, now_ms);
    return true;
}

static bool sync_tick_type_p_rail_guard(lane_t *lane, uint32_t now_ms) {
    if (g_buf_sensor_type != BUF_SENSOR_TYPE_P || !sync_enabled) {
        return false;
    }

    /* Gated to active sync: type-P rests at the tension rail (+1.0) whenever
       unloaded/idle/mid-tube, so this saturation catch must NOT run when the
       sync loop isn't driving — otherwise the resting home position trips
       sync_fault_hold() at idle and breaks a manual UL (the buffer is its own
       normal home, not a starvation fault). Over-tension is only a fault while
       actively syncing a loaded buffer. */
    if (g_vel_norm > CONF_PSF_JUMP_NORM_PER_S) {
        g_sync_fast_brake_until_ms = now_ms + CONF_PSF_STOP_CONFIRM_MS;
    }

    bool fast_brake_active =
        g_sync_fast_brake_until_ms != 0 && (int32_t)(g_sync_fast_brake_until_ms - now_ms) > 0;
    if (fast_brake_active) {
        if (g_vel_norm < TYPE_P_AUTO_START_VEL_NORM) {
            /* Extruder resumed or buffer recovered (moving back off compression,
               toward tension): clear brake, resume PD */
            g_sync_fast_brake_until_ms = 0;
        }
    } else if (g_sync_fast_brake_until_ms != 0 &&
               (int32_t)(now_ms - g_sync_fast_brake_until_ms) >= 0) {
        g_sync_fast_brake_until_ms = 0;
        if (g_buf_pos >= TYPE_P_RAIL_NORM) {
            sync_relief_pause();
            sync_apply_to_active();
            cmd_event("SYNC", "RELIEF_PAUSE");
            return true;
        }
    }

    if (g_buf_analog_saturated_since_ms != 0 &&
        (now_ms - g_buf_analog_saturated_since_ms) >= CONF_PSF_WALL_SAT_MS) {
        if (g_buf_pos >= TYPE_P_RAIL_NORM) {
            sync_relief_pause();
            sync_apply_to_active();
            cmd_event("SYNC", "RELIEF_PAUSE");
            return true;
        } else if (g_buf_pos <= -TYPE_P_RAIL_NORM) {
            if (sync_try_runout_escalation(lane, now_ms)) {
                return true;
            }
            sync_fault_hold();
            sync_apply_to_active();
            cmd_event("SYNC", "FAULT_HOLD");
            return true;
        }
    }
    return false;
}

static bool sync_tick_gated_checks(lane_t *lane, uint32_t now_ms) {
    if (sync_tick_type_p_rail_guard(lane, now_ms)) {
        return true;
    }

    if (g_sync_state == SYNC_FAULT_HOLD) {
        // VERIFY: retune from FAULT_HOLD/FAULT_HOLD_RECOVERY event logs
        if (now_ms - g_sync_fault_hold_entry_ms >= CONF_SYNC_FAULT_HOLD_RECOVERY_MS) {
            /* G1: direct resume to ACTIVE. The dead-reckoned model
             * accumulates a fictional TENSION with feed=0 during hold;
             * waiting for a TENSION event to re-arm either slams (fake
             * tension) or deadlocks neutral-print (sync off + extruder pulling
             * drains to COMPRESSION, never TENSION). Reseed the model to the
             * reserve target and re-enter ACTIVE directly, bootstrapped at
             * the baseline floor (F2b) so there is no overshoot. */
            g_buf_analog_saturated_since_ms = 0;
            cmd_event("SYNC", "FAULT_HOLD_RECOVERY");
            sync_rearm_active(lane, now_ms);
        } else {
            return true;
        }
    } else if (g_sync_state == SYNC_RETRACT_ASSIST || g_sync_state == SYNC_RELIEF_PAUSE) {
        if (g_sync_state == SYNC_RETRACT_ASSIST) {
            if (g_bl_sub_state != BL_IDLE) {
                sync_buffer_lock_tick(lane, now_ms);
            }
            return true;
        } else {
            /* 11.1 Proactive recovery from RELIEF_PAUSE: if debouncing was bypassed or raced,
             * re-arm sync immediately once debounced state is NEUTRAL during active print
             * or TENSION during feed. */
            buf_state_t s = g_buf.state;
            bool relief_rearm =
                (g_buf_sensor_type == BUF_SENSOR_TYPE_D)
                    ? (s == BUF_TENSION || (s == BUF_NEUTRAL && lane->task == TASK_FEED))
                    : ((g_buf_pos < TYPE_P_AUTO_START_POS_NORM) &&
                       (g_sync_tension_transitioned || g_vel_norm < TYPE_P_AUTO_START_VEL_NORM));
            if (relief_rearm) {
                sync_rearm_active(lane, now_ms);
            } else {
                return true;
            }
        }
    }
    return false;
}

static bool sync_tick_auto_start_stop(lane_t *lane, uint32_t now_ms, buf_state_t s) {
    /* Block auto-start while a manual unload state machine is running (cut path).
       TASK_UNLOAD guards the non-cut path; this covers the TASK_IDLE window inside
       MANUAL_UNLOAD_WAIT_FIRST_CLEAR / WAIT_CUT before the state machine completes. */
    bool auto_start_allowed =
        (lane->task == TASK_IDLE || lane->task == TASK_FEED) && !manual_unload_active();

    /* Do not auto-start sync when both OUT sensors are active: the hub has two
       filaments loaded simultaneously and the system is in manual recovery.
       The guard clears automatically once one lane's OUT sensor drops (i.e.
       after the operator unloads the unwanted lane).
       Also require at least one lane to have filament past its OUT sensor:
       without that, sync engaging at TENSION just spins an empty lane
       (post-unload state with buffer drained to tension and no filament
       past gate to feed). */
    bool l1_out = lane_out_present(&g_lane_l1);
    bool l2_out = lane_out_present(&g_lane_l2);
    bool any_lane_loaded = l1_out || l2_out;
    bool both_loaded = l1_out && l2_out;
    /* Type-P auto-start = buffer high AND under real extruder demand. Demand is
       either a fresh transition into the tension zone OR the buffer actively
       falling toward tension (g_vel_norm < 0, since tension is now the - rail).
       The velocity term is the robust signal: if the buffer rests already inside
       the goal-relative tension zone (goal is compression-side, so even a
       near-neutral rest reads TENSION), no fresh transition fires — but the
       extruder pulling still makes it fall. A static rest at home (-1.0) has
       vel~0, so this stays gated there (D18). */
    bool is_tension_active =
        (g_buf_sensor_type == BUF_SENSOR_TYPE_P)
            ? ((g_buf_pos < TYPE_P_AUTO_START_POS_NORM) &&
               (g_sync_tension_transitioned || g_vel_norm < TYPE_P_AUTO_START_VEL_NORM))
            : (s == BUF_TENSION);
    if (g_auto_mode && !sync_enabled && auto_start_allowed && is_tension_active &&
        !g_bl_autostart_suppressed && any_lane_loaded && !both_loaded) {
        /* Auto-correct the active lane to the physically loaded one. The operator
           may have switched the UI selection to an unloaded lane (to inspect or
           eject) and left it there. Tension with filament at the hub (YS) means
           the loaded lane is feeding, so adopt whichever lane has its OUT sensor
           engaged before enabling sync — otherwise we would drive the wrong
           (empty) lane. Double-load is already excluded by the guard above. */
        if (on_al(&g_y_split)) {
            if (lane_out_present(&g_lane_l1) && g_active_lane != 1) {
                set_active_lane(1);
                lane = lane_ptr(g_active_lane);
            } else if (lane_out_present(&g_lane_l2) && g_active_lane != 2) {
                set_active_lane(2);
                lane = lane_ptr(g_active_lane);
            }
        }
        bool tail_assist = !lane_in_present(lane) && lane_out_present(lane);
        int startup_sps = sync_bootstrap_sps();
        g_sync_current_sps = startup_sps;
        /* Count tension-dwell from activation, not a stale idle value. Because
           BUF_GOAL is compression-side, the buffer rests in the control TENSION
           zone while idle, so sync_tension_pin_since_ms (set on the NEUTRAL->TENSION
           transition, even while sync is OFF) accumulates a large stale dwell. On
           auto-start the tension-dwell fault would then fire instantly. Restart it. */
        g_sync_tension_pin_since_ms = (g_buf.state == BUF_TENSION) ? now_ms : 0;
        g_sync_tension_extreme = 0.0f;
        g_sync_tension_extreme_valid = false;
        g_sync_relief_active = false;
        g_sync_trip_armed = false;
        g_sync_probe_state = SYNC_PROBE_NONE;
        g_sync_probe_decided = false;
        g_sync_probe_peak_pos = g_buf_pos;
        sync_set_state(SYNC_ACTIVE);
        g_sync_auto_started = true;
        g_sync_tail_assist_active = tail_assist;
        g_sync_idle_since_ms = 0;
        cmd_event("SYNC", "AUTO_START");
    }

    if (!sync_enabled)
        return true;

    if (g_sync_auto_started) {
        if (g_sync_tail_assist_active) {
            if (s != BUF_COMPRESSION) {
                g_sync_idle_since_ms = 0;
            } else {
                if (g_sync_idle_since_ms == 0)
                    g_sync_idle_since_ms = now_ms;
                if (g_sync_auto_stop_ms > 0 &&
                    (now_ms - g_sync_idle_since_ms) > (uint32_t)g_sync_auto_stop_ms) {
                    sync_disable(true);
                    g_extruder_est_last_update_ms = now_ms;
                    sync_apply_to_active();
                    cmd_event("SYNC", "AUTO_STOP");
                    return true;
                }
            }
        } else {
            g_sync_idle_since_ms = 0;
        }
    }
    return false;
}

static float sync_apply_drift_correction(float bp_in, buf_state_t s, float thr,
                                         float reserve_deadband_mm) {
    float bp_eff = bp_in;
    float drift_correction_mm = 0.0f;
    int drift_min_samples = g_buf_drift_min_samples;
    if (drift_min_samples < 1)
        drift_min_samples = 1;
    bool drift_apply_gate = (g_buf_drift_apply_thr_mm > 0.0f) && (g_bp_drift_samples > 0) &&
                            (fabsf(g_bp_drift_ewma_mm) >= g_buf_drift_apply_thr_mm) &&
                            (g_buf_signal.confidence >= g_buf_drift_apply_min_cf);
    if (drift_apply_gate) {
        float sample_frac =
            clamp_f((float)g_bp_drift_samples / (float)drift_min_samples, 0.0f, 1.0f);
        drift_correction_mm =
            clamp_f(g_bp_drift_ewma_mm, -g_buf_drift_clamp_mm, g_buf_drift_clamp_mm) * sample_frac;
        float wall_taper_mm = reserve_deadband_mm * DRIFT_WALL_TAPER_MULT;
        if (wall_taper_mm < DRIFT_WALL_TAPER_MIN_MM)
            wall_taper_mm = DRIFT_WALL_TAPER_MIN_MM;

        if (drift_correction_mm < 0.0f) {
            float dist_from_tension_mm = bp_in + thr;
            float wall_frac = clamp_f(dist_from_tension_mm / wall_taper_mm, 0.0f, 1.0f);
            drift_correction_mm *= wall_frac;
        } else if (drift_correction_mm > 0.0f) {
            float dist_from_compression_mm = thr - bp_in;
            float wall_frac = clamp_f(dist_from_compression_mm / wall_taper_mm, 0.0f, 1.0f);
            drift_correction_mm *= wall_frac;
        }

        bp_eff = bp_in - drift_correction_mm;

        /* SAFETY: Don't let correction push bp_eff to the opposite side of physical state.
         * If we are physically at a wall, the controller must see it as at or beyond that wall. */
        if (s == BUF_COMPRESSION && bp_eff < thr)
            bp_eff = thr;
        else if (s == BUF_TENSION && bp_eff > -thr)
            bp_eff = -thr;

        /* CONFIDENCE BIAS: If we are uncertain, shift bp_eff toward the TENSION side.
         * This creates a gentle "feed pressure" that ensures we don't under-feed
         * while the model is drifting open-loop. This shift disappears as soon as
         * we hit a switch and restore confidence.
         * When blend is active (BLEND_FRAC > 0), confidence-bias is
         * redundant. Gate it off in that case. */
        if (g_buf_variance_blend_frac <= 0.0f) {
            float uncertainty_shift_mm =
                (1.0f - g_buf_signal.confidence) * (thr * DRIFT_CONFIDENCE_BIAS_FRAC);
            bp_eff -= uncertainty_shift_mm;
        }

        /* Clamp so correction cannot push bp_eff past the endstop zone boundary */
        bp_eff = clamp_f(bp_eff, -thr, thr);
    }
    g_bp_drift_correction_applied_mm = drift_correction_mm;
    return bp_eff;
}

static int sync_apply_type_d_probe_floor(buf_state_t s, int target_sps) {
    float tick_dt_s = (float)g_sync_tick_ms / MS_PER_SECOND_F;
    if (s == BUF_TENSION) {
        g_tension_floor_sps += (float)g_sync_tension_probe_up_sps_per_s * tick_dt_s;
    } else if (s == BUF_COMPRESSION) {
        g_tension_floor_sps -= (float)g_sync_tension_probe_down_sps_per_s * tick_dt_s;
    } else {
        /* NEUTRAL is the uncertain state: the buffer may be drifting to
         * either rail and only a click resolves it. Creep the floor up
         * (gently) so uncertainty always resolves into a COMPRESSION click
         * (safe, drains) rather than a metastable drift into TENSION (a
         * starve). The longer the dwell, the more we lean — uncertainty
         * grows with time since the last crossing. COMPRESSION then backs
         * it off, so this is a bounded, compression-biased sawtooth. */
        g_tension_floor_sps += (float)g_sync_tension_probe_neutral_sps_per_s * tick_dt_s;
    }
    if (g_tension_floor_sps > (float)g_sync_tension_probe_max_sps)
        g_tension_floor_sps = (float)g_sync_tension_probe_max_sps;
    if (g_tension_floor_sps < 0.0f)
        g_tension_floor_sps = 0.0f;

    if (s == BUF_NEUTRAL) {
        int floor_sps = (int)g_tension_floor_sps;
        if (floor_sps > g_sync_min_sps && target_sps < floor_sps)
            target_sps = floor_sps;
    }
    return target_sps;
}

/* Type-P bounded relief (D-01): the relief target is demand scaled by the
   persisted g_sync_psf_relief_mult, floored at the flow schedule's learned
   baseline for the current demand (never ask for less than the schedule
   already knows is needed), and finally clamped to max_sps -- a second,
   independent ceiling so even an unclamped g_sync_psf_relief_mult (REVIEW-05
   covers that separately, via settings_apply_clamps()) can never command
   above the configured max rate. Defined ahead of
   sync_check_tension_dwell_and_ramp below (D-04, Task 2) so that ramp path
   can share the same bound rather than duplicating the formula. */
static int sync_type_p_relief_bound_sps(int max_sps) {
    int demand_sps = (int)g_extruder_est_sps;
    int bound_sps = (int)((float)demand_sps * g_sync_psf_relief_mult);
    int baseline_sps = flow_param(demand_sps).baseline_sps;
    if (baseline_sps > bound_sps)
        bound_sps = baseline_sps;
    if (bound_sps <= 0)
        bound_sps = g_sync_min_sps;
    if (bound_sps > max_sps)
        bound_sps = max_sps;
    return bound_sps;
}

/* Phase 13 Plan 02 (D-07/D-09/D-16): shared fault-hold body the mm and ms
   tension traps both run once their own threshold condition trips -- try
   runout escalation first (a lane that genuinely ran out hands off to
   RELOAD/RUNOUT instead of looping FAULT_HOLD/AUTO_START forever), otherwise
   fault-hold and reset the estimator timestamp. Always returns -1 (the
   caller's target_sps sentinel for "tick already handled, apply and stop"). */
static int sync_tension_trip_fire(lane_t *lane, uint32_t now_ms) {
    if (sync_try_runout_escalation(lane, now_ms)) {
        return -1;
    }
    sync_fault_hold();
    g_extruder_est_last_update_ms = now_ms;
    sync_apply_to_active();
    cmd_event("SYNC", "FAULT_HOLD");
    return -1;
}

/* Phase 13 Plan 03 (D-16/REVIEW-03): the probe's effective distance, clamped
   strictly below the distance trip's own threshold on the SAME accumulator
   so the ordering (probe resolves before the trip can fire) is a structural
   property of the code, not an accident of two defaults. g_buf_max_travel_mm
   is clamped to [10, 1000]mm (settings_store.c), so the UNCLAMPED geometry
   basis this reads (declared in sync_internal.h) can exceed
   g_sync_tension_stop_mm's 32mm default on any rig with a larger buffer --
   without this clamp the trip fires first, resets the shared accumulator,
   and the probe never evaluates at all on that rig (a silent feature
   disable reached by configuration, not calibration). When the distance
   trip is disabled (g_sync_tension_stop_mm 0) there is no ordering to
   preserve, and clamping against 0 would collapse the probe distance to 0
   and fire it on the first millimetre -- exactly the inversion this
   function exists to prevent -- so that case returns the raw geometry basis
   unclamped. math.h (fminf) is already included at sync.c:9. The geometry
   read happens exactly once, into a local, right here -- nothing else in
   this file may compare that alias against the accumulator directly. */
static float sync_type_p_probe_mm(void) {
    float geometry_mm = SYNC_FEED_PROBE_MM;
    if (g_sync_tension_stop_mm > 0.0f) {
        return fminf(geometry_mm, g_sync_tension_stop_mm * SYNC_PROBE_TRIP_FRAC);
    }
    return geometry_mm;
}

static int sync_check_tension_dwell_and_ramp(lane_t *lane, buf_state_t s, int target_sps,
                                             uint32_t now_ms) {
    if (s == BUF_TENSION && g_sync_tension_pin_since_ms != 0) {
        uint32_t tension_dwell_ms = now_ms - g_sync_tension_pin_since_ms;
        /* Phase 13 Plan 02 (D-07/D-08/D-09): distance is the PRIMARY trip,
           evaluated BEFORE the ms fallback below so the ordering is
           structural, not incidental. Reuses the same g_sync_refill_effort_mm
           accumulator sync_buf.c's warn-only cannot_refill event already
           reads (no parallel counter, 13-RESEARCH.md "Don't Hand-Roll").
           Requires: type-D excluded (D-14, same exclusion the ms path below
           carries -- a type-D relay TENSION contact is the normal refill
           signal, not a fault); armed (D-10 -- a real buffer-state
           transition has been observed since the last re-entry or hold
           release); no deliberate hold in progress (D-26); the lane's own IN
           sensor still present (D-21 -- with IN clear this is an ordinary
           runout and the ordinary runout path already owns that outcome, so
           the distance trip stands down rather than racing it); and the
           knob itself armed (0 disables, a documented in-band value, not an
           out-of-range escape).

           REVIEW-06: the 32 mm default sits BELOW the pre-existing 50 mm
           warn-only CONF_SYNC_CANNOT_REFILL_MM threshold on this SAME
           accumulator (sync_buf.c) -- at defaults this trip fires and
           resets the accumulator before the warn can ever reach 50 mm, so
           the warn becomes practically dormant for type-P and stays
           primarily a type-D diagnostic. This is a documented threshold
           interaction, not a bug to fix by moving either threshold: raising
           this knob above 50, or setting it to 0, makes the warn reachable
           again. On a well-calibrated rail the 1 s CONF_PSF_WALL_SAT_MS
           saturation guard (sync_tick_type_p_rail_guard) usually reaches
           fault-hold first; this distance trip is the primary protection
           specifically on the shallow-reading rigs where that
           absolute-threshold guard never fires at all. */

        /* Phase 13 Plan 03 (D-15/D-16): the feed probe evaluates BEFORE the
           distance trip below, under the IDENTICAL guards (type-P only,
           armed, no deliberate hold, lane IN sensor still present) -- the
           same accumulator, at a threshold clamped strictly lower
           (sync_type_p_probe_mm()), so the ordering is structural rather
           than a sequencing flag (D-16). The window-max latch
           (g_sync_probe_peak_pos) updates every tick the probe runs,
           independent of whether this tick's probe distance has been
           reached -- the verdict at the threshold-crossing tick must read
           the BEST deflection observed across the whole window (REVIEW-02),
           not the sample at that one tick. */
        if (g_buf_sensor_type != BUF_SENSOR_TYPE_D && g_sync_trip_armed &&
            !sync_type_p_hold_in_progress() && lane_in_present(lane)) {
            if (g_sync_probe_state == SYNC_PROBE_NONE) {
                g_sync_probe_state = SYNC_PROBE_RUNNING;
                g_sync_probe_peak_pos = g_buf_pos;
            }
            if (g_sync_probe_state == SYNC_PROBE_RUNNING) {
                g_sync_probe_peak_pos = fmaxf(g_sync_probe_peak_pos, g_buf_pos);
                if (!g_sync_probe_decided &&
                    g_sync_refill_effort_mm >= sync_type_p_probe_mm()) {
                    g_sync_probe_decided = true;
                    /* CONSUMER: the best deflection observed this window
                       moved at least BL_BREAK_DELTA_NORM back off the
                       tracked tension extreme (D-23 -- the one
                       hardware-validated "moved off a rail" delta, reused
                       verbatim, no new magic number). NO_CONSUMER
                       otherwise, including when the extreme was never
                       tracked valid (no baseline to compare against yet --
                       treat as "never observed to move"). */
                    bool moved_off_rail = g_sync_tension_extreme_valid &&
                        (g_sync_probe_peak_pos >=
                         g_sync_tension_extreme + BL_BREAK_DELTA_NORM);
                    if (moved_off_rail) {
                        g_sync_probe_state = SYNC_PROBE_CONSUMER;
                        cmd_event("SYNC", "PROBE:CONSUMER");
                    } else {
                        g_sync_probe_state = SYNC_PROBE_NO_CONSUMER;
                        cmd_event("SYNC", "PROBE:NO_CONSUMER");
                        /* D-18/D-21: short-circuit the remaining trip budget
                           straight into the shared escalate-then-fault-hold
                           path. sync_try_runout_escalation() declines while
                           the lane's IN sensor reads present (checked just
                           above), so the practical outcome with filament
                           loaded is fault-hold -- which is intended: D-15's
                           ambiguous case is precisely "sensor says present,
                           buffer says nothing moves". */
                        return sync_tension_trip_fire(lane, now_ms);
                    }
                }
            }
        }

        if (g_buf_sensor_type != BUF_SENSOR_TYPE_D && g_sync_trip_armed &&
            !sync_type_p_hold_in_progress() && lane_in_present(lane) &&
            g_sync_tension_stop_mm > 0.0f && g_sync_refill_effort_mm >= g_sync_tension_stop_mm) {
            cmd_event("SYNC", "TENSION_STOP:MM");
            return sync_tension_trip_fire(lane, now_ms);
        }
        /* RELAY: TENSION switch contact is the normal "buffer empty, refill"
         * signal, not a fault. Only fault-hold on tension dwell in analog
         * mode; the type-D relay catch-up path refills it. Phase 13 Plan 02
         * (D-09/D-10): now the slow-flow FALLBACK behind the mm trip above
         * -- gated on the same arm flag and the same deliberate-hold
         * suppression so the two traps arm/disarm in lockstep and neither
         * can fire mid-hold (D-26). */
        if (g_buf_sensor_type != BUF_SENSOR_TYPE_D && g_sync_trip_armed &&
            !sync_type_p_hold_in_progress() && g_sync_tension_dwell_stop_ms > 0 &&
            tension_dwell_ms >= (uint32_t)g_sync_tension_dwell_stop_ms) {
            cmd_event("SYNC", "TENSION_STOP:MS");
            return sync_tension_trip_fire(lane, now_ms);
        }
        if (g_sync_tension_ramp_delay_ms > 0 &&
            tension_dwell_ms >= (uint32_t)g_sync_tension_ramp_delay_ms) {
            int max_sps = sync_clamp_max_sps(g_sync_max_sps);
            if (target_sps < max_sps)
                target_sps = max_sps;
            /* D-04 (Task 2): cap this escalation at the same relief bound the
               apply-side branch (sync_tick_apply_rate) uses. Without this,
               the ramp can raise target_sps to max_sps while the buffer
               sits in the debounced BUF_TENSION state but OUTSIDE the
               extreme-relative relief zone (in_relief_zone false) -- that
               value then flows into the ORDINARY (non-relief) type-P
               smoothing branch, which has no bound of its own and would
               eventually converge feed toward the raw max while the plant
               is still physically saturated. Guarded to type-P only:
               type-D's ramp behaviour (its own fault-hold guard at the top
               of this function already excludes it there) is unchanged. */
            if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
                int bound_sps = sync_type_p_relief_bound_sps(max_sps);
                if (target_sps > bound_sps)
                    target_sps = bound_sps;
            }
        }
    }
    return target_sps;
}

static int sync_apply_compression_recovery_cap(buf_state_t s, int target_sps, uint32_t now_ms) {
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && g_sync_compression_recovery_active) {
        uint32_t compression_recovery_ms = now_ms - g_buf.entered_ms;
        int compression_floor_sps = sync_compression_floor_sps();
        int kp_window = sync_effective_kp_sps(s);
        int recovery_cap = (int)g_extruder_est_sps - kp_window;
        if (compression_recovery_ms > SYNC_COMPRESSION_COLLAPSE_DELAY_MS) {
            uint32_t collapse_ms = compression_recovery_ms - SYNC_COMPRESSION_COLLAPSE_DELAY_MS;
            if (collapse_ms > SYNC_COMPRESSION_COLLAPSE_CAP_MS)
                collapse_ms = SYNC_COMPRESSION_COLLAPSE_CAP_MS;
            int extra_trim =
                (int)(((uint64_t)collapse_ms * (uint64_t)(kp_window + g_pre_ramp_sps)) /
                      (uint64_t)SYNC_COMPRESSION_COLLAPSE_CAP_MS);
            recovery_cap -= extra_trim;
        }
        if (recovery_cap < compression_floor_sps)
            recovery_cap = compression_floor_sps;
        /* F1b: collapse braking is for an over-tensioned buffer draining
         * through COMPRESSION; while still in NEUTRAL it must not starve the
         * refill below the learned baseline. */
        if (s == BUF_NEUTRAL) {
            int neutral_floor = baseline_control_floor_sps();
            if (recovery_cap < neutral_floor)
                recovery_cap = neutral_floor;
        }
        if (target_sps > recovery_cap)
            target_sps = recovery_cap;
    }
    return target_sps;
}

static void sync_sample_mmu_dwell(lane_t *lane) {
    if (lane && (lane->task == TASK_FEED || lane->task == TASK_IDLE) && lane->fault == FAULT_NONE &&
        g_sync_state == SYNC_ACTIVE) {
        if (g_buf.mmu_sps_dwell_samples >= SYNC_MMU_DWELL_SAMPLE_MAX) {
            g_buf.mmu_sps_dwell_sum /= SYNC_MMU_DWELL_DECAY_DIVISOR;
            g_buf.mmu_sps_dwell_samples /= SYNC_MMU_DWELL_DECAY_DIVISOR;
        }
        g_buf.mmu_sps_dwell_sum += (uint32_t)lane_motion_sps(lane);
        g_buf.mmu_sps_dwell_samples++;
    }
}

static float sync_effective_reserve_target(buf_state_t s, float bp_eff, float raw_target,
                                           uint32_t now_ms) {
    bool integral_active = (s == BUF_NEUTRAL) && (g_sync_reserve_integral_gain > 0.0f) &&
                           (g_buf_signal.confidence >= RESERVE_INTEGRAL_MIN_CF);
    if (integral_active) {
        float raw_error = bp_eff - raw_target;
        float dt_s = (float)g_sync_tick_ms / MS_PER_SECOND_F;
        g_sync_reserve_integral_mm -= g_sync_reserve_integral_gain * raw_error * dt_s;
        g_sync_reserve_integral_mm =
            clamp_f(g_sync_reserve_integral_mm, -g_sync_reserve_integral_clamp_mm,
                    +g_sync_reserve_integral_clamp_mm);
    }
    /* TENSION_DWELL_WARN: integral saturated toward tension side — rate-limited 10 s */
    if (sync_enabled && g_sync_reserve_integral_gain > 0.0f &&
        g_sync_reserve_integral_mm <
            -(g_sync_reserve_integral_clamp_mm * RESERVE_INTEGRAL_WARN_FRAC)) {
        if (g_buf_tension_dwell_warn_emit_ms == 0 ||
            (now_ms - g_buf_tension_dwell_warn_emit_ms) >= SYNC_TENSION_DWELL_WARN_INTERVAL_MS) {
            g_buf_tension_dwell_warn_emit_ms = now_ms;
            cmd_event("SYNC", "TENSION_DWELL_WARN");
        }
    }
    return raw_target + g_sync_reserve_integral_mm;
}

static void sync_warn_tension_risk(uint32_t now_ms) {
    if (g_tension_risk_threshold <= 0) {
        return;
    }

    int tension_pin_count = sync_tension_pin_window_count(now_ms);
    if (tension_pin_count < g_tension_risk_threshold) {
        return;
    }

    if (g_tension_risk_emit_ms == 0 ||
        (now_ms - g_tension_risk_emit_ms) >= SYNC_TENSION_RISK_WARN_INTERVAL_MS) {
        g_tension_risk_emit_ms = now_ms;
        cmd_event("SYNC", "TENSION_RISK_HIGH");
    }
}

static int sync_tick_calculate_target(buf_state_t s, uint32_t now_ms, lane_t *lane) {
    sync_sample_mmu_dwell(lane);
    float raw_target = buf_target_reserve_mm();
    float reserve_deadband_mm = buf_virtual_deadband_mm();

    g_buf_pos_raw_status = g_buf_pos;
    /* Variance-aware position blend (default OFF) */
    float blended_pos = g_buf_pos;
    if (g_buf_variance_blend_frac > 0.0f && g_buf_sigma_mm > 0.0f) {
        float sigma_ref = (g_buf_variance_blend_ref_mm > VARIANCE_BLEND_REF_MIN_MM)
                              ? g_buf_variance_blend_ref_mm
                              : VARIANCE_BLEND_REF_FALLBACK_MM;
        float distrust = clamp_f(g_buf_sigma_mm / sigma_ref, 0.0f, 1.0f);
        float blend = distrust * g_buf_variance_blend_frac;
        blended_pos = (1.0f - blend) * g_buf_pos + blend * raw_target;
    }

    float thr = buf_threshold_mm();
    if (thr < BUFFER_THRESHOLD_MIN_MM)
        thr = BUFFER_THRESHOLD_MIN_MM;

    /* Effective buffer position with drift correction (default OFF) */
    float bp_eff = sync_apply_drift_correction(blended_pos, s, thr, reserve_deadband_mm);

    float effective_target = sync_effective_reserve_target(s, bp_eff, raw_target, now_ms);

    float pos_norm = (g_buf_sensor_type == BUF_SENSOR_TYPE_P) ? g_buf_pos : (bp_eff / thr);
    float target_norm =
        (g_buf_sensor_type == BUF_SENSOR_TYPE_P) ? psf_goal_norm() : (effective_target / thr);
    float error_norm = pos_norm - target_norm;
    float deadband_norm = (g_buf_sensor_type == BUF_SENSOR_TYPE_P) ? TYPE_D_DEADBAND_NORM
                                                                   : (reserve_deadband_mm / thr);

    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D) {
        type_d_neutral_feed_sample(s, pos_norm, target_norm, deadband_norm);
        relay_neutral_trim_leak(s, now_ms);
    }

    int target_sps;
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D) {
        target_sps = relay_control_law(s);
    } else {
        target_sps = psf_control_law(error_norm);
    }
    int type_d_neutral_relay_floor_sps =
        (g_buf_sensor_type == BUF_SENSOR_TYPE_D && s == BUF_NEUTRAL && error_norm < -deadband_norm)
            ? target_sps
            : 0;

    /* RAMPING BIAS: If we don't know where we are, raise speed a little bit
     * until we touch compression. This probe speed (up to ~150mm/min) ensures
     * we gravitate toward the safe compression wall rather than drifting toward tension. */
    if (g_buf_signal.confidence < 1.0f && s == BUF_NEUTRAL) {
        float uncertainty = 1.0f - g_buf_signal.confidence;
        target_sps += (int)(uncertainty * UNCERTAINTY_PROBE_BIAS_SPS);
    }

    target_sps = sync_check_tension_dwell_and_ramp(lane, s, target_sps, now_ms);
    if (target_sps < 0) {
        return -1;
    }

    sync_warn_tension_risk(now_ms);

    target_sps = sync_apply_scaling(target_sps, target_norm, pos_norm);

    if (g_sync_post_compression_boost_until_ms != 0) {
        if ((int32_t)(g_sync_post_compression_boost_until_ms - now_ms) > 0)
            target_sps += g_pre_ramp_sps;
        else
            g_sync_post_compression_boost_until_ms = 0;
    }

    int neutral_anti_tension_floor_sps = sync_neutral_anti_tension_floor_sps(
        s, lane, error_norm, deadband_norm, type_d_neutral_relay_floor_sps, now_ms);
    if (neutral_anti_tension_floor_sps > 0 && target_sps < neutral_anti_tension_floor_sps) {
        target_sps = neutral_anti_tension_floor_sps;
    }

    target_sps = sync_apply_compression_recovery_cap(s, target_sps, now_ms);

    /* Type-D NEUTRAL relay output is the demand estimate plus the configured
     * lean. Preserve it only while reserve is tension-side; once at/above target,
     * shared shapers must be able to brake before the COMPRESSION switch. */
    if (type_d_neutral_relay_floor_sps > 0 && target_sps < type_d_neutral_relay_floor_sps) {
        target_sps = type_d_neutral_relay_floor_sps;
    }

    /* Type-D tension recovery: symmetric AIMD hunt on a held feed floor.
     * TENSION = still starved -> probe up; COMPRESSION = overfed -> ease down;
     * NEUTRAL = hold the found level. COMPRESSION (not a timeout) is the
     * recovery-complete signal, so no value of any interval is guessed. */
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D) {
        target_sps = sync_apply_type_d_probe_floor(s, target_sps);
    }
    return target_sps;
}

static int sync_apply_type_p_smoothing(int target_sps, float dt_s, float slew_per_mm,
                                       float filter_len_mm) {
    /* Type-P distance-based smoothing (Happy-Hare-style). Both the target
       EMA and the slew limit are keyed to filament distance moved this tick,
       not wall-clock — so feed changes scale with flow and go to zero when
       the printer is idle, instead of the old time-ramp (flow-blind, 2-tick
       bang-bang) or the direct apply (snaps to the noisy PD target instantly).
       "move" = extruder flow demand this tick (the system's distance clock).

       slew_per_mm/filter_len_mm are parameters, not read from the caller's
       smoothing globals directly (REVIEW-01): the ordinary type-P branch
       passes its usual smoothing globals unchanged (bit-identical to before
       this parameterization), while the bounded-relief branch passes a
       doubled slew and a divided filter length so the bound actually
       arrives before the rail does on a 16 mm buffer. One shared
       g_psf_target_filt state, two parameter sets — not a second filter. */
    uint8_t idx = (g_active_lane == 2) ? 1 : 0;
    float move = fabsf(g_extruder_est_sps) * g_mm_per_step[idx] * dt_s;

    /* 1. EMA the target over distance: alpha = 1 - exp(-move/L). Floor keeps
          the relief branch's filter_len_mm/SYNC_RELIEF_FILTER_DIV from ever
          producing a zero-or-negative EMA length. */
    float eff_filter_len_mm =
        (filter_len_mm > PSF_FILTER_MIN_MM) ? filter_len_mm : PSF_FILTER_MIN_MM;
    float alpha = 1.0f - expf(-move / eff_filter_len_mm);
    g_psf_target_filt += alpha * ((float)target_sps - g_psf_target_filt);

    /* 2. Slew-limit the applied rate, clamped so it never overshoots the
          filtered target (overshoot is what made the old ramp oscillate).
          Floor the step so a tiny creep is always allowed off idle. */
    float max_step = slew_per_mm * move;
    if (max_step < 1.0f)
        max_step = 1.0f;
    float cur = (float)g_sync_current_sps;
    if (g_psf_target_filt > cur + max_step)
        cur += max_step;
    else if (g_psf_target_filt < cur - max_step)
        cur -= max_step;
    else
        cur = g_psf_target_filt;

    /* Overfeed guard: the distance clock (move) freezes the filter when the
       extruder stops (move -> 0 => alpha, max_step -> 0), holding the feed at
       the pre-stop rate so the MMU keeps pushing into a stopped extruder and
       slams COMPRESSION. Feed *drops* are always safe (worst case is a brief
       tension excursion, which has room), so additionally let the feed fall
       toward the raw target on wall-clock, independent of flow. Take whichever
       path is lower and pin the filtered target so it can't re-drive the feed
       back up while demand stays low. UP is untouched (purely flow-keyed). */
    if (g_sync_psf_decay_sps_per_s > 0.0f) {
        float wall_cur = cur - g_sync_psf_decay_sps_per_s * dt_s;
        if (wall_cur < (float)target_sps)
            wall_cur = (float)target_sps;
        if (wall_cur < cur) {
            cur = wall_cur;
            if (g_psf_target_filt > cur)
                g_psf_target_filt = cur;
        }
    }
    return (int)(cur + ROUND_TO_NEAREST_F);
}

static bool sync_check_continuous_compression(buf_state_t s, uint32_t now_ms) {
    if (g_sync_auto_started && !g_sync_tail_assist_active) {
        if (s == BUF_COMPRESSION) {
            // Start or maintain the continuous physical dwell timer
            if (g_sync_continuous_compression_since_ms == 0) {
                g_sync_continuous_compression_since_ms = now_ms;
            }

            uint32_t compression_dwell_ms = now_ms - g_sync_continuous_compression_since_ms;

            // Define a widened floor threshold to ignore PID hunting/noise
            int effective_floor_sps = sync_compression_floor_sps() + g_pre_ramp_sps;

            uint32_t floor_timeout_ms = (uint32_t)g_sync_auto_stop_ms;

            if (floor_timeout_ms > 0 && compression_dwell_ms > floor_timeout_ms) {
                if (g_sync_current_sps <= effective_floor_sps) {
                    sync_relief_pause();
                    g_extruder_est_last_update_ms = now_ms;
                    sync_apply_to_active();
                    cmd_event("SYNC", "RELIEF_PAUSE");
                    return true;
                }
            }
        } else {
            // ONLY reset the timer when the arm physically leaves the compression switch
            g_sync_continuous_compression_since_ms = 0;
        }
    }
    return false;
}

static int sync_type_d_compression_drain_target(int max_sps, lane_t *lane) {
    int demand_sps = (int)g_extruder_est_sps;
    int idle_threshold_sps = g_sync_min_sps;
    if (idle_threshold_sps < 1)
        idle_threshold_sps = 1;
    if (lane && lane->task == TASK_FEED && demand_sps > idle_threshold_sps &&
        g_sync_compression_drain_frac > 0.0f &&
        g_sync_relieve_effort_mm < g_sync_compression_drain_budget_mm) {
        int drain_sps = (int)((float)demand_sps * g_sync_compression_drain_frac);
        if (drain_sps < 0)
            drain_sps = 0;
        if (drain_sps > max_sps)
            drain_sps = max_sps;
        if (drain_sps >= demand_sps)
            drain_sps = demand_sps - 1;
        return drain_sps;
    }
    return 0;
}

/* Type-P bounded relief entry (D-22/D-23): rail-relative, never an absolute
   normalized-position literal (the 51bdca8 root cause this phase's carried
   caveat exists to prevent). Once an extreme has been observed this sync
   window, compare against it directly; before one exists, fall back to the
   debounced BUF_TENSION state (itself goal-relative -- sync_buf.c). */
static bool sync_type_p_in_relief_zone(buf_state_t s) {
    if (g_sync_tension_extreme_valid)
        return g_buf_pos <= g_sync_tension_extreme + SOFT_WALL_MARGIN_NORM;
    return s == BUF_TENSION;
}

/* Type-P tension extreme tracking + staleness relaxation (D-22/REVIEW-07).
   Deepens the tracked extreme while starved, and separately relaxes it once
   the buffer is observed well off the rail -- both rail-relative and
   clock-free, no wall-clock decay, no tick counter, no numeric position
   literal. The relaxation runs on every type-P tick while sync is enabled,
   not only in BUF_TENSION, since the recovery it watches for happens
   outside the tension zone by definition. Without it, a single
   uncalibrated deep deflection spike would depress the relief threshold for
   the rest of a long print (the same silent-disable family as 51bdca8,
   reached by a different route). Its failure direction is "relief fires
   slightly more often", already bounded by the RELIEF_ON fewer-than-10-per-
   run assertion. */
static void sync_type_p_track_tension_extreme(buf_state_t s) {
    if (!sync_enabled || g_buf_sensor_type != BUF_SENSOR_TYPE_P)
        return;

    if (s == BUF_TENSION) {
        if (!g_sync_tension_extreme_valid || g_buf_pos < g_sync_tension_extreme) {
            g_sync_tension_extreme = g_buf_pos;
            g_sync_tension_extreme_valid = true;
        }
    }

    if (g_sync_tension_extreme_valid &&
        g_buf_pos >= g_sync_tension_extreme + SYNC_EXTREME_RELAX_MULT * BL_BREAK_DELTA_NORM) {
        g_sync_tension_extreme = g_buf_pos;
    }
}

/// @brief Track the type-P tension extreme and detect the relief-zone edge
/// for this tick, emitting RELIEF_ON/RELIEF_OFF exactly once per transition
/// and seeding the smoothing target on entry (D-06/D-22/D-23). Split out of
/// sync_tick_apply_rate to keep it under the STYLE.md function-size limit.
static bool sync_type_p_update_relief_zone(buf_state_t s, bool fast_brake_active) {
    sync_type_p_track_tension_extreme(s);

    /* D-22/D-23: fast_brake takes priority over relief entry, matching the
       original branch's precedence (a fast_brake episode zeroes feed
       regardless of buffer position). */
    bool in_relief_zone = !fast_brake_active && g_buf_sensor_type == BUF_SENSOR_TYPE_P &&
                          sync_type_p_in_relief_zone(s);

    /* D-06: edge-triggered RELIEF_ON/RELIEF_OFF, never per tick. */
    if (in_relief_zone != g_sync_relief_active) {
        g_sync_relief_active = in_relief_zone;
        cmd_event("SYNC", in_relief_zone ? "RELIEF_ON" : "RELIEF_OFF");
        if (in_relief_zone) {
            /* Seed the smoothing target at DEMAND, exactly once on entry --
               seeding every tick would defeat the EMA the bound is supposed
               to run through. Once the buffer climbs out of the relief zone
               the smoothing resumes from here, so feed eases to the
               extruder rate instead of staying pinned high. */
            g_psf_target_filt = g_extruder_est_sps;
        }
    }
    return in_relief_zone;
}

/// @brief Apply the bounded relief target through the smoothing path
/// (D-01/D-02/REVIEW-01). Split out of sync_tick_apply_rate to keep it
/// under the STYLE.md function-size limit.
static void sync_type_p_apply_relief(int target_sps, int max_sps) {
    /* Bounded relief (D-01/D-02): replaces the old direct-apply snap to
       target_sps. The bound is routed through the SAME smoothing path
       as the ordinary type-P branch below, but with a doubled slew
       allowance and a shortened EMA (REVIEW-01) so it actually arrives
       before the rail does on a 16 mm buffer -- doubling max_step alone
       in step 2 of sync_apply_type_p_smoothing cannot outrun a
       throttled step 1. */
    int bound_sps = sync_type_p_relief_bound_sps(max_sps);
    int relief_target_sps = (target_sps < bound_sps) ? target_sps : bound_sps;
    float dt_s = (float)g_sync_tick_ms / MS_PER_SECOND_F;
    g_sync_current_sps = sync_apply_type_p_smoothing(relief_target_sps, dt_s,
                                                     g_sync_psf_slew_per_mm * SYNC_RELIEF_SLEW_MULT,
                                                     g_sync_psf_filter_mm / SYNC_RELIEF_FILTER_DIV);
}

static void sync_tick_apply_rate(int target_sps, buf_state_t s, uint32_t now_ms, lane_t *lane) {
    bool compression_wall_critical = false;
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && s == BUF_COMPRESSION) {
        float compression_push_mm_s = sync_compression_wall_velocity_mm_s(lane);
        float compression_wall_ms = sync_compression_wall_time_ms(lane);
        compression_wall_critical = compression_push_mm_s > SYNC_COMPRESSION_HARD_PUSH_MM_S &&
                                    compression_wall_ms < SYNC_COMPRESSION_HARD_WALL_MS;
    }
    /* RELAY: hitting the COMPRESSION switch is the normal "buffer full,
     * back off" control signal, not a jam. This critical-fault only ever
     * fired in type-D mode and was manufacturing the FAULT_HOLD ->
     * recovery-slam cycle. Suppress it in relay mode; the relay stop state
     * drains the full buffer off the compression wall. */
    if (compression_wall_critical && g_buf_sensor_type != BUF_SENSOR_TYPE_D) {
        sync_fault_hold();
        g_extruder_est_last_update_ms = now_ms;
        sync_apply_to_active();
        cmd_event("SYNC", "FAULT_HOLD");
        return;
    }

    bool fast_brake_active =
        g_sync_fast_brake_until_ms != 0 && (int32_t)(g_sync_fast_brake_until_ms - now_ms) > 0;
    if (!fast_brake_active && g_sync_fast_brake_until_ms != 0 &&
        (int32_t)(now_ms - g_sync_fast_brake_until_ms) >= 0)
        g_sync_fast_brake_until_ms = 0;

    int max_sps = sync_clamp_max_sps(g_sync_max_sps);
    if (fast_brake_active)
        target_sps = 0;
    /* Type-D COMPRESSION drain: keep this branch above the SYNC_MIN clamp.
     * A zero drain fraction is the legacy hard-stop A/B guard; active draw may
     * feed a bounded fraction of demand, strictly below demand, so the buffer
     * still drains off the compression rail. Idle/end-of-feed remains a true
     * zero to preserve purge no-grind behavior. */
    else if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && s == BUF_COMPRESSION) {
        target_sps = sync_type_d_compression_drain_target(max_sps, lane);
    } else
        target_sps = clamp_i(target_sps, g_sync_min_sps, max_sps);

    int ramp_dn_sps = g_sync_ramp_dn_sps;
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && !fast_brake_active &&
        g_sync_compression_recovery_active && s == BUF_COMPRESSION) {
        uint32_t compression_recovery_ms = now_ms - g_buf.entered_ms;
        if (compression_recovery_ms > SYNC_COMPRESSION_COLLAPSE_DELAY_MS) {
            ramp_dn_sps *= SYNC_COMPRESSION_COLLAPSE_RAMP_MULT;
        }
    }

    bool in_relief_zone = sync_type_p_update_relief_zone(s, fast_brake_active);

    if (fast_brake_active) {
        g_sync_current_sps = 0;
        g_psf_target_filt = 0.0f;
    } else if (in_relief_zone) {
        sync_type_p_apply_relief(target_sps, max_sps);
    } else if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
        float dt_s = (float)g_sync_tick_ms / MS_PER_SECOND_F;
        g_sync_current_sps = sync_apply_type_p_smoothing(target_sps, dt_s, g_sync_psf_slew_per_mm,
                                                         g_sync_psf_filter_mm);
    } else if (g_sync_current_sps > target_sps) {
        g_sync_current_sps -= ramp_dn_sps;
        if (g_sync_current_sps < target_sps)
            g_sync_current_sps = target_sps;
    } else if (g_sync_current_sps < target_sps) {
        g_sync_current_sps += g_sync_ramp_up_sps;
        if (g_sync_current_sps > target_sps)
            g_sync_current_sps = target_sps;
    }

    g_sync_current_sps = clamp_i(g_sync_current_sps, 0, max_sps);

    if (sync_check_continuous_compression(s, now_ms)) {
        return;
    }

    sync_apply_to_active();

    if ((now_ms - g_sync_last_evt_ms) >= SYNC_STATUS_EVENT_INTERVAL_MS) {
        g_sync_last_evt_ms = now_ms;
        char ev[SYNC_STATUS_EVENT_MAX];
        snprintf(ev, sizeof(ev), "%s,%.1f,%.2f", buf_state_name(s),
                 (double)sps_to_mm_per_min(g_sync_current_sps), (double)g_buf_pos);
        cmd_event("BS", ev);
    }
}

// ============================================================================
// Sync main tick & state queries — entry point called every main loop pass
// ============================================================================

void sync_tick(uint32_t now_ms) {
    /* REVIEW-04: must run BEFORE every early return below -- the g_bypass/
       tc_state()/g_boot_stabilizing guard, sync_tick_gated_checks(),
       sync_tick_auto_start_stop(), and the tick-rate gate further down all
       swallow a hold's falling edge if the sampler sits under any of them.
       sync_tick() is called unconditionally every main-loop pass
       (main.c:~697), so this is the one position that samples the hold
       predicate on every pass regardless of bypass, toolchange, gate, or
       tick-rate state. A later refactor that moves this call below any of
       the four returns named above is silently wrong -- do not move it. */
    sync_trip_track_hold_edge();

    lane_t *lane = lane_ptr(g_active_lane);
    if (g_bypass || !lane || tc_state() != TC_IDLE || g_boot_stabilizing)
        return;

    if (sync_tick_gated_checks(lane, now_ms))
        return;

    buf_state_t s = g_buf.state;

    if (sync_tick_auto_start_stop(lane, now_ms, s))
        return;

    if ((now_ms - g_sync_last_tick_ms) < (uint32_t)g_sync_tick_ms)
        return;

    g_sync_last_tick_ms = now_ms;

    if (s == BUF_FAULT) {
        g_sync_current_sps = 0;
        sync_apply_to_active();
        cmd_event("BS", "FAULT,0");
        return;
    }

    int target_sps = sync_tick_calculate_target(s, now_ms, lane);
    if (target_sps < 0)
        return;

    sync_tick_apply_rate(target_sps, s, now_ms, lane);
}

bool sync_is_positive_relaunch_damped(void) {
    if (g_sync_tail_assist_active)
        return false;
    // Never damp while the buffer is empty — refill must be unrestricted.
    if (g_buf.state == BUF_TENSION)
        return false;
    if (g_sync_recent_negative_until_ms == 0)
        return false;

    uint32_t now_ms = g_now_ms;
    // Window has expired naturally.
    if ((int32_t)(now_ms - g_sync_recent_negative_until_ms) >= 0)
        return false;

    // Release damping 300 ms before the window expires to avoid a sharp step.
    uint32_t window_start_ms = g_sync_recent_negative_until_ms - SYNC_RECENT_NEGATIVE_HOLD_MS;
    uint32_t elapsed_in_window = now_ms - window_start_ms;
    if (elapsed_in_window >=
        (uint32_t)(SYNC_RECENT_NEGATIVE_HOLD_MS - SYNC_RECENT_NEGATIVE_RELEASE_MARGIN_MS))
        return false;

    // If reserve error is clearly positive the buffer is already refilling —
    // stop damping so the correction can run at full strength.
    if (sync_reserve_error_mm() < -buf_virtual_deadband_mm() * HALF_F)
        return false;

    return true;
}

bool sync_is_tension_predicted(void) {
    return !sync_is_positive_relaunch_damped() && predict_tension_coming();
}

uint32_t sync_tension_dwell_ms(uint32_t now_ms) {
    if (g_sync_tension_pin_since_ms == 0 || g_buf.state != BUF_TENSION)
        return 0;
    return now_ms - g_sync_tension_pin_since_ms;
}

/* Phase 13 Plan 02 Task 2 (D-11): TM: telemetry accessor. Reports what the
   TRIP sees -- 0 while unarmed or while a deliberate hold is in progress,
   otherwise the accumulated tension travel g_sync_refill_effort_mm holds --
   not the raw accumulator, which is already on the wire as
   SYNC_REFILL_MM:. Exposed here rather than reaching into sync.c internals
   from the status formatter. */
float sync_tension_stop_trip_mm(void) {
    if (!g_sync_trip_armed || sync_type_p_hold_in_progress()) {
        return 0.0f;
    }
    return g_sync_refill_effort_mm;
}

/* Phase 13 Plan 03 (D-19): PR: telemetry accessor -- exposes the current
   probe state (0/1/2/3, latched for the pinned episode) to
   protocol_status.c without reaching into sync.c internals. */
int sync_type_p_probe_state(void) {
    return (int)g_sync_probe_state;
}

/* Phase 13 Plan 03 Task 2 (D-17): forces a probe window to start immediately
   for bench use. Arms the episode (a forced bench restart deliberately
   bypasses the normal arm-after-observed-transition requirement -- that IS
   the point of a manual override), resets the shared g_sync_refill_effort_mm
   accumulator (both the distance trip and the probe read it, so restarting
   the probe necessarily restarts the distance trip's own accumulation too --
   the correct behaviour for a deliberate bench restart, not a side effect to
   work around) and the once-per-episode latch, and re-seeds
   g_sync_probe_peak_pos to the CURRENT g_buf_pos (REVIEW-02 -- a forced
   restart that inherited the previous window's peak would report a CONSUMER
   the new window never actually observed). The caller (protocol.c) has
   already validated preconditions (type-P sensor, sync active, no
   deliberate hold in progress) before calling this. */
void sync_type_p_probe_force_start(void) {
    g_sync_refill_effort_mm = 0.0f;
    g_sync_trip_armed = true;
    g_sync_probe_state = SYNC_PROBE_RUNNING;
    g_sync_probe_decided = false;
    g_sync_probe_peak_pos = g_buf_pos;
}

uint32_t sync_est_age_ms(uint32_t now_ms) {
    if (g_extruder_est_last_update_ms == 0)
        return 0;
    return now_ms - g_extruder_est_last_update_ms;
}
