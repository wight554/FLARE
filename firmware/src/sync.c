#include "sync.h"
#include "buf_signal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hardware/adc.h"

#include "motion.h"
#include "protocol.h"
#include "toolchange.h"
#include "cutter.h"

#define HISTORY_LEN 16
#define SYNC_COMPRESSION_SOFT_WALL_MS 1200.0f
#define SYNC_COMPRESSION_HARD_WALL_MS 350.0f
#define SYNC_COMPRESSION_HARD_PUSH_MM_S 0.25f
/* Runtime-tunable (SET:/GET:RELAY_COLLAPSE_*); defaults from CONF_*.
 * Macro names kept so the use sites below are unchanged. */
#define SYNC_COMPRESSION_COLLAPSE_DELAY_MS ((uint32_t)RELAY_COLLAPSE_DELAY_MS)
#define SYNC_COMPRESSION_COLLAPSE_RAMP_MULT RELAY_COLLAPSE_RAMP_MULT
#define SYNC_COMPRESSION_COLLAPSE_CAP_MS ((uint32_t)RELAY_COLLAPSE_CAP_MS)
#define SYNC_EST_FRESH_MS 20000u
/* Type-P buffer-stabilize lag horizon. g_buf_pos is an EWMA (BUF_ANALOG_ALPHA)
 * so it trails the true buffer by ~the filter group delay (~80ms at the default
 * alpha/tick). Stopping the stabilize drive only when raw reads NEUTRAL lets
 * BUF_STAB_SPS shove filament a lag-horizon past goal — a forward recovery from
 * the saturated TENSION rail overshoots into COMPRESSION and rams the extruder
 * gears during tip forming. Predict one horizon ahead with the filtered PSF
 * velocity and stop early; erring tension-side is the safe direction. */
#define SYNC_STAB_PREDICT_LEAD_S 0.10f
#define SYNC_NEUTRAL_COMPRESSION_TAPER_FRAC 0.5f
#define SYNC_NEUTRAL_COMPRESSION_FLOOR_FRAC 0.45f
#define SYNC_NEUTRAL_ANTI_TENSION_STALE_MS 1500u
#define SYNC_NEUTRAL_ANTI_TENSION_FLOOR_FRAC 0.70f
#define SYNC_NEUTRAL_ANTI_TENSION_CONF 0.98f
#define SYNC_RESERVE_CENTER_GUARD_FRAC 0.05f
/* H1: cap the bias contribution to the reserve *position* target. A deep
 * parked target (the old uncapped bias*threshold, up to ~5.5mm) sits the
 * buffer on the compression fault edge AND starves the switch-crossing
 * estimator (no crossings while pinned). Cap it shallow/holdable; the
 * never-TENSION compression lean is carried by H2 (a feed-rate trim) instead.
 * Primary on-hardware tuning knob. */
#define SYNC_RESERVE_BIAS_POS_FRAC_CAP 0.10f
/* H2: gentle compression feed trim (never-TENSION lean without deep parking).
 * Tight cap (~150mm/min equiv) so it can never starve vs the reserve
 * P-authority; only applied NEUTRAL and only on the tension side of target. */
#define SYNC_COMPRESSION_FEED_TRIM_MAX_SPS 120
/* Type-D Sync-Feedback Sensor control (BUF_SENSOR_TYPE == 0, D=0). The buffer
 * has only COMPRESSION and TENSION microswitches: no analog position, no
 * extruder feedback. A continuous PI controller on a dead-reckoned position
 * limit-cycles by construction (no feedback between crossings). The correct
 * controller is the type-D two-level / hysteretic relay law matched to the
 * switches.
 * FLARE polarity (per the rest of the system: RT is negative/compression,
 * REFILL effort fires in TENSION, RELIEVE in COMPRESSION): TENSION = buffer
 * EMPTY (starved) -> feed fast to refill; COMPRESSION = buffer FULL (reserve)
 * -> feed slow so the extruder draws it down; NEUTRAL -> match estimated
 * demand so the buffer rests in-band, with switches as guardrails. These
 * fracs are the primary on-hardware type-D relay-law tuning knobs.
 * RELAY_CATCHUP_FRAC scales the fixed baseline-anchored refill (TENSION/empty).
 * RELAY_NEUTRAL_FRAC scales the demand-tracking (EST) NEUTRAL feed:
 * 1.0 = match extruder (long dwell), >1 = full/COMPRESSION-reserve lean. COMPRESSION/full
 * feed is fixed at SYNC_MIN (stop) so the buffer drains off the wall. */
#define ENDSTOP_PER_UNIT_SIGMA_MM 0.025f
#define SYNC_HIGH_FLOW_NEG_ASSIST_START_MM_MIN 1000.0f
#define SYNC_HIGH_FLOW_NEG_ASSIST_FULL_MM_MIN 1400.0f
#define SYNC_HIGH_FLOW_NEG_ASSIST_FRAC 0.75f
#define SYNC_RECENT_NEGATIVE_HOLD_MS 900u
#define SYNC_POSITIVE_RELAUNCH_DAMP_NUM 1
#define SYNC_POSITIVE_RELAUNCH_DAMP_DEN 4
#define SYNC_RELAY_TRIM_LEAK_DELAY_MS 500u
#define SYNC_RELAY_TRIM_LEAK_RATE_FRAC 0.25f
#define SYNC_DRAIN_EST_ALPHA_DWELL_MS 1000.0f
#define SYNC_NEUTRAL_FILL_EST_ALPHA_DWELL_MS 2000.0f
#define SYNC_NEUTRAL_FILL_MIN_DWELL_MS 250u
#define SYNC_NEUTRAL_FILL_MIN_FEED_SAMPLES 2u
#define SYNC_NEUTRAL_FILL_OVERRUN_FRAC 0.25f
#define SYNC_DRAIN_EST_MIN_DWELL_MS 150u
sync_state_t g_sync_state = SYNC_OFF;
bool sync_auto_started = false;
bool sync_tail_assist_active = false;
uint32_t sync_idle_since_ms = 0;
int sync_current_sps = 0;
/* Type-P distance-EMA filter state for the smoothed feed target (see sync_tick). */
static float g_psf_target_filt = 0.0f;
int g_baseline_target_sps = CONF_BASELINE_SPS;
int g_baseline_sps = CONF_BASELINE_SPS;
float g_baseline_alpha = FLARE_INT_BASELINE_ALPHA;
static const flow_schedule_point_t g_flow_sched_config[CONF_FLOW_SCHED_CAP] = CONF_FLOW_SCHED;
static flow_schedule_point_t g_flow_sched_runtime[CONF_FLOW_SCHED_CAP] = CONF_FLOW_SCHED;
static int g_flow_sched_live_delta[CONF_FLOW_SCHED_CAP] = {0};
static int g_flow_sched_len = CONF_FLOW_SCHED_LEN;
uint32_t sync_fast_brake_until_ms = 0;
static bool sync_compression_recovery_active = false;
static uint32_t sync_continuous_compression_since_ms = 0;
static uint32_t sync_post_compression_boost_until_ms = 0;
static uint32_t sync_recent_negative_until_ms = 0;
static uint32_t sync_tension_pin_since_ms = 0;

/* Integral centering and sigma confidence state */
static float sync_reserve_integral_mm = 0.0f;
static float g_buf_pos_sigma_accum_mm = 0.0f;
static float g_buf_sigma_mm = 0.0f;
static uint32_t g_buf_est_low_cf_emit_ms = 0;
static uint32_t g_buf_tension_dwell_warn_emit_ms = 0;
static int g_neutral_creep_sps = 0;
static uint32_t g_neutral_creep_last_tension_ms = 0;
static bool g_buf_est_fallback_emitted = false;

float g_sync_refill_effort_mm = 0.0f;
float g_sync_relieve_effort_mm = 0.0f;
static bool g_sync_cannot_refill_warned = false;
static bool g_sync_cannot_relieve_warned = false;

/* Residual drift observer */
#define TENSION_PIN_WINDOW_LEN 16
static float g_bp_residual_last_mm = 0.0f;
static float g_bp_drift_ewma_mm = 0.0f;
static uint16_t g_bp_drift_samples = 0;
static uint32_t g_bp_drift_last_ms = 0;
static float g_bp_drift_correction_applied_mm = 0.0f;
static uint32_t g_tension_pin_ts[TENSION_PIN_WINDOW_LEN] = {0};
static int g_tension_pin_ts_idx = 0;
static uint32_t g_tension_risk_emit_ms = 0;
static float g_relay_flip_travel_since_mm = 0.0f;
static float g_relay_neutral_trim_sps = 0.0f;
static uint32_t g_relay_trim_last_leak_ms = 0;
static uint32_t g_type_d_neutral_feed_sps_sum = 0;
static uint16_t g_type_d_neutral_feed_samples = 0;
static uint32_t g_type_d_neutral_flat_feed_sps_sum = 0;
static uint16_t g_type_d_neutral_flat_feed_samples = 0;

/* buf_signal_t — canonical signal produced by the active sensor each tick. */
buf_signal_t g_buf_signal = {0};
static float g_buf_confidence = 1.0f;
static uint32_t g_buf_last_transition_ms = 0;
/* Analog: track how long the signal has been saturated (at a rail) */
static uint32_t g_buf_analog_saturated_since_ms = 0;
static uint32_t g_buf_analog_last_sample_ms = 0;

buf_tracker_t g_buf = { .state = BUF_NEUTRAL };

uint32_t sync_last_tick_ms = 0;
uint32_t sync_last_evt_ms = 0;
float extruder_est_sps = 0.0f;
float extruder_est_prev_sps = 0.0f;
uint32_t extruder_est_last_update_ms = 0;
uint32_t last_slope_update_ms = 0;

char g_marker_tag[32] = {0};
uint16_t g_marker_seq = 0;

/* Buffer-lock (BL) lifecycle sub-states — active while g_sync_state == SYNC_RETRACT_ASSIST */
typedef enum {
    BL_IDLE = 0,
    BL_PRIME,   /* driving lane toward armed extreme at SYNC_MAX_SPS */
    BL_LOCKED,  /* holding at extreme; motor energized, zero net feed */
    BL_FOLLOW,  /* event-triggered follow-on retract concurrent with extruder */
} bl_sub_state_t;

static bl_sub_state_t g_bl_sub_state = BL_IDLE;
static buf_state_t    g_bl_target_state = BUF_TENSION;
/* BL-implied goal override (type-P): a BL:T arm parks the buffer goal at max
 * tension (room for a retract's backflow), BL:C at max compression (feed
 * reserve). Persists past the BL move's mechanical completion until a BS or BL
 * timeout resets it (both route through buffer_stabilize_request). BUF_NEUTRAL
 * = no override (use the configured BUF_GOAL). */
static buf_state_t    g_bl_goal_override = BUF_NEUTRAL;
static uint32_t       g_bl_prime_start_ms = 0;   /* when prime search began */
static float          g_bl_prime_mm_per_s = 0.0f; /* stab speed in mm/s */
static float          g_bl_prime_cap_mm   = 0.0f; /* abs 1: outer safety cap = BUF_MAX_TRAVEL_MM */
static bool           g_bl_prime_switch_hit    = false; /* switch fired during search phase */
static uint32_t       g_bl_prime_post_start_ms = 0;    /* when post-click settle began */
static float          g_bl_prime_post_cap_mm   = 0.0f; /* abs 2: post-click extra travel = (max-span)/2 */
static float          g_bl_follow_mm        = 0.0f;    /* armed follow-on distance; 0 = disabled */
static float          g_bl_follow_rate_mmpm = 0.0f;    /* armed follow-on rate (mm/min) */
static uint32_t       g_bl_follow_start_ms  = 0;       /* when FOLLOW motion began */
static float          g_bl_follow_mm_per_s  = 0.0f;    /* FOLLOW commanded speed in mm/s */
static uint32_t       g_bl_watchdog_ms = 0;
static float          g_bl_follow_traveled_mm = 0.0f;
static uint32_t       g_bl_last_tick_ms = 0;
static int            g_bl_follow_cur_sps = 0;       /* FOLLOW current ramped rate */
static int            g_bl_follow_target_sps = 0;    /* FOLLOW target rate (clamped) */
static uint32_t       g_bl_follow_ramp_tick_ms = 0;  /* last FOLLOW ramp step */
#define BL_WATCHDOG_DEFAULT_MS 30000u
/* Suppress sync auto-start on BUF_TENSION after a BL abort (ST path).
 * The buffer is at tension because BL put it there, not because the
 * extruder is pulling. Cleared when the buffer physically departs tension
 * or BL is re-armed. */
static bool g_bl_autostart_suppressed = false;
static bool g_sync_tension_transitioned = false;

float g_buf_pos = 0.0f;
static float g_buf_pos_prev = 0.0f;
static float g_vel_norm = 0.0f;
static float g_vel_norm_f = 0.0f;
float g_buf_pos_raw_status = 0.0f;

bool g_boot_stabilizing = false;
uint32_t g_boot_stabilize_deadline_ms = 0;
lane_t *g_boot_stabilize_lane = NULL;
bool g_boot_stabilize_forward = false;
static bool g_buffer_stabilize_emit_events = false;
static uint32_t g_boot_stabilize_started_ms = 0;
static float g_boot_stabilize_start_pos = 0.0f;

typedef enum {
    BUFFER_SERVICE_STABILIZE = 0,
    BUFFER_SERVICE_NEG_SYNC,
} buffer_service_mode_t;

static buffer_service_mode_t g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;

static int sync_bootstrap_sps(void);
static uint32_t g_idle_compression_since_ms = 0;

typedef struct {
    buf_state_t zone;
    uint32_t dwell_ms;
} zone_event_t;

static zone_event_t g_history[HISTORY_LEN] = {0};
static int g_hist_idx = 0;
static uint32_t buf_pos_last_ms = 0;
static buf_state_t g_buf_stable_state = BUF_NEUTRAL;
static buf_state_t g_buf_pending_state = BUF_NEUTRAL;
static uint32_t g_buf_pending_since_ms = 0;
static float g_buf_physical_entry_pos_mm = 0.0f;

static int lane_motion_sps(lane_t *L);
static int baseline_control_floor_sps(void);
static void buf_update(buf_state_t new_state, uint32_t now_ms);

static void relay_on_transition(buf_state_t new_state, uint32_t now_ms) {
    (void)new_state;
    (void)now_ms;
    if (BUF_SENSOR_TYPE != 0) return;
    g_relay_flip_travel_since_mm = 0.0f;
}

static int flow_sched_len_clamped(void) {
    if (g_flow_sched_len < 1) return 1;
    if (g_flow_sched_len > CONF_FLOW_SCHED_CAP) return CONF_FLOW_SCHED_CAP;
    return g_flow_sched_len;
}

void flow_schedule_refresh_scalar(void) {
    g_flow_sched_len = 1;
    g_flow_sched_runtime[0].flow_sps = g_baseline_target_sps;
    g_flow_sched_runtime[0].baseline_sps = g_baseline_target_sps;
    g_flow_sched_runtime[0].bias_milli = clamp_i((int)(SYNC_COMPRESSION_BIAS_FRAC * 1000.0f + 0.5f), 0, 700);
    for (int i = 0; i < CONF_FLOW_SCHED_CAP; i++) {
        g_flow_sched_live_delta[i] = 0;
    }
}

void flow_schedule_reset_runtime(void) {
    g_flow_sched_len = CONF_FLOW_SCHED_LEN;
    if (g_flow_sched_len < 1) g_flow_sched_len = 1;
    if (g_flow_sched_len > CONF_FLOW_SCHED_CAP) g_flow_sched_len = CONF_FLOW_SCHED_CAP;

    for (int i = 0; i < g_flow_sched_len; i++) {
        g_flow_sched_runtime[i] = g_flow_sched_config[i];
    }
    for (int i = 0; i < CONF_FLOW_SCHED_CAP; i++) {
        g_flow_sched_live_delta[i] = 0;
    }

    if (CONF_FLOW_SCHED_LEN <= 1) {
        flow_schedule_refresh_scalar();
    }
}

static int lerp_i(int a, int b, int x, int x0, int x1) {
    int span = x1 - x0;
    if (span <= 0) return a;
    return a + (int)(((int64_t)(b - a) * (int64_t)(x - x0)) / (int64_t)span);
}

