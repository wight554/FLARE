/// @file sync_buf.c
/// @brief Buffer sensing for both sensor types: Type-D dual-endstop state, Type-P
///        analog/Hall read, virtual dead-reckoned position, signal publishing, and
///        the switch-crossing extruder-demand estimator.
/// @details Type-D has no analog position, so position is integrated from net
///          (MMU - extruder) travel and re-anchored at switch crossings. See spec
///          sync-refactor / sync-feedback; consumed by sync.c, sync_relay.c, sync_analog.c.

#include "config.h"
#include "controller_shared.h"
#include "hardware/adc.h"
#include "motion.h"
#include "protocol.h"
#include "sync_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    NEUTRAL_CREEP_WARN_INTERVAL_MS = 5000u,
    ADC_PIN_BASE = 26,
    ADC_SAMPLE_COUNT = 4,
    ADC_AVERAGE_SHIFT = 2,
    BUF_HYST_DEBOUNCE_DIVISOR = 2,
    EFFECTIVE_DWELL_MIN_MS = 5,
    EST_LOW_CF_WARN_INTERVAL_MS = 5000u,
    ANALOG_SATURATION_CONFIDENCE_MS = 250u,
    DRIFT_SAMPLE_MAX = 65535u,
};

static const float AVERAGE_PAIR_DIVISOR_F = 2.0f;
static const float FULL_TRANSITION_SPAN_MULT = 2.0f;
static const float MIN_PHYSICAL_HALF_TRAVEL_MM = 1.0f;
static const float RESERVE_PCT_DIVISOR_F = 100.0f;
static const float FLOW_BIAS_MILLI_DIVISOR_F = 1000.0f;
static const float COMPRESSION_RESERVE_CAP_FRAC = 0.7f;
static const float TARGET_CENTER_GUARD_MM = 0.5f;
static const float VIRTUAL_DEADBAND_FRAC = 0.15f;
static const float VIRTUAL_DEADBAND_MIN_MM = 0.5f;
static const float VIRTUAL_DEADBAND_MAX_MM = 2.0f;
static const float TYPE_P_DEADBAND_NORM = 0.1f;
static const float TAPER_SPAN_MIN_NORM = 0.001f;
static const float ADC_FULL_SCALE_F = 4095.0f;
static const float ANALOG_SPAN_MIN_F = 0.001f;
static const float RELAY_TRIM_RESET_THRESHOLD_SPS = 100.0f;
static const float RELAY_TRIM_RESET_DAMP_FRAC = 0.25f;
static const float RELAY_TRIM_ZERO_THRESHOLD_SPS = 1.0f;
static const float ESTIMATOR_TRAVEL_EPSILON_MM = 0.001f;
static const float MM_PER_STEP_MIN = 1e-6f;
static const float NEUTRAL_DRAIN_DEMAND_SLOW_MARGIN = 1.15f;
static const float ESTIMATOR_NORM_MM_S = 30.0f;
static const float ESTIMATOR_LONG_TRAVEL_MULT = 1.5f;
static const float ESTIMATOR_LONG_TRAVEL_ALPHA_MIN = 0.5f;
static const float DRIFT_EWMA_FALLBACK_TAU_MS = 60000.0f;
static const float DRIFT_ALPHA_MIN = 0.02f;
static const float SIGMA_HARD_CAP_MIN_MM = 0.1f;
static const float SIGMA_HARD_CAP_FALLBACK_MM = 1.5f;
static const float TYPE_P_RAIL_NORM = 0.99f;
static const float ANALOG_SATURATED_CONFIDENCE = 0.5f;
static const float TYPE_P_ESTIMATOR_ALPHA = 0.1f;

// Define the global buffer/sensing variables
float g_buf_pos = 0.0f;
float g_buf_pos_prev = 0.0f;
float g_vel_norm = 0.0f;
float g_vel_norm_f = 0.0f;
float g_buf_pos_raw_status = 0.0f;
float g_buf_physical_entry_pos_mm = 0.0f;
uint32_t g_buf_pos_last_ms = 0;

float g_bp_residual_last_mm = 0.0f;
float g_bp_drift_ewma_mm = 0.0f;
uint16_t g_bp_drift_samples = 0;
uint32_t g_bp_drift_last_ms = 0;
float g_bp_drift_correction_applied_mm = 0.0f;
uint32_t g_tension_pin_ts[TENSION_PIN_WINDOW_LEN] = {0};
int g_tension_pin_ts_idx = 0;
uint32_t g_tension_risk_emit_ms = 0;

float g_buf_confidence = 1.0f;
uint32_t g_buf_last_transition_ms = 0;
uint32_t g_buf_analog_saturated_since_ms = 0;
uint32_t g_buf_analog_last_sample_ms = 0;

buf_tracker_t g_buf = {.state = BUF_NEUTRAL};
buf_signal_t g_buf_signal = {0};

buf_state_t g_buf_stable_state = BUF_NEUTRAL;
buf_state_t g_buf_pending_state = BUF_NEUTRAL;
uint32_t g_buf_pending_since_ms = 0;

float g_sync_refill_effort_mm = 0.0f;
float g_sync_relieve_effort_mm = 0.0f;

// ============================================================================
// Buffer geometry — thresholds, reserve targets, deadbands
// ============================================================================

const char *buf_state_name(buf_state_t s) {
    switch (s) {
    case BUF_NEUTRAL:
        return "NEUTRAL";
    case BUF_TENSION:
        return "TENSION";
    case BUF_COMPRESSION:
        return "COMPRESSION";
    case BUF_FAULT:
        return "FAULT";
    default:
        return "?";
    }
}

