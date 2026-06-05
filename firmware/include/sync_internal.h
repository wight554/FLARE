#pragma once

#include "buf_signal.h"
#include "sync.h"
#include <stdbool.h>
#include <stdint.h>

#define HISTORY_LEN 16
#define SYNC_COMPRESSION_SOFT_WALL_MS 1200.0f
#define SYNC_COMPRESSION_HARD_WALL_MS 350.0f
#define SYNC_COMPRESSION_HARD_PUSH_MM_S 0.25f

#define SYNC_COMPRESSION_COLLAPSE_DELAY_MS ((uint32_t)RELAY_COLLAPSE_DELAY_MS)
#define SYNC_COMPRESSION_COLLAPSE_RAMP_MULT RELAY_COLLAPSE_RAMP_MULT
#define SYNC_COMPRESSION_COLLAPSE_CAP_MS ((uint32_t)RELAY_COLLAPSE_CAP_MS)
#define SYNC_EST_FRESH_MS 20000u

#define SYNC_STAB_PREDICT_LEAD_S 0.10f
#define SYNC_NEUTRAL_COMPRESSION_TAPER_FRAC 0.5f
#define SYNC_NEUTRAL_COMPRESSION_FLOOR_FRAC 0.45f
#define SYNC_NEUTRAL_ANTI_TENSION_STALE_MS 1500u
#define SYNC_NEUTRAL_ANTI_TENSION_FLOOR_FRAC 0.70f
#define SYNC_NEUTRAL_ANTI_TENSION_CONF 0.98f
#define SYNC_RESERVE_CENTER_GUARD_FRAC 0.05f

#define SYNC_RESERVE_BIAS_POS_FRAC_CAP 0.10f
#define SYNC_COMPRESSION_FEED_TRIM_MAX_SPS 120

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

typedef struct {
    buf_state_t zone;
    uint32_t dwell_ms;
} zone_event_t;

#define TENSION_PIN_WINDOW_LEN 16

#define BL_WATCHDOG_DEFAULT_MS 30000u

typedef enum {
    BUFFER_SERVICE_STABILIZE = 0,
    BUFFER_SERVICE_NEG_SYNC,
} buffer_service_mode_t;

// Shared variables
extern sync_state_t g_sync_state;
extern bool sync_auto_started;
extern bool sync_tail_assist_active;
extern uint32_t sync_idle_since_ms;
extern int sync_current_sps;

extern float g_psf_target_filt;
extern int g_baseline_target_sps;
extern int g_baseline_sps;
extern float g_baseline_alpha;
extern const flow_schedule_point_t g_flow_sched_config[CONF_FLOW_SCHED_CAP];
extern flow_schedule_point_t g_flow_sched_runtime[CONF_FLOW_SCHED_CAP];
extern int g_flow_sched_live_delta[CONF_FLOW_SCHED_CAP];
extern int g_flow_sched_len;
extern uint32_t sync_fast_brake_until_ms;
extern bool sync_compression_recovery_active;
extern uint32_t sync_continuous_compression_since_ms;
extern uint32_t sync_post_compression_boost_until_ms;
extern uint32_t sync_recent_negative_until_ms;
extern uint32_t sync_tension_pin_since_ms;

extern float sync_reserve_integral_mm;
extern float g_buf_pos_sigma_accum_mm;
extern float g_buf_sigma_mm;
extern uint32_t g_buf_est_low_cf_emit_ms;
extern uint32_t g_buf_tension_dwell_warn_emit_ms;
extern int g_neutral_creep_sps;
extern uint32_t g_neutral_creep_last_tension_ms;
extern bool g_buf_est_fallback_emitted;

extern float g_sync_refill_effort_mm;
extern float g_sync_relieve_effort_mm;
extern bool g_sync_cannot_refill_warned;
extern bool g_sync_cannot_relieve_warned;

extern float g_bp_residual_last_mm;
extern float g_bp_drift_ewma_mm;
extern uint16_t g_bp_drift_samples;
extern uint32_t g_bp_drift_last_ms;
extern float g_bp_drift_correction_applied_mm;
extern uint32_t g_tension_pin_ts[TENSION_PIN_WINDOW_LEN];
extern int g_tension_pin_ts_idx;
extern uint32_t g_tension_risk_emit_ms;

extern float g_tension_floor_sps;
extern float g_relay_flip_travel_since_mm;
extern float g_relay_neutral_trim_sps;
extern uint32_t g_relay_trim_last_leak_ms;
extern uint32_t g_type_d_neutral_feed_sps_sum;
extern uint16_t g_type_d_neutral_feed_samples;
extern uint32_t g_type_d_neutral_flat_feed_sps_sum;
extern uint16_t g_type_d_neutral_flat_feed_samples;