static int flow_active_segment(int flow_sps) {
    int len = flow_sched_len_clamped();
    flow_schedule_point_t *sched = g_flow_sched_runtime;
    if (len <= 1 || flow_sps <= sched[0].flow_sps) return 0;

    int last = len - 1;
    if (flow_sps >= sched[last].flow_sps) return last;

    for (int i = 0; i < last; i++) {
        if (flow_sps <= sched[i + 1].flow_sps) return i;
    }
    return last;
}

flow_param_t flow_param(int flow_sps) {
    int len = flow_sched_len_clamped();
    flow_schedule_point_t *sched = g_flow_sched_runtime;
    int segment = flow_active_segment(flow_sps);
    flow_param_t p;

    if (len <= 1 || flow_sps <= sched[0].flow_sps) {
        p = (flow_param_t){ sched[0].baseline_sps, sched[0].bias_milli };
        p.baseline_sps += g_flow_sched_live_delta[segment];
        return p;
    }

    int last = len - 1;
    if (flow_sps >= sched[last].flow_sps) {
        p = (flow_param_t){ sched[last].baseline_sps, sched[last].bias_milli };
        p.baseline_sps += g_flow_sched_live_delta[segment];
        return p;
    }

    for (int i = 0; i < last; i++) {
        int x0 = sched[i].flow_sps;
        int x1 = sched[i + 1].flow_sps;
        if (flow_sps >= x0 && flow_sps <= x1) {
            p.baseline_sps = lerp_i(sched[i].baseline_sps, sched[i + 1].baseline_sps, flow_sps, x0, x1);
            p.bias_milli = lerp_i(sched[i].bias_milli, sched[i + 1].bias_milli, flow_sps, x0, x1);
            p.baseline_sps += g_flow_sched_live_delta[segment];
            return p;
        }
    }

    p = (flow_param_t){ sched[last].baseline_sps, sched[last].bias_milli };
    p.baseline_sps += g_flow_sched_live_delta[segment];
    return p;
}

static int lane_motion_sps(lane_t *L) {
    if (!L) return 0;
    if (L->current_sps > 0) return L->current_sps;
    if (g_tc_ctx.state == TC_RELOAD_FOLLOW && g_tc_ctx.reload_current_sps > 0)
        return g_tc_ctx.reload_current_sps;
    return sync_current_sps;
}

static lane_t *pick_boot_stabilize_lane(void) {
    lane_t *stab_lane = lane_ptr(active_lane);
    if (stab_lane) return stab_lane;
    if (lane_out_present(&g_lane_l1) && !lane_out_present(&g_lane_l2)) return &g_lane_l1;
    if (lane_out_present(&g_lane_l2) && !lane_out_present(&g_lane_l1)) return &g_lane_l2;
    return &g_lane_l1;
}

static float buf_physical_half_travel_mm(void) {
    float physical_half = (BUF_MAX_TRAVEL_MM > 0) ? ((float)BUF_MAX_TRAVEL_MM * 0.5f) : BUF_SWITCH_SPAN_HALF_MM;
    if (physical_half < 1.0f) physical_half = 1.0f;
    if (physical_half < BUF_SWITCH_SPAN_HALF_MM) physical_half = BUF_SWITCH_SPAN_HALF_MM;
    return physical_half;
}

static float buf_threshold_mm(void) {
    float physical_half = buf_physical_half_travel_mm();
    float threshold = BUF_SWITCH_SPAN_HALF_MM;
    if (threshold < 1.0f) threshold = 1.0f;
    if (threshold > physical_half) threshold = physical_half;
    return threshold;
}

static float buf_target_reserve_mm(void) {
    float threshold = buf_threshold_mm();
    float pct = (float)SYNC_RESERVE_PCT / 100.0f;
    if (BUF_SENSOR_TYPE == 0) {
        return clamp_f(threshold * pct, 0.0f, threshold * 0.7f);
    }

    float physical_half = buf_physical_half_travel_mm();
    flow_param_t fp = flow_param((int)extruder_est_sps);
    float schedule_bias = (float)fp.bias_milli / 1000.0f;
    float bias = clamp_f(fmaxf(SYNC_COMPRESSION_BIAS_FRAC, schedule_bias), 0.0f, 0.7f);
    float target = (threshold * pct);
    float center_guard_mm = threshold * SYNC_RESERVE_CENTER_GUARD_FRAC;

    if (pct > 0.0f) target += center_guard_mm;

    /* H1: holdable target. Cap the bias position contribution so the
     * buffer parks off the fault wall with room for frequent switch
     * crossings (keeps the estimator fresh). Remaining compression lean is
     * applied as a feed trim (H2), not parked depth. */
    float bias_pos = fminf(bias, SYNC_RESERVE_BIAS_POS_FRAC_CAP);
    target += bias_pos * threshold;

    float max_target = physical_half - 0.5f;
    if (target > max_target) target = max_target;
    if (target < -threshold) target = -threshold;
    return target;
}

static float buf_virtual_deadband_mm(void) {
    float deadband = buf_threshold_mm() * 0.15f;
    if (deadband < 0.5f) deadband = 0.5f;
    if (deadband > 2.0f) deadband = 2.0f;
    return deadband;
}

static void buf_anchor_virtual_position(buf_state_t old_state, buf_state_t new_state) {
    if (BUF_SENSOR_TYPE != 0) return;

    float threshold = buf_threshold_mm();
    buf_state_t anchor_state = new_state;
    if (new_state == BUF_NEUTRAL) anchor_state = old_state;

    if (anchor_state == BUF_TENSION) g_buf_pos = -threshold;
    else if (anchor_state == BUF_COMPRESSION) g_buf_pos = threshold;
    else g_buf_pos = 0.0f;
}

static void buf_virtual_position_tick(lane_t *A, uint32_t elapsed_ms) {
    if (BUF_SENSOR_TYPE != 0 || elapsed_ms == 0) return;

    float threshold = buf_threshold_mm();
    float physical_half = buf_physical_half_travel_mm();
    if (!A) {
        g_buf_pos = clamp_f(g_buf_pos, -physical_half, physical_half);
        return;
    }

    int idx = lane_to_idx(A->lane_id);
    if (idx < 0 || idx >= NUM_LANES) idx = 0;

    bool tracking_motion = sync_enabled || g_tc_ctx.state == TC_RELOAD_APPROACH ||
                           g_tc_ctx.state == TC_RELOAD_FOLLOW ||
                           (A->task == TASK_FEED && A->fault == FAULT_NONE);
    if (tracking_motion) {
        float dt_s = (float)elapsed_ms / 1000.0f;
        float mmu_mm_s = (float)lane_motion_sps(A) * MM_PER_STEP[idx];
        float extruder_mm_s = extruder_est_sps * MM_PER_STEP[idx];
        float net_delta = (mmu_mm_s - extruder_mm_s) * dt_s;
        g_buf_pos += net_delta;
        if (g_buf.state == BUF_NEUTRAL) {
            g_buf_pos_sigma_accum_mm += fabsf(net_delta);
        }
    }

    g_buf_pos = clamp_f(g_buf_pos, -physical_half, physical_half);
    if (g_buf.state == BUF_TENSION && g_buf_pos > -threshold) g_buf_pos = -threshold;
    else if (g_buf.state == BUF_COMPRESSION && g_buf_pos < threshold) g_buf_pos = threshold;
    else if (g_buf.state == BUF_NEUTRAL) g_buf_pos = clamp_f(g_buf_pos, -threshold, threshold);
}

static float lane_motion_mm_s(lane_t *L) {
    if (!L) return 0.0f;
    int idx = lane_to_idx(L->lane_id);
    if (idx < 0 || idx >= NUM_LANES) idx = 0;
    return (float)lane_motion_sps(L) * MM_PER_STEP[idx];
}

static float extruder_motion_mm_s(lane_t *L) {
    if (!L) return 0.0f;
    int idx = lane_to_idx(L->lane_id);
    if (idx < 0 || idx >= NUM_LANES) idx = 0;
    return extruder_est_sps * MM_PER_STEP[idx];
}

float sync_compression_wall_velocity_mm_s(lane_t *L) {
    if (!L || g_buf_signal.kind != BUF_SRC_VIRTUAL_ENDSTOP) return 0.0f;
    float toward_compression = lane_motion_mm_s(L) - extruder_motion_mm_s(L);
    return toward_compression > 0.0f ? toward_compression : 0.0f;
}

static float sync_compression_wall_remaining_mm(void) {
    if (g_buf_signal.kind != BUF_SRC_VIRTUAL_ENDSTOP) return 0.0f;
    float remaining = buf_physical_half_travel_mm() - g_buf_pos;
    if (remaining < 0.0f) remaining = 0.0f;
    return remaining;
}

static int sync_compression_floor_sps(void) {
    return (SYNC_MIN_SPS > COMPRESSION_SPS) ? SYNC_MIN_SPS : COMPRESSION_SPS;
}

float sync_compression_wall_time_ms(lane_t *L) {
    float toward_compression = sync_compression_wall_velocity_mm_s(L);
    if (!L || g_buf_signal.kind != BUF_SRC_VIRTUAL_ENDSTOP || toward_compression < 0.05f) return 1000000000.0f;
    return (sync_compression_wall_remaining_mm() / toward_compression) * 1000.0f;
}

static void neutral_creep_update(buf_state_t s, lane_t *A, uint32_t now_ms) {
    /* D5/relay-buffer-control-2switch 7.2-A: keep neutral_creep as
     * intended-inert telemetry; fallback relay control ignores it. */
    if (g_sync_state != SYNC_ACTIVE || NEUTRAL_CREEP_TIMEOUT_MS == 0 || NEUTRAL_CREEP_RATE_SPS_PER_S == 0) {
        g_neutral_creep_sps = 0;
        return;
    }
    
    // Only active in NEUTRAL.
    if (s == BUF_TENSION || s == BUF_COMPRESSION) return;
    
    // Seed on boot/start if we missed a TENSION edge.
    if (g_neutral_creep_last_tension_ms == 0) {
        g_neutral_creep_last_tension_ms = now_ms;
    }

    uint32_t dwell_ms = now_ms - g_neutral_creep_last_tension_ms;
    if (dwell_ms > (uint32_t)NEUTRAL_CREEP_TIMEOUT_MS) {
        uint32_t active_ms = dwell_ms - NEUTRAL_CREEP_TIMEOUT_MS;
        float active_s = (float)active_ms / 1000.0f;
        
        int max_creep = (int)((extruder_est_sps * (float)NEUTRAL_CREEP_CAP_FRAC) / 100.0f);
        int raw_creep = (int)(active_s * (float)NEUTRAL_CREEP_RATE_SPS_PER_S);
        
        if (raw_creep >= max_creep) {
            g_neutral_creep_sps = max_creep;
            static uint32_t cap_warn_ms = 0;
            if (now_ms - cap_warn_ms >= 5000) {
                cap_warn_ms = now_ms;
                cmd_event("SYNC", "NEUTRAL_CREEP_CAP");
            }
        } else {
            g_neutral_creep_sps = raw_creep;
        }
    } else {
        g_neutral_creep_sps = 0;
    }
}

static int sync_apply_scaling(int base_sps, float target_norm, float pos_norm) {
    int target = base_sps;

    float deadband_norm = (BUF_SENSOR_TYPE == 1)
        ? 0.1f
        : (buf_virtual_deadband_mm() / buf_threshold_mm());

    /* Overfeed taper: as the buffer pushes past goal toward the COMPRESSION rail
       (+1 under the new convention), taper feed down toward the compression floor
       so it doesn't slam the wall. Fires on the compression side (pos > goal). */
    if (pos_norm > (target_norm + deadband_norm)) {
        float taper_start = target_norm + deadband_norm;
        float taper_end = 1.0f;
        float taper_span = taper_end - taper_start;

        if (target > COMPRESSION_SPS && taper_span > 0.001f) {
            float overfill = pos_norm - taper_start;
            float taper_frac = clamp_f(overfill / taper_span, 0.0f, 1.0f);
            int taper_floor_sps = COMPRESSION_SPS;
            if (g_buf.state == BUF_NEUTRAL) {
                taper_frac *= SYNC_NEUTRAL_COMPRESSION_TAPER_FRAC;
                int dynamic_neutral_floor = (int)(extruder_est_sps * SYNC_NEUTRAL_COMPRESSION_FLOOR_FRAC);
                if (dynamic_neutral_floor > taper_floor_sps) taper_floor_sps = dynamic_neutral_floor;
            }
            float tapered = (float)target - ((float)(target - COMPRESSION_SPS) * taper_frac);
            if (tapered < (float)taper_floor_sps) tapered = (float)taper_floor_sps;
            target = (int)tapered;
        }
    }

    return target;
}

const char *buf_state_name(buf_state_t s) {
    switch (s) {
        case BUF_NEUTRAL: return "NEUTRAL";
        case BUF_TENSION: return "TENSION";
        case BUF_COMPRESSION: return "COMPRESSION";
        case BUF_FAULT: return "FAULT";
        default: return "?";
    }
}

static void history_push(buf_state_t zone, uint32_t dwell_ms) {
    g_history[g_hist_idx].zone = zone;
    g_history[g_hist_idx].dwell_ms = dwell_ms;
    g_hist_idx = (g_hist_idx + 1) % HISTORY_LEN;
}

static bool predict_tension_coming(void) {
    int neutral_count = 0;
    int short_count = 0;

    for (int i = 0; i < HISTORY_LEN; i++) {
        if (g_history[i].zone == BUF_NEUTRAL && g_history[i].dwell_ms > 0) {
            neutral_count++;
            if (g_history[i].dwell_ms < (uint32_t)BUF_PREDICT_THR_MS) {
                short_count++;
            }
        }
    }
    return neutral_count > 0 && (short_count * 2 >= neutral_count);
}

void buf_analog_update(void) {
    adc_select_input(PIN_PSF - 26);
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) sum += adc_read();
    float fraction = (float)(sum >> 2) / 4095.0f;
    g_buf_pos_raw_status = fraction;

    bool reversed = (BUF_PSF_MAX_COMP < BUF_PSF_MAX_TENS);
    float neutral = BUF_PSF_NEUTRAL;
    float norm = 0.0f;

    if (reversed) {
        float d_comp = neutral - BUF_PSF_MAX_COMP;
        float d_tens = BUF_PSF_MAX_TENS - neutral;
        if (d_comp < 0.001f) d_comp = 0.001f;
        if (d_tens < 0.001f) d_tens = 0.001f;
        norm = (fraction <= neutral)
             ? -(neutral - fraction) / d_comp
             :  (fraction - neutral) / d_tens;
    } else {
        float d_comp = BUF_PSF_MAX_COMP - neutral;
        float d_tens = neutral - BUF_PSF_MAX_TENS;
        if (d_comp < 0.001f) d_comp = 0.001f;
        if (d_tens < 0.001f) d_tens = 0.001f;
        norm = (fraction >= neutral)
             ? -(fraction - neutral) / d_comp
             :  (neutral - fraction) / d_tens;
    }
    norm = clamp_f(-norm, -1.0f, 1.0f);

    g_buf_pos = BUF_ANALOG_ALPHA * norm + (1.0f - BUF_ANALOG_ALPHA) * g_buf_pos;

    float dt_s = (float)SYNC_TICK_MS / 1000.0f;
    if (dt_s < 0.001f) dt_s = 0.001f;
    float vel = (g_buf_pos - g_buf_pos_prev) / dt_s;
    g_vel_norm = vel;

    float vel_alpha = CONF_PSF_VEL_ALPHA;
    g_vel_norm_f = vel_alpha * vel + (1.0f - vel_alpha) * g_vel_norm_f;
    g_buf_pos_prev = g_buf_pos;
}