zone_event_t g_history[HISTORY_LEN] = {0};
int g_hist_idx = 0;

float buf_physical_half_travel_mm(void) {
    float physical_half =
        (g_buf_max_travel_mm > 0) ? ((float)g_buf_max_travel_mm * HALF_F) : g_buf_switch_span_half_mm;
    if (physical_half < MIN_PHYSICAL_HALF_TRAVEL_MM)
        physical_half = MIN_PHYSICAL_HALF_TRAVEL_MM;
    if (physical_half < g_buf_switch_span_half_mm)
        physical_half = g_buf_switch_span_half_mm;
    return physical_half;
}

float buf_threshold_mm(void) {
    float physical_half = buf_physical_half_travel_mm();
    float threshold = g_buf_switch_span_half_mm;
    if (threshold < MIN_PHYSICAL_HALF_TRAVEL_MM)
        threshold = MIN_PHYSICAL_HALF_TRAVEL_MM;
    if (threshold > physical_half)
        threshold = physical_half;
    return threshold;
}

float buf_target_reserve_mm(void) {
    float threshold = buf_threshold_mm();
    float pct = (float)g_sync_reserve_pct / RESERVE_PCT_DIVISOR_F;
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D) {
        return clamp_f(threshold * pct, 0.0f, threshold * COMPRESSION_RESERVE_CAP_FRAC);
    }

    float physical_half = buf_physical_half_travel_mm();
    flow_param_t fp = flow_param((int)g_extruder_est_sps);
    float schedule_bias = (float)fp.bias_milli / FLOW_BIAS_MILLI_DIVISOR_F;
    float bias = clamp_f(fmaxf(g_sync_compression_bias_frac, schedule_bias), 0.0f,
                         COMPRESSION_RESERVE_CAP_FRAC);
    float target = (threshold * pct);
    float center_guard_mm = threshold * SYNC_RESERVE_CENTER_GUARD_FRAC;

    if (pct > 0.0f)
        target += center_guard_mm;

    /* H1: holdable target. Cap the bias position contribution so the
     * buffer parks off the fault wall with room for frequent switch
     * crossings (keeps the estimator fresh). Remaining compression lean is
     * applied as a feed trim (H2), not parked depth. */
    float bias_pos = fminf(bias, SYNC_RESERVE_BIAS_POS_FRAC_CAP);
    target += bias_pos * threshold;

    float max_target = physical_half - TARGET_CENTER_GUARD_MM;
    if (target > max_target)
        target = max_target;
    if (target < -threshold)
        target = -threshold;
    return target;
}

float buf_virtual_deadband_mm(void) {
    float deadband = buf_threshold_mm() * VIRTUAL_DEADBAND_FRAC;
    if (deadband < VIRTUAL_DEADBAND_MIN_MM)
        deadband = VIRTUAL_DEADBAND_MIN_MM;
    if (deadband > VIRTUAL_DEADBAND_MAX_MM)
        deadband = VIRTUAL_DEADBAND_MAX_MM;
    return deadband;
}

// ============================================================================
// Virtual position model — Type-D dead-reckoned buffer position from net travel
// ============================================================================

void buf_anchor_virtual_position(buf_state_t old_state, buf_state_t new_state) {
    if (g_buf_sensor_type != BUF_SENSOR_TYPE_D)
        return;

    float threshold = buf_threshold_mm();
    buf_state_t anchor_state = new_state;
    if (new_state == BUF_NEUTRAL)
        anchor_state = old_state;

    if (anchor_state == BUF_TENSION)
        g_buf_pos = -threshold;
    else if (anchor_state == BUF_COMPRESSION)
        g_buf_pos = threshold;
    else
        g_buf_pos = 0.0f;
}

void buf_virtual_position_tick(lane_t *lane, uint32_t elapsed_ms) {
    if (g_buf_sensor_type != BUF_SENSOR_TYPE_D || elapsed_ms == 0)
        return;

    float threshold = buf_threshold_mm();
    float physical_half = buf_physical_half_travel_mm();
    if (!lane) {
        g_buf_pos = clamp_f(g_buf_pos, -physical_half, physical_half);
        return;
    }

    int idx = lane_to_idx(lane->lane_id);
    if (idx < 0 || idx >= NUM_LANES)
        idx = 0;

    bool tracking_motion = sync_enabled || g_tc_ctx.state == TC_RELOAD_APPROACH ||
                           g_tc_ctx.state == TC_RELOAD_FOLLOW ||
                           (lane->task == TASK_FEED && lane->fault == FAULT_NONE);
    if (tracking_motion) {
        float dt_s = (float)elapsed_ms / MS_PER_SECOND_F;
        float mmu_mm_s = (float)lane_motion_sps(lane) * g_mm_per_step[idx];
        float extruder_mm_s = g_extruder_est_sps * g_mm_per_step[idx];
        float net_delta = (mmu_mm_s - extruder_mm_s) * dt_s;
        g_buf_pos += net_delta;
        if (g_buf.state == BUF_NEUTRAL) {
            g_buf_pos_sigma_accum_mm += fabsf(net_delta);
        }
    }

    g_buf_pos = clamp_f(g_buf_pos, -physical_half, physical_half);
    if (g_buf.state == BUF_TENSION && g_buf_pos > -threshold)
        g_buf_pos = -threshold;
    else if (g_buf.state == BUF_COMPRESSION && g_buf_pos < threshold)
        g_buf_pos = threshold;
    else if (g_buf.state == BUF_NEUTRAL)
        g_buf_pos = clamp_f(g_buf_pos, -threshold, threshold);
}