extern float g_buf_confidence;
extern uint32_t g_buf_last_transition_ms;
extern uint32_t g_buf_analog_saturated_since_ms;
extern uint32_t g_buf_analog_last_sample_ms;

extern buf_tracker_t g_buf;
extern float g_buf_pos;
extern float g_buf_pos_prev;

extern uint32_t sync_last_tick_ms;
extern uint32_t sync_last_evt_ms;
extern float extruder_est_sps;
extern float extruder_est_prev_sps;
extern uint32_t extruder_est_last_update_ms;
extern uint32_t last_slope_update_ms;

extern char g_marker_tag[MARKER_TAG_LEN];
extern uint16_t g_marker_seq;

extern float g_vel_norm;
extern float g_vel_norm_f;

extern float g_buf_pos_raw_status;
extern float g_buf_physical_entry_pos_mm;
extern uint32_t buf_pos_last_ms;

extern buf_state_t g_bl_goal_override;
extern bool g_bl_autostart_suppressed;
extern bool g_sync_tension_transitioned;
extern float g_sync_mmu_total_mm;
extern int g_settle_history[SETTLE_HISTORY_LEN];
extern uint8_t g_settle_history_count;
extern uint32_t g_last_baseline_update_ms;
extern float g_last_baseline_update_mm;

// Shared functions
int sync_bootstrap_sps(void);
int lane_motion_sps(lane_t *lane);
int baseline_control_floor_sps(void);
void buf_update(buf_state_t new_state, uint32_t now_ms);
void relay_on_transition(buf_state_t new_state, uint32_t now_ms);
int flow_sched_len_clamped(void);
int lerp_i(int a, int b, int x, int x0, int x1);
int flow_active_segment(int flow_sps);
lane_t *pick_boot_stabilize_lane(void);
float buf_physical_half_travel_mm(void);
float buf_threshold_mm(void);
float buf_target_reserve_mm(void);
float buf_virtual_deadband_mm(void);
void buf_anchor_virtual_position(buf_state_t old_state, buf_state_t new_state);
void buf_virtual_position_tick(lane_t *lane, uint32_t elapsed_ms);
float lane_motion_mm_s(lane_t *lane);
float extruder_motion_mm_s(lane_t *lane);
float sync_compression_wall_remaining_mm(void);
int sync_compression_floor_sps(void);
void neutral_creep_update(buf_state_t s, lane_t *lane, uint32_t now_ms);
int sync_apply_scaling(int base_sps, float target_norm, float pos_norm);
void history_push(buf_state_t zone, uint32_t dwell_ms);
bool predict_tension_coming(void);
float psf_goal_norm(void);
float buf_pos_norm(void);
float buf_target_norm(void);
buf_state_t buf_read_stable(uint32_t now_ms);
void buf_force_stable_state(buf_state_t state, uint32_t now_ms);
void boot_stabilize_stop(void);
void boot_stabilize_disarm(void);
bool buffer_stabilize_controller_idle(void);
bool buffer_negative_sync_eligible(void);
bool buffer_stabilize_start_internal(uint32_t now_ms, bool emit_events, buffer_service_mode_t mode);
float clamp_est_sps(float est_sps);
void blend_extruder_est_sps(float sample_sps, float alpha, uint32_t now_ms);
void blend_extruder_est_sps_direct(float sample_sps, float alpha, uint32_t now_ms);
void type_d_neutral_feed_reset(void);
void type_d_neutral_feed_sample(buf_state_t s, float pos_norm, float target_norm,
                                float deadband_norm);
bool type_d_neutral_feed_avg_sps(float *avg_sps);
bool type_d_sample_demand_bounds(float *demand_sps);
void relay_neutral_trim_clamp(void);
void relay_neutral_trim_leak(buf_state_t s, uint32_t now_ms);
void baseline_update_on_settle(uint32_t neutral_dwell_ms, uint32_t now_ms);
int sync_neutral_anti_tension_floor_sps(buf_state_t s, lane_t *lane, float error_norm,
                                        float deadband_norm, int neutral_target_sps,
                                        uint32_t now_ms);
int relay_control_law(buf_state_t s);
int psf_control_law(float error_norm);
void sync_apply_to_active(void);
void sync_on_transition(buf_state_t prev, buf_state_t now_state, uint32_t now_ms);
void buf_signal_publish(uint32_t now_ms);
void sync_buffer_lock_tick(lane_t *lane, uint32_t now_ms);
void handle_bl_watchdog_timeout(uint32_t now_ms);
int sync_neutral_creep_sps(void);
int sync_effective_kp_sps(buf_state_t s);