static float psf_goal_norm(void) {
    /* BL-implied override: while a buffer-lock is in effect (until BS/timeout),
       park the goal at the armed rail so the idle stabilize and PD both pull the
       buffer to the side that leaves room for the locked operation. */
    if (g_bl_goal_override == BUF_TENSION) return -1.0f;
    if (g_bl_goal_override == BUF_COMPRESSION) return 1.0f;

    float goal_norm = 0.0f;
    bool reversed = (BUF_PSF_MAX_COMP < BUF_PSF_MAX_TENS);
    float goal_raw = BUF_GOAL;

    if (reversed) {
        float d_comp = BUF_PSF_NEUTRAL - BUF_PSF_MAX_COMP;
        float d_tens = BUF_PSF_MAX_TENS - BUF_PSF_NEUTRAL;
        if (d_comp < 0.001f) d_comp = 0.001f;
        if (d_tens < 0.001f) d_tens = 0.001f;
        goal_norm = (goal_raw <= BUF_PSF_NEUTRAL)
                  ? -(BUF_PSF_NEUTRAL - goal_raw) / d_comp
                  :  (goal_raw - BUF_PSF_NEUTRAL) / d_tens;
    } else {
        float d_comp = BUF_PSF_MAX_COMP - BUF_PSF_NEUTRAL;
        float d_tens = BUF_PSF_NEUTRAL - BUF_PSF_MAX_TENS;
        if (d_comp < 0.001f) d_comp = 0.001f;
        if (d_tens < 0.001f) d_tens = 0.001f;
        goal_norm = (goal_raw >= BUF_PSF_NEUTRAL)
                  ? -(goal_raw - BUF_PSF_NEUTRAL) / d_comp
                  :  (BUF_PSF_NEUTRAL - goal_raw) / d_tens;
    }
    return clamp_f(-goal_norm, -1.0f, 1.0f);
}

static float buf_pos_norm(void) {
    if (BUF_SENSOR_TYPE == 1) {
        return g_buf_pos;
    } else {
        float thr = buf_threshold_mm();
        if (thr < 0.001f) thr = 0.001f;
        return clamp_f(g_buf_pos / thr, -1.0f, 1.0f);
    }
}

static float buf_target_norm(void) {
    if (BUF_SENSOR_TYPE == 1) {
        return psf_goal_norm();
    } else {
        float thr = buf_threshold_mm();
        if (thr < 0.001f) thr = 0.001f;
        return clamp_f(buf_target_reserve_mm() / thr, -1.0f, 1.0f);
    }
}

buf_state_t buf_state_raw(void) {
    if (BUF_SENSOR_TYPE == 1) {
        float goal_norm = psf_goal_norm();
        const float deadband = 0.1f;
        if (g_buf_pos < goal_norm - deadband) return BUF_TENSION;
        if (g_buf_pos > goal_norm + deadband) return BUF_COMPRESSION;
        return BUF_NEUTRAL;
    }

    bool tension = on_al(&g_buf_tension_din);
    bool compression = on_al(&g_buf_compression_din);

    if (tension && compression) return BUF_FAULT;
    if (tension) return BUF_TENSION;
    if (compression) return BUF_COMPRESSION;
    return BUF_NEUTRAL;
}

static buf_state_t buf_read_stable(uint32_t now_ms) {
    buf_state_t raw = buf_state_raw();
    if (raw == g_buf_stable_state) {
        g_buf_pending_since_ms = 0;
        return g_buf_stable_state;
    }

    if (raw != g_buf_pending_state) {
        g_buf_pending_state = raw;
        g_buf_pending_since_ms = now_ms;
        return g_buf_stable_state;
    }

    if ((now_ms - g_buf_pending_since_ms) >= (uint32_t)BUF_HYST_MS) {
        /* G2(b): never gate the egress flip from a zero-feed state.
         * Type-D COMPRESSION commands feed rate of 0, so no motor travel
         * accrues there; gating the flip OUT of COMPRESSION on
         * g_relay_flip_travel_since_mm deadlocks the relay (cannot leave
         * a stopped state because leaving it is what produces the travel
         * the guard demands). The distance hysteresis applies only to
         * actuator-moving transitions (NEUTRAL<->TENSION). */
        if (BUF_SENSOR_TYPE == 0 && RELAY_MIN_FLIP_MM > 0.0f &&
            raw != BUF_FAULT && g_buf_stable_state != BUF_COMPRESSION &&
            g_relay_flip_travel_since_mm < RELAY_MIN_FLIP_MM) {
            return g_buf_stable_state;
        }
        g_buf_stable_state = g_buf_pending_state;
        g_buf_pending_since_ms = 0;
    }
    return g_buf_stable_state;
}

static void buf_force_stable_state(buf_state_t state, uint32_t now_ms) {
    g_buf_stable_state = state;
    g_buf_pending_state = state;
    g_buf_pending_since_ms = 0;

    if (g_buf.state != state) {
        buf_update(state, now_ms);
    }

    g_buf.entered_ms = now_ms;
    if (state == BUF_NEUTRAL) {
        g_buf_pos = 0.0f;
        g_buf_physical_entry_pos_mm = 0.0f;
        g_buf.arm_vel_mm_s = 0.0f;
        if (!sync_enabled) {
            extruder_est_sps = 0.0f;
            extruder_est_prev_sps = 0.0f;
            extruder_est_last_update_ms = now_ms;
        }
    }
}

void sync_init(uint32_t now_ms) {
    buf_state_t raw = buf_state_raw();
    g_buf_stable_state = raw;
    g_buf_pending_state = raw;
    g_buf_pending_since_ms = 0;
    g_buf.state = raw;
    g_buf.entered_ms = now_ms;
    
    g_sync_tension_transitioned = false; /* Never false-trigger tension auto-start on boot! */
    g_sync_state = SYNC_OFF;
    sync_auto_started = false;
    sync_tail_assist_active = false;
    sync_current_sps = 0;
    g_buf_pos_prev = g_buf_pos;
    g_vel_norm = 0.0f;
    g_vel_norm_f = 0.0f;
}

static void boot_stabilize_stop(void) {
    if (g_boot_stabilize_lane) {
        motor_stop(&g_boot_stabilize_lane->m);
    }
    g_boot_stabilizing = false;
    g_boot_stabilize_deadline_ms = 0;
    g_boot_stabilize_lane = NULL;
    g_buffer_stabilize_emit_events = false;
    g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
}

static void boot_stabilize_disarm(void) {
    g_boot_stabilizing = false;
    g_boot_stabilize_deadline_ms = 0;
    g_boot_stabilize_lane = NULL;
    g_buffer_stabilize_emit_events = false;
    g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
}

static bool buffer_stabilize_controller_idle(void) {
    /* TC_ERROR: TC concluded (failed), motors stopped — allow stabilize. */
    if ((g_tc_ctx.state != TC_IDLE && g_tc_ctx.state != TC_ERROR) ||
        cutter_busy() || sync_enabled) return false;
    if (g_lane_l1.task != TASK_IDLE || g_lane_l2.task != TASK_IDLE) return false;
    return true;
}

static bool buffer_negative_sync_eligible(void) {
    lane_t *active = lane_ptr(active_lane);
    return active && lane_out_present(active);
}

static bool buffer_stabilize_start_internal(uint32_t now_ms, bool emit_events, buffer_service_mode_t mode) {
    if (g_boot_stabilizing) return true;
    if (!buffer_stabilize_controller_idle()) return false;
    if (BUF_SENSOR_TYPE != 0) {
        /* D23 Gate A: type-P idle stabilize. Drive toward goal via the shared
           goal-relative path below (buf_state_raw() zones pick direction, the
           stabilize tick stops at BUF_NEUTRAL = near goal), but only when
           filament is present on a board-local sensor. Unloaded, the buffer
           rests at the tension/home rail by design, so driving the motor would
           dry-spin against the home stop. */
        lane_t *pl = pick_boot_stabilize_lane();
        if (!(lane_out_present(pl) || lane_in_present(pl))) return true;
    }
    if (mode == BUFFER_SERVICE_NEG_SYNC && sync_guard_active) return true;



    buf_state_t buf_state = buf_state_raw();
    lane_t *stab_lane = NULL;
    bool forward = false;

    if (mode == BUFFER_SERVICE_NEG_SYNC) {
        if (buf_state != BUF_COMPRESSION || !buffer_negative_sync_eligible()) return true;
        stab_lane = lane_ptr(active_lane);
        forward = false;
    } else {
        if (buf_state != BUF_COMPRESSION && buf_state != BUF_TENSION) return true;
        stab_lane = pick_boot_stabilize_lane();
        forward = (buf_state == BUF_TENSION);
    }

    if (!stab_lane || BUF_STAB_SPS <= 0) return false;

    g_boot_stabilizing = true;
    g_boot_stabilize_deadline_ms = now_ms + 10000u;
    g_boot_stabilize_lane = stab_lane;
    g_buffer_stabilize_emit_events = emit_events;
    g_buffer_service_mode = mode;
    g_idle_compression_since_ms = 0;
    g_boot_stabilize_started_ms = now_ms;
    g_boot_stabilize_start_pos = g_buf_pos;

    motor_enable(&stab_lane->m, true);
    motor_set_dir(&stab_lane->m, forward);
    motor_set_rate_sps(&stab_lane->m, BUF_STAB_SPS);
    g_boot_stabilize_forward = forward;

    if (g_buffer_stabilize_emit_events) cmd_event("BUF_STAB", "START");
    return true;
}

bool buffer_stabilize_request(uint32_t now_ms) {
    g_idle_compression_since_ms = 0;
    /* BS (and BL timeout, which routes here) ends the buffer-lock context: drop
       the BL-implied goal override back to the configured BUF_GOAL. */
    g_bl_goal_override = BUF_NEUTRAL;
    return buffer_stabilize_start_internal(now_ms, true, BUFFER_SERVICE_STABILIZE);
}

void boot_stabilize_start(uint32_t now_ms) {
    (void)buffer_stabilize_start_internal(now_ms, false, BUFFER_SERVICE_STABILIZE);
}

void buffer_stabilize_tick(uint32_t now_ms) {
    if (sync_guard_active && g_boot_stabilizing && g_buffer_service_mode == BUFFER_SERVICE_NEG_SYNC) {
        boot_stabilize_stop();
        return;
    }

    if (!g_boot_stabilizing) {
        if (!buffer_stabilize_controller_idle()) {
            g_idle_compression_since_ms = 0;
        } else if (buf_state_raw() == BUF_COMPRESSION && buffer_negative_sync_eligible()) {
            if (g_idle_compression_since_ms == 0) g_idle_compression_since_ms = now_ms;
            if (POST_PRINT_STAB_DELAY_MS <= 0 ||
                (now_ms - g_idle_compression_since_ms) >= (uint32_t)POST_PRINT_STAB_DELAY_MS) {
                (void)buffer_stabilize_start_internal(now_ms, true, BUFFER_SERVICE_NEG_SYNC);
            }
        } else {
            g_idle_compression_since_ms = 0;
        }
    }

    if (!g_boot_stabilizing) return;

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

    if (BUF_SENSOR_TYPE == 1 && g_boot_stabilizing) {
        if ((int32_t)(now_ms - g_boot_stabilize_started_ms) >= 200) {
            float change = fabsf(g_buf_pos - g_boot_stabilize_start_pos);
            if (change < 0.03f) {
                if (g_buffer_stabilize_emit_events) cmd_event("BUF_STAB", "STAGNANT_TIMEOUT");
                boot_stabilize_stop();
                return;
            }
        }
    }

    buf_state_t raw_state = buf_state_raw();

    /* Type-P lag compensation: predict the buffer one EWMA group-delay ahead
     * with the filtered PSF velocity and stop as soon as the prediction reaches
     * goal in the drive direction. Without this the lagged g_buf_pos reads
     * NEUTRAL only after BUF_STAB_SPS has already pushed past it, overshooting
     * the opposite rail (forward stabilize from TENSION -> COMPRESSION into the
     * gears). Stopping a hair early parks tension-side, which is safe. */
    if (BUF_SENSOR_TYPE == 1 && g_boot_stabilize_lane) {
        float goal = psf_goal_norm();
        float predicted = g_buf_pos + SYNC_STAB_PREDICT_LEAD_S * g_vel_norm_f;
        bool reached = g_boot_stabilize_forward ? (predicted >= goal)
                                                : (predicted <= goal);
        if (reached) {
            buf_force_stable_state(BUF_NEUTRAL, now_ms);
            if (g_buffer_stabilize_emit_events) cmd_event("BUF_STAB", "DONE");
            boot_stabilize_stop();
            return;
        }
    }

    if (g_buffer_service_mode == BUFFER_SERVICE_NEG_SYNC) {
        if (raw_state == BUF_NEUTRAL) {
            buf_force_stable_state(BUF_NEUTRAL, now_ms);
            if (g_buffer_stabilize_emit_events) cmd_event("BUF_STAB", "DONE");
            boot_stabilize_stop();
            return;
        }

        if (raw_state == BUF_TENSION) {
            g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
            g_boot_stabilize_deadline_ms = now_ms + 10000u;
            motor_enable(&g_boot_stabilize_lane->m, true);
            motor_set_dir(&g_boot_stabilize_lane->m, true);
            motor_set_rate_sps(&g_boot_stabilize_lane->m, BUF_STAB_SPS);
            g_boot_stabilize_forward = true;
            return;
        }
    } else {
        if (raw_state == BUF_NEUTRAL) {
            buf_force_stable_state(BUF_NEUTRAL, now_ms);
            if (g_buffer_stabilize_emit_events) cmd_event("BUF_STAB", "DONE");
            boot_stabilize_stop();
            return;
        }
        /* Overshoot recovery: BUF_STAB_SPS can push the buffer past
         * NEUTRAL into the opposite extreme in a single tick on a
         * light/fast buffer. Without a reversal the motor keeps
         * driving in the original direction and slams the opposite
         * mechanical end. Detect the polarity flip via motor_get_dir
         * vs the current raw state and reverse if they no longer
         * agree (still-need-forward when raw == COMPRESSION, or
         * still-need-retract when raw == TENSION). */
        bool need_forward = (raw_state == BUF_TENSION);
        if (g_boot_stabilize_forward != need_forward) {
            motor_set_dir(&g_boot_stabilize_lane->m, need_forward);
            g_boot_stabilize_forward = need_forward;
            g_boot_stabilize_deadline_ms = now_ms + 10000u;
            if (g_buffer_stabilize_emit_events) cmd_event("BUF_STAB", "REVERSE");
        }
    }

    if ((int32_t)(now_ms - g_boot_stabilize_deadline_ms) >= 0) {
        if (g_buffer_stabilize_emit_events) cmd_event("BUF_STAB", "TIMEOUT");
        boot_stabilize_stop();
    }
}

static float clamp_est_sps(float est_sps) {
    float max_est_sps = (float)GLOBAL_MAX_SPS;
    if (est_sps < 0.0f) return 0.0f;
    if (est_sps > max_est_sps) return max_est_sps;
    return est_sps;
}

static void blend_extruder_est_sps(float sample_sps, float alpha, uint32_t now_ms) {
    sample_sps = clamp_est_sps(sample_sps);
    alpha = clamp_f(alpha, EST_ALPHA_MIN, EST_ALPHA_MAX);
    extruder_est_sps = alpha * sample_sps + (1.0f - alpha) * extruder_est_sps;
    extruder_est_last_update_ms = now_ms;
}

static void type_d_neutral_feed_reset(void) {
    g_type_d_neutral_feed_sps_sum = 0;
    g_type_d_neutral_feed_samples = 0;
    g_type_d_neutral_flat_feed_sps_sum = 0;
    g_type_d_neutral_flat_feed_samples = 0;
}

static void type_d_neutral_feed_sample(buf_state_t s, float pos_norm,
                                       float target_norm, float deadband_norm) {
    if (BUF_SENSOR_TYPE != 0 || s != BUF_NEUTRAL || g_sync_state != SYNC_ACTIVE) return;
    if (sync_current_sps <= 0) return;
    if (g_type_d_neutral_feed_samples >= 10000u) {
        g_type_d_neutral_feed_sps_sum /= 2u;
        g_type_d_neutral_feed_samples /= 2u;
    }
    g_type_d_neutral_feed_sps_sum += (uint32_t)sync_current_sps;
    g_type_d_neutral_feed_samples++;

    if (pos_norm <= (target_norm + deadband_norm)) {
        if (g_type_d_neutral_flat_feed_samples >= 10000u) {
            g_type_d_neutral_flat_feed_sps_sum /= 2u;
            g_type_d_neutral_flat_feed_samples /= 2u;
        }
        g_type_d_neutral_flat_feed_sps_sum += (uint32_t)sync_current_sps;
        g_type_d_neutral_flat_feed_samples++;
    }
}