float lane_motion_mm_s(lane_t *lane) {
    if (!lane)
        return 0.0f;
    int idx = lane_to_idx(lane->lane_id);
    if (idx < 0 || idx >= NUM_LANES)
        idx = 0;
    return (float)lane_motion_sps(lane) * g_mm_per_step[idx];
}

float extruder_motion_mm_s(lane_t *lane) {
    if (!lane)
        return 0.0f;
    int idx = lane_to_idx(lane->lane_id);
    if (idx < 0 || idx >= NUM_LANES)
        idx = 0;
    return g_extruder_est_sps * g_mm_per_step[idx];
}

// ============================================================================
// Compression wall, neutral creep & demand scaling
// ============================================================================

float sync_compression_wall_remaining_mm(void) {
    if (g_buf_signal.kind != BUF_SRC_VIRTUAL_ENDSTOP)
        return 0.0f;
    float remaining = buf_physical_half_travel_mm() - g_buf_pos;
    if (remaining < 0.0f)
        remaining = 0.0f;
    return remaining;
}

int sync_compression_floor_sps(void) {
    return (g_sync_min_sps > g_compression_sps) ? g_sync_min_sps : g_compression_sps;
}

void neutral_creep_update(buf_state_t s, lane_t *lane, uint32_t now_ms) {
    /* D5/relay-buffer-control-2switch 7.2-A: keep neutral_creep as
     * intended-inert telemetry; fallback relay control ignores it. */
    if (g_sync_state != SYNC_ACTIVE || g_neutral_creep_timeout_ms == 0 ||
        g_neutral_creep_rate_sps_per_s == 0) {
        g_neutral_creep_sps = 0;
        return;
    }

    // Only active in NEUTRAL.
    if (s == BUF_TENSION || s == BUF_COMPRESSION)
        return;

    // Seed on boot/start if we missed a TENSION edge.
    if (g_neutral_creep_last_tension_ms == 0) {
        g_neutral_creep_last_tension_ms = now_ms;
    }

    uint32_t dwell_ms = now_ms - g_neutral_creep_last_tension_ms;
    if (dwell_ms > (uint32_t)g_neutral_creep_timeout_ms) {
        uint32_t active_ms = dwell_ms - g_neutral_creep_timeout_ms;
        float active_s = (float)active_ms / MS_PER_SECOND_F;

        int max_creep =
            (int)((g_extruder_est_sps * (float)g_neutral_creep_cap_frac) / RESERVE_PCT_DIVISOR_F);
        int raw_creep = (int)(active_s * (float)g_neutral_creep_rate_sps_per_s);

        if (raw_creep >= max_creep) {
            g_neutral_creep_sps = max_creep;
            static uint32_t cap_warn_ms = 0;
            if (now_ms - cap_warn_ms >= NEUTRAL_CREEP_WARN_INTERVAL_MS) {
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

int sync_apply_scaling(int base_sps, float target_norm, float pos_norm) {
    int target = base_sps;

    float deadband_norm = (g_buf_sensor_type == BUF_SENSOR_TYPE_P) ? TYPE_P_DEADBAND_NORM
                                                 : (buf_virtual_deadband_mm() / buf_threshold_mm());

    /* Overfeed taper: as the buffer pushes past goal toward the COMPRESSION rail
       (+1 under the new convention), taper feed down toward the compression floor
       so it doesn't slam the wall. Fires on the compression side (pos > goal). */
    if (pos_norm > (target_norm + deadband_norm)) {
        float taper_start = target_norm + deadband_norm;
        float taper_end = 1.0f;
        float taper_span = taper_end - taper_start;

        if (target > g_compression_sps && taper_span > TAPER_SPAN_MIN_NORM) {
            float overfill = pos_norm - taper_start;
            float taper_frac = clamp_f(overfill / taper_span, 0.0f, 1.0f);
            int taper_floor_sps = g_compression_sps;
            if (g_buf.state == BUF_NEUTRAL) {
                taper_frac *= SYNC_NEUTRAL_COMPRESSION_TAPER_FRAC;
                int dynamic_neutral_floor =
                    (int)(g_extruder_est_sps * SYNC_NEUTRAL_COMPRESSION_FLOOR_FRAC);
                if (dynamic_neutral_floor > taper_floor_sps)
                    taper_floor_sps = dynamic_neutral_floor;
            }
            float tapered = (float)target - ((float)(target - g_compression_sps) * taper_frac);
            if (tapered < (float)taper_floor_sps)
                tapered = (float)taper_floor_sps;
            target = (int)tapered;
        }
    }

    return target;
}

// ============================================================================
// Zone-dwell history & tension prediction
// ============================================================================

void history_push(buf_state_t zone, uint32_t dwell_ms) {
    g_history[g_hist_idx].zone = zone;
    g_history[g_hist_idx].dwell_ms = dwell_ms;
    g_hist_idx = (g_hist_idx + 1) % HISTORY_LEN;
}

bool predict_tension_coming(void) {
    int neutral_count = 0;
    int short_count = 0;

    for (int i = 0; i < HISTORY_LEN; i++) {
        if (g_history[i].zone == BUF_NEUTRAL && g_history[i].dwell_ms > 0) {
            neutral_count++;
            if (g_history[i].dwell_ms < (uint32_t)g_buf_predict_thr_ms) {
                short_count++;
            }
        }
    }
    return neutral_count > 0 && (short_count * 2 >= neutral_count);
}

// ============================================================================
// Analog read & normalized position/target (Type-P)
// ============================================================================

void buf_analog_update(uint32_t elapsed_ms) {
    g_buf_analog_last_sample_ms = to_ms_since_boot(get_absolute_time());
    adc_select_input(PIN_PSF - ADC_PIN_BASE);
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLE_COUNT; i++)
        sum += adc_read();
    float fraction = (float)(sum >> ADC_AVERAGE_SHIFT) / ADC_FULL_SCALE_F;
    g_buf_pos_raw_status = fraction;

    bool reversed = (g_buf_psf_max_comp < g_buf_psf_max_tens);
    float neutral = g_buf_psf_neutral;
    float norm = 0.0f;

    if (reversed) {
        float d_comp = neutral - g_buf_psf_max_comp;
        float d_tens = g_buf_psf_max_tens - neutral;
        if (d_comp < ANALOG_SPAN_MIN_F)
            d_comp = ANALOG_SPAN_MIN_F;
        if (d_tens < ANALOG_SPAN_MIN_F)
            d_tens = ANALOG_SPAN_MIN_F;
        norm =
            (fraction <= neutral) ? -(neutral - fraction) / d_comp : (fraction - neutral) / d_tens;
    } else {
        float d_comp = g_buf_psf_max_comp - neutral;
        float d_tens = neutral - g_buf_psf_max_tens;
        if (d_comp < ANALOG_SPAN_MIN_F)
            d_comp = ANALOG_SPAN_MIN_F;
        if (d_tens < ANALOG_SPAN_MIN_F)
            d_tens = ANALOG_SPAN_MIN_F;
        norm =
            (fraction >= neutral) ? -(fraction - neutral) / d_comp : (neutral - fraction) / d_tens;
    }
    norm = clamp_f(-norm, -1.0f, 1.0f);

    g_buf_pos = g_buf_analog_alpha * norm + (1.0f - g_buf_analog_alpha) * g_buf_pos;

    uint32_t dt_ms = (elapsed_ms == 0) ? 1 : elapsed_ms;
    float dt_s = (float)dt_ms / MS_PER_SECOND_F;
    if (dt_s < ANALOG_SPAN_MIN_F)
        dt_s = ANALOG_SPAN_MIN_F;
    float vel = (g_buf_pos - g_buf_pos_prev) / dt_s;
    g_vel_norm = vel;

    float vel_alpha = CONF_PSF_VEL_ALPHA;
    g_vel_norm_f = vel_alpha * vel + (1.0f - vel_alpha) * g_vel_norm_f;
    g_buf_pos_prev = g_buf_pos;
}

float psf_goal_norm(void) {
    /* BL-implied override: while a buffer-lock is in effect (until BS/timeout),
       park the goal at the armed rail so the idle stabilize and PD both pull the
       buffer to the side that leaves room for the locked operation. */
    if (g_bl_goal_override == BUF_TENSION)
        return -1.0f;
    if (g_bl_goal_override == BUF_COMPRESSION)
        return 1.0f;

    float goal_norm = 0.0f;
    bool reversed = (g_buf_psf_max_comp < g_buf_psf_max_tens);
    float goal_raw = g_buf_goal;

    if (reversed) {
        float d_comp = g_buf_psf_neutral - g_buf_psf_max_comp;
        float d_tens = g_buf_psf_max_tens - g_buf_psf_neutral;
        if (d_comp < ANALOG_SPAN_MIN_F)
            d_comp = ANALOG_SPAN_MIN_F;
        if (d_tens < ANALOG_SPAN_MIN_F)
            d_tens = ANALOG_SPAN_MIN_F;
        goal_norm = (goal_raw <= g_buf_psf_neutral) ? -(g_buf_psf_neutral - goal_raw) / d_comp
                                                  : (goal_raw - g_buf_psf_neutral) / d_tens;
    } else {
        float d_comp = g_buf_psf_max_comp - g_buf_psf_neutral;
        float d_tens = g_buf_psf_neutral - g_buf_psf_max_tens;
        if (d_comp < ANALOG_SPAN_MIN_F)
            d_comp = ANALOG_SPAN_MIN_F;
        if (d_tens < ANALOG_SPAN_MIN_F)
            d_tens = ANALOG_SPAN_MIN_F;
        goal_norm = (goal_raw >= g_buf_psf_neutral) ? -(goal_raw - g_buf_psf_neutral) / d_comp
                                                  : (g_buf_psf_neutral - goal_raw) / d_tens;
    }
    return clamp_f(-goal_norm, -1.0f, 1.0f);
}

float buf_pos_norm(void) {
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
        return g_buf_pos;
    } else {
        float thr = buf_threshold_mm();
        if (thr < ANALOG_SPAN_MIN_F)
            thr = ANALOG_SPAN_MIN_F;
        return clamp_f(g_buf_pos / thr, -1.0f, 1.0f);
    }
}

float buf_target_norm(void) {
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
        return psf_goal_norm();
    } else {
        float thr = buf_threshold_mm();
        if (thr < ANALOG_SPAN_MIN_F)
            thr = ANALOG_SPAN_MIN_F;
        return clamp_f(buf_target_reserve_mm() / thr, -1.0f, 1.0f);
    }
}

buf_state_t buf_state_raw(void) {
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
        float goal_norm = psf_goal_norm();
        const float deadband = 0.1f;
        if (g_buf_pos < goal_norm - deadband)
            return BUF_TENSION;
        if (g_buf_pos > goal_norm + deadband)
            return BUF_COMPRESSION;
        return BUF_NEUTRAL;
    }

    bool tension = on_al(&g_buf_tension_din);
    bool compression = on_al(&g_buf_compression_din);

    if (tension && compression)
        return BUF_FAULT;
    if (tension)
        return BUF_TENSION;
    if (compression)
        return BUF_COMPRESSION;
    return BUF_NEUTRAL;
}

buf_state_t buf_read_stable(uint32_t now_ms) {
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

    if ((now_ms - g_buf_pending_since_ms) >= (uint32_t)g_buf_hyst_ms) {
        /* G2(b): never gate the egress flip from a zero-feed state.
         * Type-D COMPRESSION commands feed rate of 0, so no motor travel
         * accrues there; gating the flip OUT of COMPRESSION on
         * g_relay_flip_travel_since_mm deadlocks the relay (cannot leave
         * a stopped state because leaving it is what produces the travel
         * the guard demands). The distance hysteresis applies only to
         * actuator-moving transitions (NEUTRAL<->TENSION). */
        if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && g_relay_min_flip_mm > 0.0f && raw != BUF_FAULT &&
            g_buf_stable_state != BUF_COMPRESSION &&
            g_relay_flip_travel_since_mm < g_relay_min_flip_mm) {
            return g_buf_stable_state;
        }
        g_buf_stable_state = g_buf_pending_state;
        g_buf_pending_since_ms = 0;
    }
    return g_buf_stable_state;
}

void buf_force_stable_state(buf_state_t state, uint32_t now_ms) {
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
            g_extruder_est_sps = 0.0f;
            g_extruder_est_prev_sps = 0.0f;
            g_extruder_est_last_update_ms = now_ms;
        }
    }
}