static bool type_d_neutral_feed_avg_sps(float *avg_sps) {
    if (!avg_sps || g_type_d_neutral_feed_samples == 0) return false;
    if (g_type_d_neutral_flat_feed_samples >= SYNC_NEUTRAL_FILL_MIN_FEED_SAMPLES) {
        *avg_sps = (float)g_type_d_neutral_flat_feed_sps_sum /
                   (float)g_type_d_neutral_flat_feed_samples;
    } else {
        *avg_sps = (float)g_type_d_neutral_feed_sps_sum /
                   (float)g_type_d_neutral_feed_samples;
    }
    return *avg_sps > 0.0f;
}

static bool type_d_sample_demand_bounds(float *demand_sps) {
    if (!demand_sps) return false;
    int floor_sps = SYNC_MIN_SPS;
    int cap_sps = baseline_control_floor_sps();
    if (cap_sps < floor_sps) cap_sps = floor_sps;
    if (*demand_sps <= 0.0f) return false;
    *demand_sps = clamp_f(*demand_sps, (float)floor_sps, (float)cap_sps);
    return true;
}

static void relay_neutral_trim_clamp(void) {
    if (SYNC_RELAY_TRIM_CLAMP_SPS <= 0) {
        g_relay_neutral_trim_sps = 0.0f;
        return;
    }
    g_relay_neutral_trim_sps = clamp_f(g_relay_neutral_trim_sps,
        -(float)SYNC_RELAY_TRIM_CLAMP_SPS, (float)SYNC_RELAY_TRIM_CLAMP_SPS);
}

static void relay_neutral_trim_leak(buf_state_t s, uint32_t now_ms) {
    if (BUF_SENSOR_TYPE != 0 || s != BUF_NEUTRAL ||
        g_sync_state != SYNC_ACTIVE || g_relay_neutral_trim_sps == 0.0f) {
        g_relay_trim_last_leak_ms = now_ms;
        return;
    }
    if ((now_ms - g_buf.entered_ms) < SYNC_RELAY_TRIM_LEAK_DELAY_MS) {
        g_relay_trim_last_leak_ms = now_ms;
        return;
    }
    if (g_relay_trim_last_leak_ms == 0) {
        g_relay_trim_last_leak_ms = now_ms;
        return;
    }

    uint32_t dt_ms = now_ms - g_relay_trim_last_leak_ms;
    g_relay_trim_last_leak_ms = now_ms;
    if (dt_ms == 0) return;

    float leak_rate_sps_s = fmaxf((float)SYNC_RELAY_TRIM_STEP_SPS * SYNC_RELAY_TRIM_LEAK_RATE_FRAC, 25.0f);
    float leak_sps = leak_rate_sps_s * ((float)dt_ms / 1000.0f);
    if (g_relay_neutral_trim_sps > 0.0f) {
        g_relay_neutral_trim_sps = (g_relay_neutral_trim_sps > leak_sps)
            ? (g_relay_neutral_trim_sps - leak_sps) : 0.0f;
    } else {
        g_relay_neutral_trim_sps = (-g_relay_neutral_trim_sps > leak_sps)
            ? (g_relay_neutral_trim_sps + leak_sps) : 0.0f;
    }
}

static void buf_update(buf_state_t new_state, uint32_t now_ms) {
    if (new_state == g_buf.state) return;

    uint32_t prev_dwell = now_ms - g_buf.entered_ms;
    g_buf.dwell_ms = prev_dwell;
    lane_t *A = lane_ptr(active_lane);
    int mmu_now_sps = lane_motion_sps(A);

    float mmu_avg_sps = 0.0f;
    if (g_buf.mmu_sps_dwell_samples > 0) {
        mmu_avg_sps = (float)g_buf.mmu_sps_dwell_sum / (float)g_buf.mmu_sps_dwell_samples;
    } else {
        mmu_avg_sps = (float)(g_buf.mmu_sps_at_entry + mmu_now_sps) / 2.0f;
    }

    float travel_mm = 0.0f;
    buf_state_t old = g_buf.state;
    float threshold = buf_threshold_mm();
    float max_transition_mm = threshold * 2.0f;

    g_buf.arm_vel_mm_s = 0.0f;

    if (old == BUF_NEUTRAL) {
        if (new_state == BUF_TENSION) travel_mm = -threshold - g_buf_physical_entry_pos_mm;
        else if (new_state == BUF_COMPRESSION) travel_mm = threshold - g_buf_physical_entry_pos_mm;
    } else if (old == BUF_TENSION) {
        if (new_state == BUF_NEUTRAL) travel_mm = threshold;
        else if (new_state == BUF_COMPRESSION) travel_mm = max_transition_mm;
    } else if (old == BUF_COMPRESSION) {
        if (new_state == BUF_NEUTRAL) travel_mm = -threshold;
        else if (new_state == BUF_TENSION) travel_mm = -max_transition_mm;
    }

    travel_mm = clamp_f(travel_mm, -max_transition_mm, max_transition_mm);

    if (new_state == BUF_TENSION) {
        g_buf_physical_entry_pos_mm = -threshold;
    } else if (new_state == BUF_COMPRESSION) {
        g_buf_physical_entry_pos_mm = threshold;
    } else if (new_state == BUF_NEUTRAL) {
        if (old == BUF_TENSION) g_buf_physical_entry_pos_mm = -threshold;
        else if (old == BUF_COMPRESSION) g_buf_physical_entry_pos_mm = threshold;
    }

    bool neutral_fill_sample = (old == BUF_NEUTRAL && new_state == BUF_COMPRESSION);
    bool neutral_drain_sample = (old == BUF_NEUTRAL && new_state == BUF_TENSION);
    bool compression_drain_sample = (old == BUF_COMPRESSION && new_state == BUF_NEUTRAL);
    bool has_known_travel = fabsf(travel_mm) > 0.001f;

    if (BUF_SENSOR_TYPE == 0 && (neutral_fill_sample || neutral_drain_sample || compression_drain_sample) && prev_dwell > (uint32_t)BUF_HYST_MS) {
        uint32_t effective_dwell = prev_dwell - (uint32_t)(BUF_HYST_MS / 2);
        if (effective_dwell < 5) effective_dwell = 5;
        if (has_known_travel) {
            g_buf.arm_vel_mm_s = travel_mm / ((float)effective_dwell / 1000.0f);
        }

        int idx = g_buf.lane_idx_at_entry;
        if (idx < 0 || idx >= NUM_LANES) idx = 0;
        float mm_per_step = MM_PER_STEP[idx];
        if (mm_per_step <= 1e-6f) goto estimator_done;

        float est_sps = 0.0f;
        float alpha = 0.0f;
        bool sample_valid = false;

        if (neutral_fill_sample) {
            float feed_avg_sps = 0.0f;
            if (prev_dwell >= SYNC_NEUTRAL_FILL_MIN_DWELL_MS &&
                type_d_neutral_feed_avg_sps(&feed_avg_sps)) {
                if (has_known_travel) {
                    float fill_sps = fabsf(g_buf.arm_vel_mm_s) / mm_per_step;
                    float max_fill_sps = feed_avg_sps * (1.0f + SYNC_NEUTRAL_FILL_OVERRUN_FRAC);
                    if (fill_sps <= max_fill_sps) {
                        est_sps = feed_avg_sps - fill_sps;
                        if (est_sps < (float)SYNC_MIN_SPS) est_sps = (float)SYNC_MIN_SPS;
                        sample_valid = type_d_sample_demand_bounds(&est_sps);
                    }
                } else {
                    est_sps = feed_avg_sps;
                    sample_valid = type_d_sample_demand_bounds(&est_sps);
                }
                alpha = clamp_f((float)prev_dwell / SYNC_NEUTRAL_FILL_EST_ALPHA_DWELL_MS,
                                EST_ALPHA_MIN, EST_ALPHA_MAX);
            }
        } else if (neutral_drain_sample) {
            float feed_avg_sps = 0.0f;
            if (prev_dwell >= SYNC_NEUTRAL_FILL_MIN_DWELL_MS &&
                type_d_neutral_feed_avg_sps(&feed_avg_sps)) {
                if (has_known_travel) {
                    float drain_sps = fabsf(g_buf.arm_vel_mm_s) / mm_per_step;
                    est_sps = feed_avg_sps + drain_sps;
                    sample_valid = type_d_sample_demand_bounds(&est_sps);
                } else {
                    est_sps = feed_avg_sps * 1.15f; // softened from 1.5f to prevent EST overshooting spikes during physical drift
                    sample_valid = type_d_sample_demand_bounds(&est_sps);
                }
                alpha = clamp_f((float)prev_dwell / SYNC_NEUTRAL_FILL_EST_ALPHA_DWELL_MS,
                                EST_ALPHA_MIN, EST_ALPHA_MAX);
            }
        } else if (compression_drain_sample) {
            float drain_feed_sps = mmu_avg_sps;
            if (prev_dwell >= SYNC_DRAIN_EST_MIN_DWELL_MS &&
                (drain_feed_sps <= (float)SYNC_MIN_SPS || sync_current_sps <= SYNC_MIN_SPS)) {
                float mmu_mm_s = drain_feed_sps * mm_per_step;
                float extruder_mm_s = mmu_mm_s - g_buf.arm_vel_mm_s;
                est_sps = extruder_mm_s / mm_per_step;
                sample_valid = type_d_sample_demand_bounds(&est_sps);
                alpha = clamp_f((float)effective_dwell / SYNC_DRAIN_EST_ALPHA_DWELL_MS,
                                EST_ALPHA_MIN, EST_ALPHA_MAX);
            }
        } else {
            float mmu_mm_s = mmu_avg_sps * mm_per_step;
            float extruder_mm_s = mmu_mm_s - g_buf.arm_vel_mm_s;
            est_sps = extruder_mm_s / mm_per_step;

            const float estimator_norm_mm_s = 30.0f;
            alpha = clamp_f(fabsf(g_buf.arm_vel_mm_s) / estimator_norm_mm_s,
                            EST_ALPHA_MIN, EST_ALPHA_MAX);

            /* If the model was way off (e.g. at one end but hit the other), trust the new estimate more. */
            if (fabsf(travel_mm) > threshold * 1.5f && alpha < 0.5f) {
                alpha = 0.5f;
            }
            sample_valid = type_d_sample_demand_bounds(&est_sps);
        }

        if (sample_valid) {
            float prev_est_sps = extruder_est_sps;
            blend_extruder_est_sps(est_sps, alpha, now_ms);
            float reset_threshold_sps = fmaxf((float)SYNC_RELAY_TRIM_STEP_SPS, 100.0f);
            if (fabsf(extruder_est_sps - prev_est_sps) >= reset_threshold_sps) {
                g_relay_neutral_trim_sps *= 0.25f;
                if (fabsf(g_relay_neutral_trim_sps) < 1.0f) g_relay_neutral_trim_sps = 0.0f;
                relay_neutral_trim_clamp();
            }
        }
estimator_done:
        ;
    }

    /* Residual observer — measure pre-snap virtual/physical mismatch.
     * Record on both endstops to capture both positive and negative drift correctly. */
    if (BUF_SENSOR_TYPE == 0 && old == BUF_NEUTRAL && (new_state == BUF_TENSION || new_state == BUF_COMPRESSION)) {
        float switch_pos_mm = (new_state == BUF_TENSION) ? -threshold : threshold;
        float residual = g_buf_pos - switch_pos_mm;
        g_bp_residual_last_mm = residual;

        float tau_ms_f = (BUF_DRIFT_EWMA_TAU_MS > 0) ? (float)BUF_DRIFT_EWMA_TAU_MS : 60000.0f;
        float alpha;
        if (g_bp_drift_samples == 0 || g_bp_drift_last_ms == 0) {
            alpha = 1.0f;
        } else {
            uint32_t dt = now_ms - g_bp_drift_last_ms;
            alpha = 1.0f - expf(-(float)dt / tau_ms_f);
            if (alpha < 0.02f) alpha = 0.02f;
            if (alpha > 1.0f) alpha = 1.0f;
        }
        g_bp_drift_ewma_mm = alpha * residual + (1.0f - alpha) * g_bp_drift_ewma_mm;
        g_bp_drift_last_ms = now_ms;
        if (g_bp_drift_samples < 65535u) g_bp_drift_samples++;
    }

    if (BUF_SENSOR_TYPE == 0 && old == BUF_NEUTRAL && new_state == BUF_TENSION &&
        SYNC_RELAY_TRIM_STEP_SPS > 0 && SYNC_RELAY_TRIM_CLAMP_SPS > 0) {
        g_relay_neutral_trim_sps += (float)SYNC_RELAY_TRIM_STEP_SPS;
        relay_neutral_trim_clamp();
    }

    history_push(g_buf.state, prev_dwell);
    g_buf.state = new_state;
    g_buf.entered_ms = now_ms;
    if (BUF_SENSOR_TYPE == 0 && new_state == BUF_NEUTRAL) type_d_neutral_feed_reset();
    g_buf_confidence = 1.0f;
    g_buf_last_transition_ms = now_ms;
    g_buf_pos_sigma_accum_mm = 0.0f;
    g_buf_sigma_mm = 0.0f;
    g_buf_est_fallback_emitted = false;
    relay_on_transition(new_state, now_ms);
    
    g_sync_refill_effort_mm = 0.0f;
    g_sync_relieve_effort_mm = 0.0f;
    g_sync_cannot_refill_warned = false;
    g_sync_cannot_relieve_warned = false;
    
    if (BUF_SENSOR_TYPE == 0 && g_sync_state == SYNC_RELIEF_PAUSE && !g_boot_stabilizing &&
        (new_state == BUF_NEUTRAL || new_state == BUF_TENSION)) {
        g_buf_pos = buf_target_reserve_mm();
        sync_current_sps = sync_bootstrap_sps();
        sync_set_state(SYNC_ACTIVE);
        sync_auto_started = true;
        sync_tail_assist_active = A && !lane_in_present(A) && lane_out_present(A);
        sync_idle_since_ms = 0;
        cmd_event("SYNC", "AUTO_START");
    }
    
    buf_anchor_virtual_position(old, new_state);

    g_buf.lane_idx_at_entry = (active_lane == 2) ? 1 : 0;
    g_buf.mmu_sps_at_entry = mmu_now_sps;
    g_buf.mmu_sps_dwell_sum = 0;
    g_buf.mmu_sps_dwell_samples = 0;
}

static int g_settle_history[16];
static uint8_t g_settle_history_count = 0;
static uint32_t g_last_baseline_update_ms = 0;
static float g_last_baseline_update_mm = 0.0f;
float g_sync_mmu_total_mm = 0.0f;