// ============================================================================
// Extruder-demand estimator — infer demand from switch-crossing travel (Type-D)
// ============================================================================

float clamp_est_sps(float est_sps) {
    float max_est_sps = (float)g_global_max_sps;
    if (est_sps < 0.0f)
        return 0.0f;
    if (est_sps > max_est_sps)
        return max_est_sps;
    return est_sps;
}

void blend_extruder_est_sps(float sample_sps, float alpha, uint32_t now_ms) {
    sample_sps = clamp_est_sps(sample_sps);
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && sample_sps > g_extruder_est_sps) {
        alpha = g_sync_est_attack_alpha;
    } else {
        alpha = clamp_f(alpha, g_est_alpha_min, g_est_alpha_max);
    }
    g_extruder_est_sps = alpha * sample_sps + (1.0f - alpha) * g_extruder_est_sps;
    g_extruder_est_last_update_ms = now_ms;
}

void blend_extruder_est_sps_direct(float sample_sps, float alpha, uint32_t now_ms) {
    sample_sps = clamp_est_sps(sample_sps);
    alpha = clamp_f(alpha, 0.0f, 1.0f);
    g_extruder_est_sps = alpha * sample_sps + (1.0f - alpha) * g_extruder_est_sps;
    g_extruder_est_last_update_ms = now_ms;
}

static float buf_update_transition_geometry(buf_state_t old, buf_state_t new_state, float threshold,
                                            float max_transition_mm) {
    float travel_mm = 0.0f;
    if (old == BUF_NEUTRAL) {
        if (new_state == BUF_TENSION)
            travel_mm = -threshold - g_buf_physical_entry_pos_mm;
        else if (new_state == BUF_COMPRESSION)
            travel_mm = threshold - g_buf_physical_entry_pos_mm;
    } else if (old == BUF_TENSION) {
        if (new_state == BUF_NEUTRAL)
            travel_mm = threshold;
        else if (new_state == BUF_COMPRESSION)
            travel_mm = max_transition_mm;
    } else if (old == BUF_COMPRESSION) {
        if (new_state == BUF_NEUTRAL)
            travel_mm = -threshold;
        else if (new_state == BUF_TENSION)
            travel_mm = -max_transition_mm;
    }

    travel_mm = clamp_f(travel_mm, -max_transition_mm, max_transition_mm);

    if (new_state == BUF_TENSION) {
        g_buf_physical_entry_pos_mm = -threshold;
    } else if (new_state == BUF_COMPRESSION) {
        g_buf_physical_entry_pos_mm = threshold;
    } else if (new_state == BUF_NEUTRAL) {
        if (old == BUF_TENSION)
            g_buf_physical_entry_pos_mm = -threshold;
        else if (old == BUF_COMPRESSION)
            g_buf_physical_entry_pos_mm = threshold;
    }
    return travel_mm;
}

static void buf_apply_estimator_sample(bool neutral_drain_sample, float est_sps, float alpha,
                                       uint32_t now_ms) {
    float prev_est_sps = g_extruder_est_sps;
    if (neutral_drain_sample) {
        blend_extruder_est_sps_direct(est_sps, alpha, now_ms);
        if (g_extruder_est_sps < prev_est_sps)
            g_extruder_est_sps = prev_est_sps;
    } else {
        blend_extruder_est_sps(est_sps, alpha, now_ms);
    }
    float reset_threshold_sps =
        fmaxf((float)g_sync_relay_trim_step_sps, RELAY_TRIM_RESET_THRESHOLD_SPS);
    if (fabsf(g_extruder_est_sps - prev_est_sps) >= reset_threshold_sps) {
        g_relay_neutral_trim_sps *= RELAY_TRIM_RESET_DAMP_FRAC;
        if (fabsf(g_relay_neutral_trim_sps) < RELAY_TRIM_ZERO_THRESHOLD_SPS)
            g_relay_neutral_trim_sps = 0.0f;
        relay_neutral_trim_clamp();
    }
}

static void buf_update_estimator(buf_state_t old, buf_state_t new_state, float travel_mm,
                                 float threshold, float prev_dwell, float mmu_avg_sps,
                                 uint32_t now_ms) {
    bool neutral_fill_sample = (old == BUF_NEUTRAL && new_state == BUF_COMPRESSION);
    bool neutral_drain_sample = (old == BUF_NEUTRAL && new_state == BUF_TENSION);
    bool compression_drain_sample = (old == BUF_COMPRESSION && new_state == BUF_NEUTRAL);
    bool has_known_travel = fabsf(travel_mm) > ESTIMATOR_TRAVEL_EPSILON_MM;
    float hyst_ms = (float)g_buf_hyst_ms;
    float hyst_debounce_ms = hyst_ms / (float)BUF_HYST_DEBOUNCE_DIVISOR;

    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D &&
        (neutral_fill_sample || neutral_drain_sample || compression_drain_sample) &&
        prev_dwell > hyst_ms) {
        uint32_t effective_dwell = (uint32_t)(prev_dwell - hyst_debounce_ms);
        if (effective_dwell < EFFECTIVE_DWELL_MIN_MS)
            effective_dwell = EFFECTIVE_DWELL_MIN_MS;
        if (has_known_travel) {
            g_buf.arm_vel_mm_s = travel_mm / ((float)effective_dwell / MS_PER_SECOND_F);
        }

        int idx = g_buf.lane_idx_at_entry;
        if (idx < 0 || idx >= NUM_LANES)
            idx = 0;
        float mm_per_step = g_mm_per_step[idx];
        if (mm_per_step <= MM_PER_STEP_MIN)
            return;

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
                        if (est_sps < (float)g_sync_min_sps)
                            est_sps = (float)g_sync_min_sps;
                        sample_valid = type_d_sample_demand_bounds(&est_sps);
                    }
                } else {
                    est_sps = feed_avg_sps;
                    sample_valid = type_d_sample_demand_bounds(&est_sps);
                }
                alpha = clamp_f((float)prev_dwell / SYNC_NEUTRAL_FILL_EST_ALPHA_DWELL_MS,
                                g_est_alpha_min, g_est_alpha_max);
            }
        } else if (neutral_drain_sample) {
            float feed_avg_sps = 0.0f;
            if (prev_dwell >= SYNC_NEUTRAL_FILL_MIN_DWELL_MS &&
                type_d_neutral_feed_avg_sps(&feed_avg_sps)) {
                float v_mm_s = fabsf(g_buf.arm_vel_mm_s);
                float v_norm = clamp_f(v_mm_s / g_sync_tension_fast_mm_s, 0.0f, 1.0f);
                float drain_sps = has_known_travel ? (v_mm_s / mm_per_step) : 0.0f;
                float demand_slow = feed_avg_sps * NEUTRAL_DRAIN_DEMAND_SLOW_MARGIN;
                float demand_fast = feed_avg_sps + drain_sps;
                est_sps = demand_slow + v_norm * (demand_fast - demand_slow);
                sample_valid = type_d_sample_demand_bounds(&est_sps);
                alpha = g_est_alpha_max + v_norm * (1.0f - g_est_alpha_max);
            }
        } else if (compression_drain_sample) {
            float drain_feed_sps = mmu_avg_sps;
            if (prev_dwell >= SYNC_DRAIN_EST_MIN_DWELL_MS &&
                (drain_feed_sps <= (float)g_sync_min_sps || g_sync_current_sps <= g_sync_min_sps)) {
                float mmu_mm_s = drain_feed_sps * mm_per_step;
                float extruder_mm_s = mmu_mm_s - g_buf.arm_vel_mm_s;
                est_sps = extruder_mm_s / mm_per_step;
                sample_valid = type_d_sample_demand_bounds(&est_sps);
                alpha = clamp_f((float)effective_dwell / SYNC_DRAIN_EST_ALPHA_DWELL_MS,
                                g_est_alpha_min, g_est_alpha_max);
            }
        } else {
            float mmu_mm_s = mmu_avg_sps * mm_per_step;
            float extruder_mm_s = mmu_mm_s - g_buf.arm_vel_mm_s;
            est_sps = extruder_mm_s / mm_per_step;

            alpha = clamp_f(fabsf(g_buf.arm_vel_mm_s) / ESTIMATOR_NORM_MM_S, g_est_alpha_min,
                            g_est_alpha_max);

            if (fabsf(travel_mm) > threshold * ESTIMATOR_LONG_TRAVEL_MULT &&
                alpha < ESTIMATOR_LONG_TRAVEL_ALPHA_MIN) {
                alpha = ESTIMATOR_LONG_TRAVEL_ALPHA_MIN;
            }
            sample_valid = type_d_sample_demand_bounds(&est_sps);
        }

        if (sample_valid) {
            buf_apply_estimator_sample(neutral_drain_sample, est_sps, alpha, now_ms);
        }
    }
}