static void baseline_update_on_settle(uint32_t neutral_dwell_ms, uint32_t now_ms) {
    if (neutral_dwell_ms <= 500) {
        g_settle_history_count = 0;
        return;
    }

    uint8_t n = CONF_BASELINE_SETTLE_COUNT;
    if (n > 16) n = 16;
    if (n < 1) n = 1;

    for (int i = 15; i > 0; i--) {
        g_settle_history[i] = g_settle_history[i - 1];
    }
    g_settle_history[0] = sync_current_sps;

    if (g_settle_history_count < n) {
        g_settle_history_count++;
    }

    if (g_settle_history_count >= n) {
        int min_sps = g_settle_history[0];
        int max_sps = g_settle_history[0];
        int sum_sps = 0;
        for (int i = 0; i < n; i++) {
            if (g_settle_history[i] < min_sps) min_sps = g_settle_history[i];
            if (g_settle_history[i] > max_sps) max_sps = g_settle_history[i];
            sum_sps += g_settle_history[i];
        }

        float mean_sps = (float)sum_sps / (float)n;
        float variance_frac = (mean_sps > 0.1f) ? ((float)(max_sps - min_sps) / mean_sps) : 0.0f;

        if (variance_frac <= CONF_BASELINE_VARIANCE_REJECT_FRAC) {
            uint32_t elapsed_ms = now_ms - g_last_baseline_update_ms;
            float elapsed_mm = g_sync_mmu_total_mm - g_last_baseline_update_mm;

            if (g_last_baseline_update_ms == 0 ||
                (elapsed_ms >= CONF_BASELINE_COOLDOWN_MS && elapsed_mm >= CONF_BASELINE_COOLDOWN_MM)) {
                
                int flow_sps = (int)extruder_est_sps;
                int segment = flow_active_segment(flow_sps);
                flow_param_t fp = flow_param(flow_sps);
                int new_baseline = (int)(FLARE_INT_BASELINE_ALPHA * (float)sync_current_sps + (1.0f - FLARE_INT_BASELINE_ALPHA) * (float)fp.baseline_sps);
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

static int baseline_control_floor_sps(void) {
    flow_param_t fp = flow_param((int)extruder_est_sps);
    return (fp.baseline_sps > g_baseline_target_sps) ? fp.baseline_sps : g_baseline_target_sps;
}

static int sync_neutral_anti_tension_floor_sps(buf_state_t s, lane_t *A,
                                           float error_norm,
                                           float deadband_norm,
                                           int neutral_target_sps,
                                           uint32_t now_ms) {
    if (g_sync_state != SYNC_ACTIVE || s != BUF_NEUTRAL || !A) return 0;
    if (A->task != TASK_FEED || A->fault != FAULT_NONE) return 0;
    (void)now_ms;

    if (BUF_SENSOR_TYPE == 0) {
        if (error_norm >= -deadband_norm) return 0;

        int demand_floor_sps = (int)(extruder_est_sps * 1.05f);
        if (demand_floor_sps < SYNC_MIN_SPS) demand_floor_sps = SYNC_MIN_SPS;
        if (neutral_target_sps > 0 && demand_floor_sps > neutral_target_sps) {
            demand_floor_sps = neutral_target_sps;
        }

        int baseline_floor_sps = baseline_control_floor_sps();
        int assist_floor_sps = (int)((float)baseline_floor_sps * SYNC_NEUTRAL_ANTI_TENSION_FLOOR_FRAC);
        if (assist_floor_sps > demand_floor_sps) assist_floor_sps = demand_floor_sps;
        if (assist_floor_sps <= SYNC_MIN_SPS) return 0;
        return assist_floor_sps;
    }

    if (error_norm > deadband_norm) return 0;

    /* Type-P legacy path: unconditional refill floor. The floor is baseline-
     * derived so it can never itself drive the buffer toward TENSION. */
    int baseline_floor_sps = baseline_control_floor_sps();
    int assist_floor_sps = (int)((float)baseline_floor_sps * SYNC_NEUTRAL_ANTI_TENSION_FLOOR_FRAC);
    if (assist_floor_sps <= SYNC_MIN_SPS) return 0;

    return assist_floor_sps;
}

int sync_clamp_max_sps(int requested_sps) {
    return motion_clamp_rate_sps(requested_sps);
}

void sync_set_state(sync_state_t new_state) {
    if (g_sync_state == new_state) return;
    /* Seed the type-P smoothing filter to the current feed on (re)entering active
       sync so it doesn't slew from a stale value left by a prior session. */
    if (new_state == SYNC_ACTIVE) g_psf_target_filt = (float)sync_current_sps;
    g_sync_state = new_state;
    g_sync_tension_transitioned = false;
    g_sync_refill_effort_mm = 0.0f;
    g_sync_relieve_effort_mm = 0.0f;
    g_sync_cannot_refill_warned = false;
    g_sync_cannot_relieve_warned = false;
}

void sync_retract_assist_set(bool enabled) {
    lane_t *A = lane_ptr(active_lane);
    if (enabled) {
        sync_current_sps = 0;
        sync_auto_started = false;
        sync_tail_assist_active = false;
        sync_idle_since_ms = 0;
        g_bl_autostart_suppressed = false;
        sync_set_state(SYNC_RETRACT_ASSIST);
        if (A && A->task == TASK_FEED) lane_stop(A);
    } else {
        if (g_sync_state == SYNC_RETRACT_ASSIST) {
            /* Stop any BL-driven motor motion before transitioning to SYNC_OFF */
            if (g_bl_sub_state != BL_IDLE && A) {
                motor_set_rate_sps(&A->m, 0);
                motor_enable(&A->m, false);
            }
            g_bl_sub_state = BL_IDLE;
            g_bl_prime_start_ms      = 0;
            g_bl_prime_mm_per_s      = 0.0f;
            g_bl_prime_cap_mm        = 0.0f;
            g_bl_prime_switch_hit    = false;
            g_bl_prime_post_start_ms = 0;
            g_bl_prime_post_cap_mm   = 0.0f;
            g_bl_follow_mm           = 0.0f;
            g_bl_follow_rate_mmpm    = 0.0f;
            g_bl_follow_start_ms     = 0;
            g_bl_follow_mm_per_s     = 0.0f;
            g_bl_watchdog_ms = 0;
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

/* Called when the BL watchdog fires. Releases the lock then immediately
 * clears the auto-start suppression flag and triggers a full buffer stabilize
 * so sync can re-engage without requiring a manual BS from the host. */
static void handle_bl_watchdog_timeout(uint32_t now_ms) {
    cmd_event("EV:BL", "TIMEOUT");
    sync_retract_assist_release(now_ms);
    sync_bl_clear_autostart_suppress();
    (void)buffer_stabilize_request(now_ms);
}

/* Clear the BL auto-start suppression flag. The flag is set when BL releases
 * with the buffer still at the armed extreme (no extruder demand to depart
 * the band) to prevent sync from immediately re-engaging in AUTO_MODE. An
 * explicit operator stabilize via BS must clear it so the buffer can be
 * driven back to NEUTRAL even when raw is still pinned at TENSION. */
void sync_bl_clear_autostart_suppress(void) {
    g_bl_autostart_suppressed = false;
}

bool sync_retract_assist_enabled(void) {
    return g_sync_state == SYNC_RETRACT_ASSIST;
}

/* Arm the buffer-lock: drive the active lane to the requested extreme and lock.
 * Accepts BUF_TENSION ("BL:T") or BUF_COMPRESSION ("BL:C").
 * Optional follow_mm/follow_rate_mmpm arm an event-triggered follow-on
 * retract (same direction as prime) that fires on the first raw transition
 * after LOCKED. Pass 0 for both to skip the follow-on (passive lock only).
 * Safe to call when already in SYNC_RETRACT_ASSIST (re-arms with a fresh prime). */
void sync_buffer_lock_arm(buf_state_t target, float follow_mm,
                          float follow_rate_mmpm, uint32_t now_ms) {
    lane_t *A = lane_ptr(active_lane);
    if (!A) return;

    /* Stop any in-progress BL motor drive before re-arming */
    if (g_bl_sub_state != BL_IDLE) {
        motor_set_rate_sps(&A->m, 0);
        motor_enable(&A->m, false);
    }

    /* Enter (or stay in) SYNC_RETRACT_ASSIST */
    sync_current_sps = 0;
    sync_auto_started = false;
    sync_tail_assist_active = false;
    sync_idle_since_ms = 0;
    sync_set_state(SYNC_RETRACT_ASSIST);
    if (A->task == TASK_FEED) lane_stop(A);

    g_bl_target_state = target;
    /* Park the buffer goal at the armed rail until BS/timeout clears it. */
    g_bl_goal_override = target;

    /* Prime runs at SYNC_MAX_SPS. Overtravel is controlled by the two-phase
     * gates:
     *   phase 1 — search until switch fires (outer cap: BUF_MAX_TRAVEL_MM)
     *   phase 2 — lock at switch click (no post-settle travel). */
    int idx = A->lane_id - 1;
    /* Type-P (analog) primes gently at BUF_STAB_SPS: the PSF EMA filter lags,
     * so a full-speed SYNC_MAX_SPS prime overshoots PSF_HOME_THRESHOLD_NORM and
     * slams the rail before the motor reads the threshold and stops. Type-D
     * (switch) is bang-bang — the click stops it instantly, so full speed is fine. */
    int prime_sps  = (BUF_SENSOR_TYPE == 1)
                     ? sync_clamp_max_sps(BUF_STAB_SPS)
                     : sync_clamp_max_sps(SYNC_MAX_SPS);
    float mm_per_s = (float)prime_sps * MM_PER_STEP[idx];
    float max_cap_mm  = (BUF_MAX_TRAVEL_MM > 0) ? (float)BUF_MAX_TRAVEL_MM : 25.0f;
    g_bl_prime_start_ms      = now_ms;
    g_bl_prime_mm_per_s      = mm_per_s;
    g_bl_prime_cap_mm        = max_cap_mm;
    g_bl_prime_switch_hit    = false;
    g_bl_prime_post_start_ms = 0;
    g_bl_prime_post_cap_mm   = 0.0f;
    /* Subtract BUF_MAX_TRAVEL_MM/2 so FOLLOW finishes near NEUTRAL rather
     * than at the switch click. After the extruder stops the MMU keeps
     * draining; parking at the switch click leaves one step before the
     * mechanical hard end. If the adjusted distance is ≤ 0, the macro
     * doesn't need a follow-on for such a short move — use passive lock. */
    {
        float half_travel = (BUF_MAX_TRAVEL_MM > 0)
                            ? ((float)BUF_MAX_TRAVEL_MM * 0.5f) : 12.5f;
        float effective_follow_mm = (follow_mm > 0.0f && follow_rate_mmpm > 0.0f)
                                    ? (follow_mm - half_travel) : 0.0f;
        g_bl_follow_mm        = (effective_follow_mm > 0.0f) ? effective_follow_mm : 0.0f;
        g_bl_follow_rate_mmpm = (g_bl_follow_mm > 0.0f) ? follow_rate_mmpm : 0.0f;
    }
    g_bl_follow_start_ms     = 0;
    g_bl_follow_mm_per_s     = 0.0f;
    g_bl_follow_traveled_mm  = 0.0f;
    g_bl_last_tick_ms        = now_ms;

    g_bl_watchdog_ms = 0;
    g_bl_sub_state = BL_PRIME;

    /* BL:T → retract (forward=false) to pull buffer toward tension extreme.
     * BL:C → extrude (forward=true) to push buffer toward compression extreme. */
    bool forward = (target == BUF_COMPRESSION);
    motor_enable(&A->m, true);
    motor_set_dir(&A->m, forward);
    motor_set_rate_sps(&A->m, prime_sps);

    cmd_event("BL", "PRIME");
}

/* Return the status string for the BL arm field: "T", "C", or "0". */
const char *sync_buffer_lock_arm_str(void) {
    if (g_sync_state != SYNC_RETRACT_ASSIST || g_bl_sub_state == BL_IDLE) return "0";
    return (g_bl_target_state == BUF_TENSION) ? "T" : "C";
}

/* True while BL is driving the lane motor (PRIME or FOLLOW). False during
 * LOCKED (motor at zero) and outside of SYNC_RETRACT_ASSIST. Used by
 * autopreload to know when IN-sensor edges come from BL motion (consume)
 * vs real operator insertion (process). */
bool sync_buffer_lock_motor_moving(void) {
    return g_sync_state == SYNC_RETRACT_ASSIST &&
           (g_bl_sub_state == BL_PRIME || g_bl_sub_state == BL_FOLLOW);
}

/* Per-tick handler for the buffer-lock lifecycle.
 * Called every sync_tick iteration while g_sync_state == SYNC_RETRACT_ASSIST
 * and g_bl_sub_state != BL_IDLE.
 * Runs at main-loop rate (no SYNC_TICK_MS gating) so lock-break is detected
 * on the same tick as the raw buffer edge. */
static void sync_buffer_lock_tick(lane_t *A, uint32_t now_ms) {
    if (!A) return;

    if (g_bl_sub_state == BL_PRIME) {
        bool reached = false;
        if (BUF_SENSOR_TYPE == 1) {
            if (g_bl_target_state == BUF_TENSION) reached = (g_buf_pos <= -PSF_HOME_THRESHOLD_NORM);
            else if (g_bl_target_state == BUF_COMPRESSION) reached = (g_buf_pos >= PSF_HOME_THRESHOLD_NORM);
        } else {
            buf_state_t raw = buf_state_raw();
            reached = (raw == g_bl_target_state);
        }

        /* Phase 1 — search: outer safety cap fires if switch never triggers */
        float traveled_mm = (g_bl_prime_mm_per_s > 0.0f)
            ? ((float)(now_ms - g_bl_prime_start_ms) / 1000.0f * g_bl_prime_mm_per_s)
            : g_bl_prime_cap_mm;
        bool deadline_hit = (!g_bl_prime_switch_hit && traveled_mm >= g_bl_prime_cap_mm);

        /* Switch click: transition to post-click settle phase */
        if (!g_bl_prime_switch_hit && reached) {
            g_bl_prime_switch_hit    = true;
            g_bl_prime_post_start_ms = now_ms;
        }

        /* Phase 2 — post-click settle: continue (max-span)/2 past the switch */
        bool post_done = false;
        if (g_bl_prime_switch_hit) {
            float post_mm = (g_bl_prime_mm_per_s > 0.0f)
                ? ((float)(now_ms - g_bl_prime_post_start_ms) / 1000.0f * g_bl_prime_mm_per_s)
                : g_bl_prime_post_cap_mm;
            post_done = (post_mm >= g_bl_prime_post_cap_mm);
        }

        if (post_done || deadline_hit) {
            /* Prime done — stop motor but keep enabled for holding torque */
            motor_set_rate_sps(&A->m, 0);
            /* motor_enable stays true: locked hold needs energized stepper */

            if (deadline_hit) {
                cmd_event("EV:BL", "PRIME_BOUND");
            }

            g_bl_sub_state = BL_LOCKED;
            g_bl_watchdog_ms = now_ms + BL_WATCHDOG_DEFAULT_MS;
            cmd_event("BL", "LOCKED");
        }

    } else if (g_bl_sub_state == BL_LOCKED) {
        /* Passive lock: motor energized at zero rate (holding torque).
         * If a follow-on retract is armed, watch for the first raw
         * transition off the armed extreme (extruder started filling
         * the buffer) and fire concurrent MMU motion in the prime
         * direction. Otherwise, buffer is free to migrate via external
         * force; only the watchdog can break the lock from firmware. */
        if (g_bl_follow_mm > 0.0f) {
            bool lock_broken = false;
            if (BUF_SENSOR_TYPE == 1) {
                if (g_bl_target_state == BUF_TENSION) lock_broken = (g_buf_pos > -PSF_HOME_THRESHOLD_NORM);
                else if (g_bl_target_state == BUF_COMPRESSION) lock_broken = (g_buf_pos < PSF_HOME_THRESHOLD_NORM);
            } else {
                buf_state_t raw = buf_state_raw();
                lock_broken = (raw != g_bl_target_state);
            }

            if (lock_broken) {
                int idx = A->lane_id - 1;
                int follow_sps = (int)(g_bl_follow_rate_mmpm / 60.0f / MM_PER_STEP[idx] + 0.5f);
                if (follow_sps < 1) follow_sps = 1;
                follow_sps = sync_clamp_max_sps(follow_sps);
                bool forward = (g_bl_target_state == BUF_COMPRESSION);
                motor_set_dir(&A->m, forward);
                /* Ramp the follow like the normal motion path. An instant 0->target
                 * rate jump exceeds the stepper's pull-in torque and stalls (it can
                 * spin fast but can't *start* fast), so a fast retract gets no follow
                 * at all and the buffer fills to COMPRESSION. Start at the pull-in-
                 * safe RAMP_STEP_SPS and accelerate toward follow_sps in BL_FOLLOW. */
                int start_sps = RAMP_STEP_SPS;
                if (start_sps > follow_sps) start_sps = follow_sps;
                if (start_sps < 1) start_sps = 1;
                motor_set_rate_sps(&A->m, start_sps);

                g_bl_follow_start_ms = now_ms;
                g_bl_follow_cur_sps = start_sps;
                g_bl_follow_target_sps = follow_sps;
                g_bl_follow_ramp_tick_ms = now_ms;
                g_bl_follow_mm_per_s = (float)start_sps * MM_PER_STEP[idx];
                g_bl_follow_traveled_mm = 0.0f;
                g_bl_last_tick_ms = now_ms;
                g_bl_sub_state = BL_FOLLOW;
                cmd_event("BL", "FOLLOW");
                return;
            }
        }
        if (g_bl_watchdog_ms != 0 &&
            (int32_t)(now_ms - g_bl_watchdog_ms) >= 0) {
            handle_bl_watchdog_timeout(now_ms);
        }
    } else if (g_bl_sub_state == BL_FOLLOW) {
        /* Concurrent follow-on retract. Mass-balances the extruder fill
         * over a known distance; when the armed distance is consumed,
         * stop the motor and return to LOCKED (waiting for BS or watchdog). */
        int idx = A->lane_id - 1;
        float dt_s = (float)(now_ms - g_bl_last_tick_ms) / 1000.0f;
        if (dt_s < 0.0001f) dt_s = 0.0001f;
        if (dt_s > 0.1f) dt_s = 0.001f;

        /* Type-P safety gate: the open-loop follow is blind to position, so if
         * it out-drives the extruder backflow it would slam the armed rail.
         * Stop the motor and drop to LOCKED before reaching the rail; if the
         * backflow later pushes the buffer back off the extreme the lock breaks
         * again and the follow re-fires to keep absorbing. Type-D has no analog
         * position, so it can only rely on the elapsed-distance budget below. */
        if (BUF_SENSOR_TYPE == 1) {
            bool rail_hit = (g_bl_target_state == BUF_TENSION)
                            ? (g_buf_pos <= -PSF_FOLLOW_RAIL_NORM)
                            : (g_buf_pos >= PSF_FOLLOW_RAIL_NORM);
            if (rail_hit) {
                motor_set_rate_sps(&A->m, 0);
                g_bl_sub_state = BL_LOCKED;
                cmd_event("EV:BL", "FOLLOW_GATED");
                g_bl_last_tick_ms = now_ms;
                return;
            }
        }

        /* Accelerate toward the target follow rate (pull-in-safe ramp). */
        if (g_bl_follow_cur_sps < g_bl_follow_target_sps &&
            (int32_t)(now_ms - g_bl_follow_ramp_tick_ms) >= RAMP_TICK_MS) {
            g_bl_follow_ramp_tick_ms = now_ms;
            g_bl_follow_cur_sps += RAMP_STEP_SPS;
            if (g_bl_follow_cur_sps > g_bl_follow_target_sps)
                g_bl_follow_cur_sps = g_bl_follow_target_sps;
            motor_set_rate_sps(&A->m, g_bl_follow_cur_sps);
            g_bl_follow_mm_per_s = (float)g_bl_follow_cur_sps * MM_PER_STEP[idx];
        }

        /* Integrate distance at the current (ramping) rate, not a fixed one. */
        g_bl_follow_traveled_mm += g_bl_follow_mm_per_s * dt_s;
        float traveled = g_bl_follow_traveled_mm;

        if (traveled >= g_bl_follow_mm) {
            motor_set_rate_sps(&A->m, 0);
            g_bl_follow_mm        = 0.0f;
            g_bl_follow_rate_mmpm = 0.0f;
            g_bl_follow_start_ms  = 0;
            g_bl_follow_mm_per_s  = 0.0f;
            g_bl_follow_traveled_mm = 0.0f;
            g_bl_sub_state = BL_LOCKED;
            cmd_event("BL", "FOLLOW_DONE");
        } else if (g_bl_watchdog_ms != 0 &&
                   (int32_t)(now_ms - g_bl_watchdog_ms) >= 0) {
            handle_bl_watchdog_timeout(now_ms);
        }
    }
    g_bl_last_tick_ms = now_ms;
}

void sync_relief_pause(void) {
    sync_set_state(SYNC_RELIEF_PAUSE);
    sync_current_sps = 0;
}

static uint32_t g_sync_fault_hold_entry_ms = 0;

void sync_fault_hold(void) {
    sync_set_state(SYNC_FAULT_HOLD);
    sync_current_sps = 0;
    g_sync_fault_hold_entry_ms = g_now_ms;
}

void sync_disable(bool reset_estimator) {
    sync_set_state(SYNC_OFF);
    sync_auto_started = false;
    sync_tail_assist_active = false;
    sync_current_sps = 0;
    sync_idle_since_ms = 0;
    sync_fast_brake_until_ms = 0;
    sync_compression_recovery_active = false;
    sync_continuous_compression_since_ms = 0;
    sync_post_compression_boost_until_ms = 0;
    sync_recent_negative_until_ms = 0;
    sync_tension_pin_since_ms = 0;
    g_buf_confidence = 1.0f;
    sync_reserve_integral_mm = 0.0f;
    g_buf_pos_sigma_accum_mm = 0.0f;
    g_buf_sigma_mm = 0.0f;
    g_buf_est_low_cf_emit_ms = 0;
    g_buf_tension_dwell_warn_emit_ms = 0;
    g_buf_est_fallback_emitted = false;
    if (g_bp_drift_samples > 0) cmd_event("BUF", "DRIFT_RESET");
    g_bp_residual_last_mm = 0.0f;
    g_bp_drift_ewma_mm = 0.0f;
    g_bp_drift_samples = 0;
    g_bp_drift_last_ms = 0;
    g_bp_drift_correction_applied_mm = 0.0f;
    memset(g_tension_pin_ts, 0, sizeof(g_tension_pin_ts));
    g_tension_pin_ts_idx = 0;
    g_tension_risk_emit_ms = 0;
    g_relay_flip_travel_since_mm = 0.0f;
    g_relay_neutral_trim_sps = 0.0f;
    g_relay_trim_last_leak_ms = 0;
    type_d_neutral_feed_reset();

    if (reset_estimator) {
        extruder_est_sps = 0.0f;
        extruder_est_prev_sps = 0.0f;
        extruder_est_last_update_ms = g_now_ms;
    }
}

static int sync_bootstrap_sps(void) {
    int max_sps = sync_clamp_max_sps(SYNC_MAX_SPS);
    int startup_floor_sps = COMPRESSION_SPS + PRE_RAMP_SPS;
    int baseline_floor_sps = baseline_control_floor_sps();

    /* Adaptive floor: if we have a learned baseline, don't start way below it. */
    if (baseline_floor_sps > (startup_floor_sps * 2)) {
        int adaptive_floor = baseline_floor_sps / 2;
        if (adaptive_floor > startup_floor_sps) startup_floor_sps = adaptive_floor;
    }
    if (startup_floor_sps < BUF_STAB_SPS) startup_floor_sps = BUF_STAB_SPS;

    bool est_fresh = extruder_est_last_update_ms != 0 &&
                     (g_now_ms - extruder_est_last_update_ms) < SYNC_EST_FRESH_MS &&
                     extruder_est_sps >= (float)startup_floor_sps;

    int res;
    if (est_fresh) {
        res = clamp_i((int)extruder_est_sps, startup_floor_sps, max_sps);
    } else {
        res = clamp_i(startup_floor_sps, COMPRESSION_SPS, max_sps);
    }

    /* F2b: a post-recovery start ramps from the learned baseline, not a
     * collapsed/high estimator, so bootstrap cannot slam TENSION. The
     * startup_floor lower bound is still honored. */
    if (baseline_floor_sps >= startup_floor_sps && res > baseline_floor_sps)
        res = baseline_floor_sps;

    if (!est_fresh) {
        extruder_est_sps = (float)res;
        extruder_est_prev_sps = (float)res;
    }

    extruder_est_last_update_ms = g_now_ms;
    sync_fast_brake_until_ms = 0;
    sync_continuous_compression_since_ms = 0;
    return res;
}

static int sync_effective_kp_sps(buf_state_t s) {
    int baseline_ref_sps = baseline_control_floor_sps();
    int baseline_limited_kp = (s == BUF_TENSION) ? (baseline_ref_sps * 2) : (baseline_ref_sps / 3);
    if (baseline_limited_kp < COMPRESSION_SPS) baseline_limited_kp = COMPRESSION_SPS;
    return (SYNC_KP_SPS < baseline_limited_kp) ? SYNC_KP_SPS : baseline_limited_kp;
}

static void sync_apply_to_active(void) {
    lane_t *A = lane_ptr(active_lane);
    if (!A) {
        sync_current_sps = 0;
        return;
    }
    if (A->task == TASK_MOVE) return;

    bool is_protected_task = (A->task == TASK_UNLOAD || A->task == TASK_AUTOLOAD);

    if (sync_current_sps > 0) {
        if (is_protected_task) {
            A->current_sps = sync_current_sps;
            A->target_sps = sync_current_sps;
            motor_set_rate_sps(&A->m, sync_current_sps);
            motor_enable(&A->m, true);
        } else if (A->task != TASK_FEED && A->fault == FAULT_NONE) {
            lane_start(A, TASK_FEED, sync_current_sps, true, g_now_ms, 0);
        } else {
            A->current_sps = sync_current_sps;
            A->target_sps = sync_current_sps;
            motor_set_rate_sps(&A->m, sync_current_sps);
            motor_enable(&A->m, true);
            motor_set_dir(&A->m, true);
        }
    } else if (A->task == TASK_FEED) {
        lane_stop(A);
    }
}

static void sync_on_transition(buf_state_t prev, buf_state_t now_state, uint32_t now_ms) {
    if (prev == BUF_TENSION && now_state == BUF_COMPRESSION) {
        sync_fast_brake_until_ms = now_ms + 250u;
    }

    if (now_state == BUF_TENSION) {
        sync_tension_pin_since_ms = now_ms;
        lane_t *A = lane_ptr(active_lane);
        if (A && (A->task == TASK_IDLE || A->task == TASK_FEED)) {
            g_sync_tension_transitioned = true;
        }
        g_tension_pin_ts[g_tension_pin_ts_idx] = now_ms;
        g_tension_pin_ts_idx = (g_tension_pin_ts_idx + 1) % TENSION_PIN_WINDOW_LEN;
    } else if (prev == BUF_TENSION) {
        sync_tension_pin_since_ms = 0;
        /* Buffer physically departed tension — safe to allow auto-start again. */
        g_bl_autostart_suppressed = false;
    }

    if (!sync_tail_assist_active) {
        if (now_state == BUF_COMPRESSION) {
            g_neutral_creep_sps = 0;
            sync_compression_recovery_active = true;
            sync_continuous_compression_since_ms = 0;
            sync_post_compression_boost_until_ms = 0;
        } else if (prev == BUF_COMPRESSION && now_state == BUF_NEUTRAL) {
            if (sync_compression_recovery_active) sync_post_compression_boost_until_ms = now_ms + 300u;
            sync_compression_recovery_active = false;
            sync_continuous_compression_since_ms = 0;
        } else if (now_state == BUF_TENSION) {
            sync_compression_recovery_active = false;
            sync_continuous_compression_since_ms = 0;
            sync_post_compression_boost_until_ms = 0;
        }
    }

    bool fast_brake_active = sync_fast_brake_until_ms != 0 && (int32_t)(sync_fast_brake_until_ms - now_ms) > 0;
    if (prev != BUF_NEUTRAL && now_state == BUF_NEUTRAL && sync_enabled &&
        !fast_brake_active && !sync_compression_recovery_active && !sync_guard_active) {
        baseline_update_on_settle(g_buf.dwell_ms, now_ms);
    }
}

static void buf_signal_publish(uint32_t now_ms) {
    float half = buf_physical_half_travel_mm();
    buf_source_kind_t kind;
    float norm;
    if (BUF_SENSOR_TYPE == 0) {
        kind = BUF_SRC_VIRTUAL_ENDSTOP;
        norm = (half > 0.001f) ? (g_buf_pos / half) : 0.0f;
        norm = clamp_f(norm, -1.0f, 1.0f);
        {
            float sigma_cap = (EST_SIGMA_HARD_CAP_MM > 0.1f) ? EST_SIGMA_HARD_CAP_MM : 1.5f;
            g_buf_sigma_mm = sqrtf(g_buf_pos_sigma_accum_mm) * ENDSTOP_PER_UNIT_SIGMA_MM;
            if (g_buf_sigma_mm > sigma_cap) g_buf_sigma_mm = sigma_cap;
            g_buf_confidence = clamp_f(1.0f - g_buf_sigma_mm / sigma_cap, 0.0f, 1.0f);
            if (g_buf_confidence == 0.0f && !g_buf_est_fallback_emitted) {
                g_buf_est_fallback_emitted = true;
                sync_reserve_integral_mm = 0.0f;
                cmd_event("BUF", "EST_FALLBACK");
                g_bp_drift_ewma_mm = 0.0f;
                g_bp_drift_samples = 0;
                g_bp_drift_last_ms = 0;
                g_bp_drift_correction_applied_mm = 0.0f;
                cmd_event("BUF", "DRIFT_RESET");
            }
            if (sync_enabled && g_buf_confidence < EST_LOW_CF_WARN_THRESHOLD) {
                if (g_buf_est_low_cf_emit_ms == 0 ||
                    (now_ms - g_buf_est_low_cf_emit_ms) >= 5000u) {
                    g_buf_est_low_cf_emit_ms = now_ms;
                    cmd_event("BUF", "EST_LOW_CF");
                }
            } else {
                g_buf_est_low_cf_emit_ms = 0;
            }
        }
        g_buf_analog_saturated_since_ms = 0;
        g_buf_analog_last_sample_ms = now_ms;
    } else {
        kind = BUF_SRC_ANALOG;
        norm = g_buf_pos;
        g_buf_analog_last_sample_ms = now_ms;
        if (fabsf(norm) >= 0.99f) {
            if (g_buf_analog_saturated_since_ms == 0) g_buf_analog_saturated_since_ms = now_ms;
            if ((now_ms - g_buf_analog_saturated_since_ms) > 250u) g_buf_confidence = 0.5f;
        } else {
            g_buf_analog_saturated_since_ms = 0;
            g_buf_confidence = 1.0f;
        }
    }
    g_buf_signal.pos_norm   = norm;
    g_buf_signal.pos_mm     = (BUF_SENSOR_TYPE == 0) ? g_buf_pos : (norm * half);
    g_buf_signal.confidence = g_buf_confidence;
    g_buf_signal.age_ms     = now_ms - g_buf_analog_last_sample_ms;
    g_buf_signal.zone       = g_buf.state;
    g_buf_signal.kind       = kind;
    g_buf_signal.fault      = (g_buf.state == BUF_FAULT);
}

void buf_sensor_tick(uint32_t now_ms) {
    uint32_t elapsed_ms = now_ms - buf_pos_last_ms;
    bool do_pos = elapsed_ms >= (uint32_t)SYNC_TICK_MS;
    if (do_pos) buf_pos_last_ms = now_ms;

    if (BUF_SENSOR_TYPE == 1 && do_pos) buf_analog_update();

    buf_state_t prev = g_buf.state;
    buf_state_t s = buf_read_stable(now_ms);
    if (s != prev) {
        buf_update(s, now_ms);
        sync_on_transition(prev, s, now_ms);
    }

    if (BUF_SENSOR_TYPE == 0 && do_pos) {
        buf_virtual_position_tick(lane_ptr(active_lane), elapsed_ms);
    }

    if (do_pos) {
        lane_t *A = lane_ptr(active_lane);
        if (A) {
            uint8_t idx = (active_lane == 2) ? 1 : 0;
            float delta_mm = (float)lane_motion_sps(A) * ((float)elapsed_ms / 1000.0f) * MM_PER_STEP[idx];
            g_sync_mmu_total_mm += delta_mm;
            g_relay_flip_travel_since_mm += fabsf(delta_mm);

            if (BUF_SENSOR_TYPE == 1 && sync_enabled) {
                float mmu_mm_s = (float)lane_motion_sps(A) * MM_PER_STEP[idx];
                float arm_vel = g_vel_norm * buf_physical_half_travel_mm();
                /* Filament conservation: extruder draw = mmu feed + buffer drain rate.
                   Buffer drains toward TENSION, which is now NEGATIVE velocity, so the
                   drain rate is -arm_vel (was +arm_vel under the old +tension sign). */
                float extruder_mm_s = mmu_mm_s - arm_vel;
                float est_sps = 0.0f;
                if (MM_PER_STEP[idx] > 1e-6f) {
                    est_sps = extruder_mm_s / MM_PER_STEP[idx];
                }
                float max_est_sps = (float)GLOBAL_MAX_SPS;
                if (est_sps < 0.0f) est_sps = 0.0f;
                if (est_sps > max_est_sps) est_sps = max_est_sps;

                float alpha = 0.1f;
                extruder_est_sps = alpha * est_sps + (1.0f - alpha) * extruder_est_sps;
                extruder_est_last_update_ms = now_ms;
            }

            if (g_buf.state == BUF_TENSION) {
                g_sync_refill_effort_mm += delta_mm;
                if (!g_sync_cannot_refill_warned && g_sync_refill_effort_mm >= CONF_SYNC_CANNOT_REFILL_MM) {
                    g_sync_cannot_refill_warned = true;
                    cmd_event("SYNC", "cannot_refill");
                }
            } else if (g_buf.state == BUF_COMPRESSION) {
                g_sync_relieve_effort_mm += delta_mm;
                if (!g_sync_cannot_relieve_warned && g_sync_relieve_effort_mm >= CONF_SYNC_CANNOT_RELIEVE_MM) {
                    g_sync_cannot_relieve_warned = true;
                    cmd_event("SYNC", "cannot_relieve");
                }
            }
        }

        buf_signal_publish(now_ms);
    }
}

static int relay_control_law(buf_state_t s) {
    int relay_base = baseline_control_floor_sps();
    if (s == BUF_TENSION) {
        return (int)((float)relay_base * RELAY_CATCHUP_FRAC);
    } else if (s == BUF_COMPRESSION) {
        return 0;
    } else {
        int demand_sps = (int)extruder_est_sps;
        int neutral = (int)((float)demand_sps * RELAY_NEUTRAL_FRAC + g_relay_neutral_trim_sps);
        if (neutral < SYNC_MIN_SPS) neutral = SYNC_MIN_SPS;
        if (neutral > relay_base) neutral = relay_base;
        return neutral;
    }
}

static int psf_control_law(float error_norm) {
    int max_sps = sync_clamp_max_sps(SYNC_MAX_SPS);
    int kp_window = sync_effective_kp_sps(g_buf.state);

    /* Convention: g_buf_pos +compression / -tension. error_norm = pos - goal,
       so +error = compression-side (overfed) → must FEED LESS, -error = tension
       (starved) → FEED MORE. Negate so the proportional correction drives toward
       goal instead of away (positive feedback). Same for the velocity damping. */
    float p_err = -error_norm;
    if (fabsf(p_err) < CONF_PSF_CTRL_DEADBAND) {
        p_err = 0.0f;
    }

    int ff = (int)extruder_est_sps;
    int p  = (int)(p_err * (float)kp_window);
    int d  = (int)(-g_vel_norm_f * KD_PSF);

    int target = ff + p + d;

    /* Layer 2 Soft Walls */
    float pos_norm = buf_pos_norm();
    float abs_pos = fabsf(pos_norm);
    if (abs_pos > CONF_PSF_SOFT_WALL_START) {
        float wall = (abs_pos - CONF_PSF_SOFT_WALL_START) / (1.0f - CONF_PSF_SOFT_WALL_START);
        if (wall > 1.0f) wall = 1.0f;
        if (wall < 0.0f) wall = 0.0f;
        if (pos_norm > 0.0f) {
            /* COMPRESSION (+): blend toward 0 SPS to prevent overfeed */
            target = (int)((float)target * (1.0f - wall));
        } else {
            /* TENSION (-): blend toward max_sps to urge refill */
            target = (int)((float)target + (float)(max_sps - target) * wall);
        }
    }

    return clamp_i(target, 0, max_sps);
}

void sync_tick(uint32_t now_ms) {
    lane_t *A = lane_ptr(active_lane);
    if (!A || tc_state() != TC_IDLE || g_boot_stabilizing) return;

    if (BUF_SENSOR_TYPE == 1 && sync_enabled) {
        /* Gated to active sync: type-P rests at the tension rail (+1.0) whenever
           unloaded/idle/mid-tube, so this saturation catch must NOT run when the
           sync loop isn't driving — otherwise the resting home position trips
           sync_fault_hold() at idle and breaks a manual UL (the buffer is its own
           normal home, not a starvation fault). Over-tension is only a fault while
           actively syncing a loaded buffer. */
        /* 10.1: Velocity-triggered brake on compression slam. Compression is +
           under the new convention, so a slam toward the compression rail is a
           POSITIVE velocity spike. */
        if (g_vel_norm > CONF_PSF_JUMP_NORM_PER_S) {
            sync_fast_brake_until_ms = now_ms + CONF_PSF_STOP_CONFIRM_MS;
        }

        /* 10.2: Stop/slowdown classification during brake */
        bool fast_brake_active = sync_fast_brake_until_ms != 0 && (int32_t)(sync_fast_brake_until_ms - now_ms) > 0;
        if (fast_brake_active) {
            if (g_vel_norm < -0.1f) {
                /* Extruder resumed or buffer recovered (moving back off compression,
                   toward tension): clear brake, resume PD */
                sync_fast_brake_until_ms = 0;
            }
        } else if (sync_fast_brake_until_ms != 0 && (int32_t)(now_ms - sync_fast_brake_until_ms) >= 0) {
            /* Brake expired: check if pinned */
            sync_fast_brake_until_ms = 0;
            if (g_buf_pos >= 0.99f) {
                sync_relief_pause();
                sync_apply_to_active();
                cmd_event("SYNC", "RELIEF_PAUSE");
                return;
            }
        }

        /* 10.3: Saturation-sustained relief/fault triggers */
        if (g_buf_analog_saturated_since_ms != 0 &&
            (now_ms - g_buf_analog_saturated_since_ms) >= CONF_PSF_WALL_SAT_MS) {
            if (g_buf_pos >= 0.99f) {
                sync_relief_pause();
                sync_apply_to_active();
                cmd_event("SYNC", "RELIEF_PAUSE");
                return;
            } else if (g_buf_pos <= -0.99f) {
                sync_fault_hold();
                sync_apply_to_active();
                cmd_event("SYNC", "FAULT_HOLD");
                return;
            }
        }
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
            if (BUF_SENSOR_TYPE == 0) g_buf_pos = buf_target_reserve_mm();
            g_buf.state = BUF_NEUTRAL;
            g_buf.entered_ms = now_ms;
            sync_current_sps = sync_bootstrap_sps();
            sync_set_state(SYNC_ACTIVE);
            sync_auto_started = true;
            sync_tail_assist_active = !lane_in_present(A) && lane_out_present(A);
            sync_idle_since_ms = 0;
            cmd_event("SYNC", "FAULT_HOLD_RECOVERY");
            cmd_event("SYNC", "AUTO_START");
        } else {
            return;
        }
    } else if (g_sync_state == SYNC_RETRACT_ASSIST || g_sync_state == SYNC_RELIEF_PAUSE) {
        if (g_sync_state == SYNC_RETRACT_ASSIST) {
            if (g_bl_sub_state != BL_IDLE) {
                sync_buffer_lock_tick(A, now_ms);
            }
            return;
        } else {
            /* 11.1 Proactive recovery from RELIEF_PAUSE: if debouncing was bypassed or raced,
             * re-arm sync immediately once debounced state is NEUTRAL during active print
             * or TENSION during feed. */
            buf_state_t s = g_buf.state;
            if (BUF_SENSOR_TYPE == 0 &&
                (s == BUF_TENSION || (s == BUF_NEUTRAL && A->task == TASK_FEED))) {
                g_buf_pos = buf_target_reserve_mm();
                sync_current_sps = sync_bootstrap_sps();
                sync_set_state(SYNC_ACTIVE);
                sync_auto_started = true;
                sync_tail_assist_active = !lane_in_present(A) && lane_out_present(A);
                sync_idle_since_ms = 0;
                cmd_event("SYNC", "AUTO_START");
            } else {
                return;
            }
        }
    }

    buf_state_t s = g_buf.state;
    /* Block auto-start while a manual unload state machine is running (cut path).
       TASK_UNLOAD guards the non-cut path; this covers the TASK_IDLE window inside
       MANUAL_UNLOAD_WAIT_FIRST_CLEAR / WAIT_CUT before the state machine completes. */
    bool auto_start_allowed = (A->task == TASK_IDLE || A->task == TASK_FEED)
        && !manual_unload_active();

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
    bool is_tension_active = (BUF_SENSOR_TYPE == 1)
        ? ((g_buf_pos < -0.6f) && (g_sync_tension_transitioned || g_vel_norm < -0.1f))
        : (s == BUF_TENSION);
    if (AUTO_MODE && !sync_enabled && auto_start_allowed && is_tension_active &&
            !g_bl_autostart_suppressed &&
            any_lane_loaded && !both_loaded) {
        /* Auto-correct the active lane to the physically loaded one. The operator
           may have switched the UI selection to an unloaded lane (to inspect or
           eject) and left it there. Tension with filament at the hub (YS) means
           the loaded lane is feeding, so adopt whichever lane has its OUT sensor
           engaged before enabling sync — otherwise we would drive the wrong
           (empty) lane. Double-load is already excluded by the guard above. */
        if (on_al(&g_y_split)) {
            if (lane_out_present(&g_lane_l1) && active_lane != 1) {
                set_active_lane(1);
                A = lane_ptr(active_lane);
            } else if (lane_out_present(&g_lane_l2) && active_lane != 2) {
                set_active_lane(2);
                A = lane_ptr(active_lane);
            }
        }
        bool tail_assist = !lane_in_present(A) && lane_out_present(A);
        int startup_sps = sync_bootstrap_sps();
        sync_current_sps = startup_sps;
        sync_set_state(SYNC_ACTIVE);
        sync_auto_started = true;
        sync_tail_assist_active = tail_assist;
        sync_idle_since_ms = 0;
        cmd_event("SYNC", "AUTO_START");
    }

    if (!sync_enabled) return;

    if (sync_auto_started) {
        if (sync_tail_assist_active) {
            if (s != BUF_COMPRESSION) {
                sync_idle_since_ms = 0;
            } else {
                if (sync_idle_since_ms == 0) sync_idle_since_ms = now_ms;
                if (SYNC_AUTO_STOP_MS > 0 && (now_ms - sync_idle_since_ms) > (uint32_t)SYNC_AUTO_STOP_MS) {
                    sync_disable(true);
                    extruder_est_last_update_ms = now_ms;
                    sync_apply_to_active();
                    cmd_event("SYNC", "AUTO_STOP");
                    return;
                }
            }
        } else {
            sync_idle_since_ms = 0;
        }
    }

    if ((now_ms - sync_last_tick_ms) < (uint32_t)SYNC_TICK_MS) return;

    sync_last_tick_ms = now_ms;

    if (s == BUF_FAULT) {
        sync_current_sps = 0;
        sync_apply_to_active();
        cmd_event("BS", "FAULT,0");
        return;
    }

    if (A && (A->task == TASK_FEED || A->task == TASK_IDLE) && A->fault == FAULT_NONE && g_sync_state == SYNC_ACTIVE) {
        if (g_buf.mmu_sps_dwell_samples >= 10000) {
            g_buf.mmu_sps_dwell_sum /= 2;
            g_buf.mmu_sps_dwell_samples /= 2;
        }
        g_buf.mmu_sps_dwell_sum += (uint32_t)lane_motion_sps(A);
        g_buf.mmu_sps_dwell_samples++;
    }
    float raw_target = buf_target_reserve_mm();
    float reserve_deadband_mm = buf_virtual_deadband_mm();

    g_buf_pos_raw_status = g_buf_pos;
    /* Variance-aware position blend (default OFF) */
    if (BUF_VARIANCE_BLEND_FRAC > 0.0f && g_buf_sigma_mm > 0.0f) {
        float sigma_ref = (BUF_VARIANCE_BLEND_REF_MM > 0.05f)
                          ? BUF_VARIANCE_BLEND_REF_MM : 1.0f;
        float distrust = clamp_f(g_buf_sigma_mm / sigma_ref, 0.0f, 1.0f);
        float blend = distrust * BUF_VARIANCE_BLEND_FRAC;
        g_buf_pos = (1.0f - blend) * g_buf_pos + blend * raw_target;
    }

    /* Effective buffer position with drift correction (default OFF) */
    float bp_eff = g_buf_pos;
    float drift_correction_mm = 0.0f;
    int drift_min_samples = BUF_DRIFT_MIN_SAMPLES;
    if (drift_min_samples < 1) drift_min_samples = 1;
    bool drift_apply_gate = (BUF_DRIFT_APPLY_THR_MM > 0.0f)
        && (g_bp_drift_samples > 0)
        && (fabsf(g_bp_drift_ewma_mm) >= BUF_DRIFT_APPLY_THR_MM)
        && (g_buf_signal.confidence >= BUF_DRIFT_APPLY_MIN_CF);
    if (drift_apply_gate) {
        float sample_frac = clamp_f((float)g_bp_drift_samples / (float)drift_min_samples, 0.0f, 1.0f);
        drift_correction_mm = clamp_f(g_bp_drift_ewma_mm, -BUF_DRIFT_CLAMP_MM, BUF_DRIFT_CLAMP_MM) * sample_frac;
        float thr = buf_threshold_mm();
        float wall_taper_mm = reserve_deadband_mm * 2.0f;
        if (wall_taper_mm < 0.5f) wall_taper_mm = 0.5f;

        if (drift_correction_mm < 0.0f) {
            float dist_from_tension_mm = g_buf_pos + thr;
            float wall_frac = clamp_f(dist_from_tension_mm / wall_taper_mm, 0.0f, 1.0f);
            drift_correction_mm *= wall_frac;
        } else if (drift_correction_mm > 0.0f) {
            float dist_from_compression_mm = thr - g_buf_pos;
            float wall_frac = clamp_f(dist_from_compression_mm / wall_taper_mm, 0.0f, 1.0f);
            drift_correction_mm *= wall_frac;
        }

        bp_eff = g_buf_pos - drift_correction_mm;

        /* SAFETY: Don't let correction push bp_eff to the opposite side of physical state.
         * If we are physically at a wall, the controller must see it as at or beyond that wall. */
        if (s == BUF_COMPRESSION && bp_eff < thr) bp_eff = thr;
        else if (s == BUF_TENSION && bp_eff > -thr) bp_eff = -thr;

        /* CONFIDENCE BIAS: If we are uncertain, shift bp_eff toward the TENSION side.
         * This creates a gentle "feed pressure" that ensures we don't under-feed
         * while the model is drifting open-loop. This shift disappears as soon as
         * we hit a switch and restore confidence.
         * When blend is active (BLEND_FRAC > 0), confidence-bias is
         * redundant. Gate it off in that case. */
        if (BUF_VARIANCE_BLEND_FRAC <= 0.0f) {
            float uncertainty_shift_mm = (1.0f - g_buf_signal.confidence) * (thr * 0.8f);
            bp_eff -= uncertainty_shift_mm;
        }

        /* Clamp so correction cannot push bp_eff past the endstop zone boundary */
        bp_eff = clamp_f(bp_eff, -thr, thr);
    }
    g_bp_drift_correction_applied_mm = drift_correction_mm;

    /* Integral reserve centering — active only in BUF_NEUTRAL with adequate confidence */
    bool integral_active = (s == BUF_NEUTRAL)
        && (SYNC_RESERVE_INTEGRAL_GAIN > 0.0f)
        && (g_buf_signal.confidence >= 0.7f);
    if (integral_active) {
        float raw_error = bp_eff - raw_target;
        float dt_s = (float)SYNC_TICK_MS / 1000.0f;
        sync_reserve_integral_mm -= SYNC_RESERVE_INTEGRAL_GAIN * raw_error * dt_s;
        sync_reserve_integral_mm = clamp_f(sync_reserve_integral_mm,
            -SYNC_RESERVE_INTEGRAL_CLAMP_MM, +SYNC_RESERVE_INTEGRAL_CLAMP_MM);
    }
    /* TENSION_DWELL_WARN: integral saturated toward tension side — rate-limited 10 s */
    if (sync_enabled && SYNC_RESERVE_INTEGRAL_GAIN > 0.0f
        && sync_reserve_integral_mm < -(SYNC_RESERVE_INTEGRAL_CLAMP_MM * 0.5f)) {
        if (g_buf_tension_dwell_warn_emit_ms == 0 ||
            (now_ms - g_buf_tension_dwell_warn_emit_ms) >= 10000u) {
            g_buf_tension_dwell_warn_emit_ms = now_ms;
            cmd_event("SYNC", "TENSION_DWELL_WARN");
        }
    }
    float effective_target = raw_target + sync_reserve_integral_mm;

    float thr = buf_threshold_mm();
    if (thr < 0.001f) thr = 0.001f;
    float pos_norm = (BUF_SENSOR_TYPE == 1) ? g_buf_pos : (bp_eff / thr);
    float target_norm = (BUF_SENSOR_TYPE == 1) ? psf_goal_norm() : (effective_target / thr);
    float error_norm = pos_norm - target_norm;
    float deadband_norm = (BUF_SENSOR_TYPE == 1) ? 0.1f : (reserve_deadband_mm / thr);

    if (BUF_SENSOR_TYPE == 0) {
        type_d_neutral_feed_sample(s, pos_norm, target_norm, deadband_norm);
        relay_neutral_trim_leak(s, now_ms);
    }

    int target_sps;
    if (BUF_SENSOR_TYPE == 0) {
        target_sps = relay_control_law(s);
    } else {
        target_sps = psf_control_law(error_norm);
    }
    int type_d_neutral_relay_floor_sps = (BUF_SENSOR_TYPE == 0 && s == BUF_NEUTRAL && error_norm < -deadband_norm)
        ? target_sps : 0;

    /* RAMPING BIAS: If we don't know where we are, raise speed a little bit
     * until we touch compression. This probe speed (up to ~150mm/min) ensures
     * we gravitate toward the safe compression wall rather than drifting toward tension. */
    if (g_buf_signal.confidence < 1.0f && s == BUF_NEUTRAL) {
        float uncertainty = 1.0f - g_buf_signal.confidence;
        target_sps += (int)(uncertainty * 6.0f);
    }

    if (s == BUF_TENSION && sync_tension_pin_since_ms != 0) {
        uint32_t tension_dwell_ms = now_ms - sync_tension_pin_since_ms;
        /* RELAY: TENSION switch contact is the normal "buffer empty, refill"
         * signal, not a fault. Only fault-hold on tension dwell in analog
         * mode; the type-D relay catch-up path refills it. */
        if (BUF_SENSOR_TYPE != 0 &&
            SYNC_TENSION_DWELL_STOP_MS > 0 &&
            tension_dwell_ms >= (uint32_t)SYNC_TENSION_DWELL_STOP_MS) {
            sync_fault_hold();
            extruder_est_last_update_ms = now_ms;
            sync_apply_to_active();
            cmd_event("SYNC", "FAULT_HOLD");
            return;
        }
        if (SYNC_TENSION_RAMP_DELAY_MS > 0 &&
            tension_dwell_ms >= (uint32_t)SYNC_TENSION_RAMP_DELAY_MS) {
            int max_sps = sync_clamp_max_sps(SYNC_MAX_SPS);
            if (target_sps < max_sps) target_sps = max_sps;
        }
    }

    /* TENSION-risk density warning (warn-only, default threshold=4) */
    if (TENSION_RISK_THRESHOLD > 0) {
        int tpx = sync_tension_pin_window_count(now_ms);
        if (tpx >= TENSION_RISK_THRESHOLD) {
            if (g_tension_risk_emit_ms == 0 || (now_ms - g_tension_risk_emit_ms) >= 30000u) {
                g_tension_risk_emit_ms = now_ms;
                cmd_event("SYNC", "TENSION_RISK_HIGH");
            }
        }
    }

    target_sps = sync_apply_scaling(target_sps, target_norm, pos_norm);

    if (sync_post_compression_boost_until_ms != 0) {
        if ((int32_t)(sync_post_compression_boost_until_ms - now_ms) > 0) target_sps += PRE_RAMP_SPS;
        else sync_post_compression_boost_until_ms = 0;
    }

    int neutral_anti_tension_floor_sps = sync_neutral_anti_tension_floor_sps(
        s, A, error_norm, deadband_norm, type_d_neutral_relay_floor_sps, now_ms);
    if (neutral_anti_tension_floor_sps > 0 && target_sps < neutral_anti_tension_floor_sps) {
        target_sps = neutral_anti_tension_floor_sps;
    }

    if (sync_compression_recovery_active) {
        uint32_t compression_recovery_ms = now_ms - g_buf.entered_ms;
        int compression_floor_sps = sync_compression_floor_sps();
        int kp_window = sync_effective_kp_sps(s);
        int recovery_cap = (int)extruder_est_sps - kp_window;
        if (compression_recovery_ms > SYNC_COMPRESSION_COLLAPSE_DELAY_MS) {
            uint32_t collapse_ms = compression_recovery_ms - SYNC_COMPRESSION_COLLAPSE_DELAY_MS;
            if (collapse_ms > SYNC_COMPRESSION_COLLAPSE_CAP_MS) collapse_ms = SYNC_COMPRESSION_COLLAPSE_CAP_MS;
            int extra_trim = (int)(((uint64_t)collapse_ms * (uint64_t)(kp_window + PRE_RAMP_SPS)) /
                                   (uint64_t)SYNC_COMPRESSION_COLLAPSE_CAP_MS);
            recovery_cap -= extra_trim;
        }
        if (recovery_cap < compression_floor_sps) recovery_cap = compression_floor_sps;
        /* F1b: collapse braking is for an over-tensioned buffer draining
         * through COMPRESSION; while still in NEUTRAL it must not starve the
         * refill below the learned baseline. */
        if (s == BUF_NEUTRAL) {
            int neutral_floor = baseline_control_floor_sps();
            if (recovery_cap < neutral_floor) recovery_cap = neutral_floor;
        }
        if (target_sps > recovery_cap) target_sps = recovery_cap;
    }

    /* Type-D NEUTRAL relay output is the demand estimate plus the configured
     * lean. Preserve it only while reserve is tension-side; once at/above target,
     * shared shapers must be able to brake before the COMPRESSION switch. */
    if (type_d_neutral_relay_floor_sps > 0 && target_sps < type_d_neutral_relay_floor_sps) {
        target_sps = type_d_neutral_relay_floor_sps;
    }

    bool compression_wall_critical = false;
    if (BUF_SENSOR_TYPE == 0 && s == BUF_COMPRESSION) {
        float compression_push_mm_s = sync_compression_wall_velocity_mm_s(A);
        float compression_wall_ms = sync_compression_wall_time_ms(A);
        compression_wall_critical = compression_push_mm_s > SYNC_COMPRESSION_HARD_PUSH_MM_S &&
                                    compression_wall_ms < SYNC_COMPRESSION_HARD_WALL_MS;
    }
    /* RELAY: hitting the COMPRESSION switch is the normal "buffer full,
     * back off" control signal, not a jam. This critical-fault only ever
     * fired in type-D mode and was manufacturing the FAULT_HOLD ->
     * recovery-slam cycle. Suppress it in relay mode; the relay stop state
     * drains the full buffer off the compression wall. */
    if (compression_wall_critical && BUF_SENSOR_TYPE != 0) {
        sync_fault_hold();
        extruder_est_last_update_ms = now_ms;
        sync_apply_to_active();
        cmd_event("SYNC", "FAULT_HOLD");
        return;
    }

    bool fast_brake_active = sync_fast_brake_until_ms != 0 && (int32_t)(sync_fast_brake_until_ms - now_ms) > 0;
    if (!fast_brake_active && sync_fast_brake_until_ms != 0 && (int32_t)(now_ms - sync_fast_brake_until_ms) >= 0)
        sync_fast_brake_until_ms = 0;



    int max_sps = sync_clamp_max_sps(SYNC_MAX_SPS);
    if (fast_brake_active) target_sps = 0;
    /* Type-D COMPRESSION true-stop: don't let the SYNC_MIN clamp re-floor the 0,
     * so feed actually stops instead of pushing SYNC_MIN forward into a full
     * buffer (which deepened BP past the switch for ~5s at end of feed). */
    else if (BUF_SENSOR_TYPE == 0 && s == BUF_COMPRESSION) target_sps = 0;
    else target_sps = clamp_i(target_sps, SYNC_MIN_SPS, max_sps);

    int ramp_dn_sps = SYNC_RAMP_DN_SPS;
    if (!fast_brake_active && sync_compression_recovery_active && s == BUF_COMPRESSION) {
        uint32_t compression_recovery_ms = now_ms - g_buf.entered_ms;
        if (compression_recovery_ms > SYNC_COMPRESSION_COLLAPSE_DELAY_MS) {
            ramp_dn_sps *= SYNC_COMPRESSION_COLLAPSE_RAMP_MULT;
        }
    }

    if (fast_brake_active) { sync_current_sps = 0; g_psf_target_filt = 0.0f; }
    else if (BUF_SENSOR_TYPE == 1) {
        /* Type-P distance-based smoothing (Happy-Hare-style). Both the target
           EMA and the slew limit are keyed to filament distance moved this tick,
           not wall-clock — so feed changes scale with flow and go to zero when
           the printer is idle, instead of the old time-ramp (flow-blind, 2-tick
           bang-bang) or the direct apply (snaps to the noisy PD target instantly).
           "move" = extruder flow demand this tick (the system's distance clock). */
        uint8_t idx = (active_lane == 2) ? 1 : 0;
        float dt_s = (float)SYNC_TICK_MS / 1000.0f;
        float move = fabsf(extruder_est_sps) * MM_PER_STEP[idx] * dt_s;

        /* 1. EMA the target over distance: alpha = 1 - exp(-move/L). */
        float L = (SYNC_PSF_FILTER_MM > 0.01f) ? SYNC_PSF_FILTER_MM : 0.01f;
        float alpha = 1.0f - expf(-move / L);
        g_psf_target_filt += alpha * ((float)target_sps - g_psf_target_filt);

        /* 2. Slew-limit the applied rate, clamped so it never overshoots the
              filtered target (overshoot is what made the old ramp oscillate).
              Floor the step so a tiny creep is always allowed off idle. */
        float max_step = SYNC_PSF_SLEW_PER_MM * move;
        if (max_step < 1.0f) max_step = 1.0f;
        float cur = (float)sync_current_sps;
        if (g_psf_target_filt > cur + max_step)      cur += max_step;
        else if (g_psf_target_filt < cur - max_step) cur -= max_step;
        else                                          cur = g_psf_target_filt;

        /* Overfeed guard: the distance clock (move) freezes the filter when the
           extruder stops (move -> 0 => alpha, max_step -> 0), holding the feed at
           the pre-stop rate so the MMU keeps pushing into a stopped extruder and
           slams COMPRESSION. Feed *drops* are always safe (worst case is a brief
           tension excursion, which has room), so additionally let the feed fall
           toward the raw target on wall-clock, independent of flow. Take whichever
           path is lower and pin the filtered target so it can't re-drive the feed
           back up while demand stays low. UP is untouched (purely flow-keyed). */
        if (SYNC_PSF_DECAY_SPS_PER_S > 0.0f) {
            float wall_cur = cur - SYNC_PSF_DECAY_SPS_PER_S * dt_s;
            if (wall_cur < (float)target_sps) wall_cur = (float)target_sps;
            if (wall_cur < cur) {
                cur = wall_cur;
                if (g_psf_target_filt > cur) g_psf_target_filt = cur;
            }
        }
        sync_current_sps = (int)(cur + 0.5f);
    }
    else if (sync_current_sps > target_sps) {
        sync_current_sps -= ramp_dn_sps;
        if (sync_current_sps < target_sps) sync_current_sps = target_sps;
    } else if (sync_current_sps < target_sps) {
        sync_current_sps += SYNC_RAMP_UP_SPS;
        if (sync_current_sps > target_sps) sync_current_sps = target_sps;
    }



    sync_current_sps = clamp_i(sync_current_sps, 0, max_sps);

    if (sync_auto_started && !sync_tail_assist_active) {
        if (s == BUF_COMPRESSION) {
            // Start or maintain the continuous physical dwell timer
            if (sync_continuous_compression_since_ms == 0) {
                sync_continuous_compression_since_ms = now_ms;
            }
            
            uint32_t compression_dwell_ms = now_ms - sync_continuous_compression_since_ms;

            // Define a widened floor threshold to ignore PID hunting/noise
            int effective_floor_sps = sync_compression_floor_sps() + PRE_RAMP_SPS; 

            uint32_t floor_timeout_ms = (uint32_t)SYNC_AUTO_STOP_MS;

            if (floor_timeout_ms > 0 && compression_dwell_ms > floor_timeout_ms) {
                if (sync_current_sps <= effective_floor_sps) {
                    sync_relief_pause();
                    extruder_est_last_update_ms = now_ms;
                    sync_apply_to_active();
                    cmd_event("SYNC", "RELIEF_PAUSE");
                    return;
                }
            }
        } else {
            // ONLY reset the timer when the arm physically leaves the compression switch
            sync_continuous_compression_since_ms = 0;
        }
    }

    sync_apply_to_active();

    if ((now_ms - sync_last_evt_ms) >= 500u) {
        sync_last_evt_ms = now_ms;
        char ev[48];
        snprintf(ev, sizeof(ev), "%s,%.1f,%.2f",
                 buf_state_name(s),
                 (double)sps_to_mm_per_min(sync_current_sps),
                 (double)g_buf_pos);
        cmd_event("BS", ev);
    }
}

float sync_reserve_error_mm(void) {
    return g_buf_pos - buf_target_reserve_mm();
}

bool sync_is_positive_relaunch_damped(void) {
    if (sync_tail_assist_active) return false;
    // Never damp while the buffer is empty — refill must be unrestricted.
    if (g_buf.state == BUF_TENSION) return false;
    if (sync_recent_negative_until_ms == 0) return false;

    uint32_t now_ms = g_now_ms;
    // Window has expired naturally.
    if ((int32_t)(now_ms - sync_recent_negative_until_ms) >= 0) return false;

    // Release damping 300 ms before the window expires to avoid a sharp step.
    uint32_t window_start_ms = sync_recent_negative_until_ms - SYNC_RECENT_NEGATIVE_HOLD_MS;
    uint32_t elapsed_in_window = now_ms - window_start_ms;
    if (elapsed_in_window >= (uint32_t)(SYNC_RECENT_NEGATIVE_HOLD_MS - 300u)) return false;

    // If reserve error is clearly positive the buffer is already refilling —
    // stop damping so the correction can run at full strength.
    if (sync_reserve_error_mm() < -buf_virtual_deadband_mm() * 0.5f) return false;

    return true;
}

bool sync_is_tension_predicted(void) {
    return !sync_is_positive_relaunch_damped() && predict_tension_coming();
}

float sync_reserve_target_mm(void) {
    return buf_target_reserve_mm();
}

float sync_reserve_deadband_mm(void) {
    return buf_virtual_deadband_mm();
}

uint32_t sync_tension_dwell_ms(uint32_t now_ms) {
    if (sync_tension_pin_since_ms == 0 || g_buf.state != BUF_TENSION) return 0;
    return now_ms - sync_tension_pin_since_ms;
}

uint32_t sync_est_age_ms(uint32_t now_ms) {
    if (extruder_est_last_update_ms == 0) return 0;
    return now_ms - extruder_est_last_update_ms;
}

float sync_reserve_integral_get_mm(void) {
    return sync_reserve_integral_mm;
}

float sync_buf_sigma_mm(void) {
    return g_buf_sigma_mm;
}

float sync_bp_residual_last_mm(void) {
    return g_bp_residual_last_mm;
}

float sync_bp_drift_ewma_mm(void) {
    return g_bp_drift_ewma_mm;
}

int sync_bp_drift_samples(void) {
    return (int)g_bp_drift_samples;
}

int sync_tension_pin_window_count(uint32_t now_ms) {
    uint32_t window_ms = (TENSION_RISK_WINDOW_MS > 0) ? (uint32_t)TENSION_RISK_WINDOW_MS : 60000u;
    int count = 0;
    for (int i = 0; i < TENSION_PIN_WINDOW_LEN; i++) {
        if (g_tension_pin_ts[i] != 0 && (now_ms - g_tension_pin_ts[i]) < window_ms) {
            count++;
        }
    }
    return count;
}

float sync_bp_drift_correction_applied_mm(void) {
    return g_bp_drift_correction_applied_mm;
}

int sync_neutral_creep_sps(void) {
    return g_neutral_creep_sps;
}