static void buf_update_residual_observer(buf_state_t old, buf_state_t new_state, float threshold,
                                         uint32_t now_ms) {
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && old == BUF_NEUTRAL &&
        (new_state == BUF_TENSION || new_state == BUF_COMPRESSION)) {
        float switch_pos_mm = (new_state == BUF_TENSION) ? -threshold : threshold;
        float residual = g_buf_pos - switch_pos_mm;
        g_bp_residual_last_mm = residual;

        float tau_ms_f =
            (g_buf_drift_ewma_tau_ms > 0) ? (float)g_buf_drift_ewma_tau_ms : DRIFT_EWMA_FALLBACK_TAU_MS;
        float alpha;
        if (g_bp_drift_samples == 0 || g_bp_drift_last_ms == 0) {
            alpha = 1.0f;
        } else {
            uint32_t dt = now_ms - g_bp_drift_last_ms;
            alpha = 1.0f - expf(-(float)dt / tau_ms_f);
            if (alpha < DRIFT_ALPHA_MIN)
                alpha = DRIFT_ALPHA_MIN;
            if (alpha > 1.0f)
                alpha = 1.0f;
        }
        g_bp_drift_ewma_mm = alpha * residual + (1.0f - alpha) * g_bp_drift_ewma_mm;
        g_bp_drift_last_ms = now_ms;
        if (g_bp_drift_samples < DRIFT_SAMPLE_MAX)
            g_bp_drift_samples++;
    }
}

static void buf_update_trim_and_floor(buf_state_t old, buf_state_t new_state) {
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && old == BUF_NEUTRAL && new_state == BUF_TENSION &&
        g_sync_relay_trim_step_sps > 0 && g_sync_relay_trim_clamp_sps > 0) {
        g_relay_neutral_trim_sps += (float)g_sync_relay_trim_step_sps;
        relay_neutral_trim_clamp();
    }

    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && old == BUF_NEUTRAL && new_state == BUF_TENSION) {
        if ((float)g_extruder_est_sps > g_tension_floor_sps)
            g_tension_floor_sps = (float)g_extruder_est_sps;
        if (g_tension_floor_sps > (float)g_sync_tension_probe_max_sps)
            g_tension_floor_sps = (float)g_sync_tension_probe_max_sps;
    }
}

// ============================================================================
// Buffer state update & signal publish — per-tick sensing entry points
// ============================================================================

void buf_update(buf_state_t new_state, uint32_t now_ms) {
    if (new_state == g_buf.state)
        return;

    uint32_t prev_dwell = now_ms - g_buf.entered_ms;
    g_buf.dwell_ms = prev_dwell;
    lane_t *lane = lane_ptr(g_active_lane);
    int mmu_now_sps = lane_motion_sps(lane);

    float mmu_avg_sps = 0.0f;
    if (g_buf.mmu_sps_dwell_samples > 0) {
        mmu_avg_sps = (float)g_buf.mmu_sps_dwell_sum / (float)g_buf.mmu_sps_dwell_samples;
    } else {
        mmu_avg_sps = (float)(g_buf.mmu_sps_at_entry + mmu_now_sps) / AVERAGE_PAIR_DIVISOR_F;
    }

    buf_state_t old = g_buf.state;
    float threshold = buf_threshold_mm();
    float max_transition_mm = threshold * FULL_TRANSITION_SPAN_MULT;

    g_buf.arm_vel_mm_s = 0.0f;

    float travel_mm = buf_update_transition_geometry(old, new_state, threshold, max_transition_mm);

    buf_update_estimator(old, new_state, travel_mm, threshold, (float)prev_dwell, mmu_avg_sps,
                         now_ms);

    buf_update_residual_observer(old, new_state, threshold, now_ms);

    buf_update_trim_and_floor(old, new_state);

    history_push(g_buf.state, prev_dwell);
    g_buf.state = new_state;
    g_buf.entered_ms = now_ms;
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && new_state == BUF_NEUTRAL)
        type_d_neutral_feed_reset();
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

    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && g_sync_state == SYNC_RELIEF_PAUSE && !g_boot_stabilizing &&
        (new_state == BUF_NEUTRAL || new_state == BUF_TENSION)) {
        sync_rearm_active(lane, now_ms);
    }

    buf_anchor_virtual_position(old, new_state);

    g_buf.lane_idx_at_entry = (g_active_lane == 2) ? 1 : 0;
    g_buf.mmu_sps_at_entry = mmu_now_sps;
    g_buf.mmu_sps_dwell_sum = 0;
    g_buf.mmu_sps_dwell_samples = 0;
}

void buf_signal_publish(uint32_t now_ms) {
    float half = buf_physical_half_travel_mm();
    buf_source_kind_t kind;
    float norm;
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D) {
        kind = BUF_SRC_VIRTUAL_ENDSTOP;
        norm = (half > ANALOG_SPAN_MIN_F) ? (g_buf_pos / half) : 0.0f;
        norm = clamp_f(norm, -1.0f, 1.0f);
        {
            float sigma_cap = (g_est_sigma_hard_cap_mm > SIGMA_HARD_CAP_MIN_MM)
                                  ? g_est_sigma_hard_cap_mm
                                  : SIGMA_HARD_CAP_FALLBACK_MM;
            g_buf_sigma_mm = sqrtf(g_buf_pos_sigma_accum_mm) * ENDSTOP_PER_UNIT_SIGMA_MM;
            if (g_buf_sigma_mm > sigma_cap)
                g_buf_sigma_mm = sigma_cap;
            g_buf_confidence = clamp_f(1.0f - g_buf_sigma_mm / sigma_cap, 0.0f, 1.0f);
            if (g_buf_confidence == 0.0f && !g_buf_est_fallback_emitted) {
                g_buf_est_fallback_emitted = true;
                g_sync_reserve_integral_mm = 0.0f;
                cmd_event("BUF", "EST_FALLBACK");
                g_bp_drift_ewma_mm = 0.0f;
                g_bp_drift_samples = 0;
                g_bp_drift_last_ms = 0;
                g_bp_drift_correction_applied_mm = 0.0f;
                cmd_event("BUF", "DRIFT_RESET");
            }
            if (sync_enabled && g_buf_confidence < g_est_low_cf_warn_threshold) {
                if (g_buf_est_low_cf_emit_ms == 0 ||
                    (now_ms - g_buf_est_low_cf_emit_ms) >= EST_LOW_CF_WARN_INTERVAL_MS) {
                    g_buf_est_low_cf_emit_ms = now_ms;
                    cmd_event("BUF", "EST_LOW_CF");
                }
            } else {
                g_buf_est_low_cf_emit_ms = 0;
            }
        }
        g_buf_analog_saturated_since_ms = 0;
    } else {
        kind = BUF_SRC_ANALOG;
        norm = g_buf_pos;
        if (fabsf(norm) >= TYPE_P_RAIL_NORM) {
            if (g_buf_analog_saturated_since_ms == 0)
                g_buf_analog_saturated_since_ms = now_ms;
            if ((now_ms - g_buf_analog_saturated_since_ms) > ANALOG_SATURATION_CONFIDENCE_MS)
                g_buf_confidence = ANALOG_SATURATED_CONFIDENCE;
        } else {
            g_buf_analog_saturated_since_ms = 0;
            g_buf_confidence = 1.0f;
        }
    }
    g_buf_signal.pos_norm = norm;
    g_buf_signal.pos_mm = (g_buf_sensor_type == BUF_SENSOR_TYPE_D) ? g_buf_pos : (norm * half);
    g_buf_signal.confidence = g_buf_confidence;
    g_buf_signal.zone = g_buf.state;
    g_buf_signal.kind = kind;
    g_buf_signal.fault = (g_buf.state == BUF_FAULT);
}

void buf_sensor_tick(uint32_t now_ms) {
    uint32_t elapsed_ms = now_ms - g_buf_pos_last_ms;
    bool do_pos = elapsed_ms >= (uint32_t)g_sync_tick_ms;
    if (do_pos)
        g_buf_pos_last_ms = now_ms;

    if (g_buf_sensor_type == BUF_SENSOR_TYPE_P && do_pos)
        buf_analog_update(elapsed_ms);

    buf_state_t prev = g_buf.state;
    buf_state_t s = buf_read_stable(now_ms);
    if (s != prev) {
        buf_update(s, now_ms);
        sync_on_transition(prev, s, now_ms);
    }
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_D && do_pos) {
        buf_virtual_position_tick(lane_ptr(g_active_lane), elapsed_ms);
        g_buf_analog_last_sample_ms = now_ms;
    }

    if (do_pos) {
        lane_t *lane = lane_ptr(g_active_lane);
        if (lane) {
            uint8_t idx = (g_active_lane == 2) ? 1 : 0;
            float delta_mm = (float)lane_motion_sps(lane) * ((float)elapsed_ms / MS_PER_SECOND_F) *
                             g_mm_per_step[idx];
            g_sync_mmu_total_mm += delta_mm;
            g_relay_flip_travel_since_mm += fabsf(delta_mm);

            if (g_buf_sensor_type == BUF_SENSOR_TYPE_P && sync_enabled) {
                float mmu_mm_s = (float)lane_motion_sps(lane) * g_mm_per_step[idx];
                float arm_vel = g_vel_norm * buf_physical_half_travel_mm();
                /* Filament conservation: extruder draw = mmu feed + buffer drain rate.
                   Buffer drains toward TENSION, which is now NEGATIVE velocity, so the
                   drain rate is -arm_vel (was +arm_vel under the old +tension sign). */
                float extruder_mm_s = mmu_mm_s - arm_vel;
                float est_sps = 0.0f;
                if (g_mm_per_step[idx] > MM_PER_STEP_MIN) {
                    est_sps = extruder_mm_s / g_mm_per_step[idx];
                }
                float max_est_sps = (float)g_global_max_sps;
                if (est_sps < 0.0f)
                    est_sps = 0.0f;
                if (est_sps > max_est_sps)
                    est_sps = max_est_sps;

                float alpha = TYPE_P_ESTIMATOR_ALPHA;
                g_extruder_est_sps = alpha * est_sps + (1.0f - alpha) * g_extruder_est_sps;
                g_extruder_est_last_update_ms = now_ms;
            }

            if (g_buf.state == BUF_TENSION) {
                g_sync_refill_effort_mm += delta_mm;
                if (!g_sync_cannot_refill_warned &&
                    g_sync_refill_effort_mm >= CONF_SYNC_CANNOT_REFILL_MM) {
                    g_sync_cannot_refill_warned = true;
                    cmd_event("SYNC", "cannot_refill");
                }
            } else if (g_buf.state == BUF_COMPRESSION) {
                g_sync_relieve_effort_mm += delta_mm;
                if (!g_sync_cannot_relieve_warned &&
                    g_sync_relieve_effort_mm >= CONF_SYNC_CANNOT_RELIEVE_MM) {
                    g_sync_cannot_relieve_warned = true;
                    cmd_event("SYNC", "cannot_relieve");
                }
            }
        }

        buf_signal_publish(now_ms);
    }
}
