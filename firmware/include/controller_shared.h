#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "firmware_constants.h"
#include "pico/stdlib.h"
#include "tmc2209.h"

/// @brief Type-P analog physical extreme boundary used for homing/locking.
#define PSF_HOME_THRESHOLD_NORM 0.90f /* reached/locked extreme boundary */
/// @brief Type-P BL_FOLLOW open-loop safety gate before the armed rail.
#define PSF_FOLLOW_RAIL_NORM                                                                       \
    0.95f /* BL_FOLLOW open-loop safety gate: stop feed before slamming the armed rail */

/// @brief Type-P compression-zone contact threshold during load.
#define PSF_LOAD_CONTACT_THRESHOLD_NORM                                                            \
    0.50f /* load contact detection boundary (compression zone) */
/// @brief
/// @brief Maximum marker tag bytes including the terminating NUL.
#define MARKER_TAG_LEN 32
/// @brief Baseline settle history sample slots.
#define SETTLE_HISTORY_LEN 16

/// @brief Debounced GPIO input state.
typedef struct {
    uint pin;
    bool stable;
    bool last_raw;
    absolute_time_t last_edge;
} debounced_input_t;

/// @brief Stepper motor GPIO/PWM routing and direction state.
typedef struct {
    uint en_pin, dir_pin, step_pin;
    bool dir_invert;
    uint slice;
    uint chan;
} motor_t;

/// @brief Lane motion task.
typedef enum {
    TASK_IDLE = 0,
    TASK_AUTOLOAD,
    TASK_FEED,
    TASK_UNLOAD,
    TASK_LOAD_FULL,
    TASK_MOVE
} task_t;

/// @brief Lane task fault classification.
typedef enum {
    FAULT_NONE = 0,
    FAULT_TIMEOUT,
    FAULT_SENSOR,
    FAULT_BUF,
    FAULT_CUT,
    FAULT_DRY_SPIN
} fault_t;

/// @brief Per-lane runtime state and motion bookkeeping.
typedef struct lane_s {
    debounced_input_t in_sw;
    debounced_input_t out_sw;
    motor_t m;
    task_t task;
    uint32_t motion_started_ms;
    uint32_t task_started_ms;
    uint32_t dry_spin_ms;
    float task_limit_mm;
    uint32_t retract_deadline_ms;
    int target_sps;
    int current_sps;
    uint32_t ramp_last_tick_ms;
    tmc_t *tmc;
    bool unload_sensor_latch;
    bool unload_buf_recover_done;
    fault_t fault;
    int lane_id;
    uint32_t runout_block_until_ms;
    uint32_t buf_tension_since_ms;
    uint32_t reload_tail_ms;
    float task_dist_mm;
    float dist_at_out_mm;
    uint32_t last_dist_tick_ms;
    float dist_at_in_clear_mm;
    bool task_forward;
    bool prev_in;
    bool unload_to_in;
    bool suppress_unloaded_event;
    bool load_completed;
    bool move_ignore_buffer;
} lane_t;

/// @brief Toolchange/RELOAD state-machine phase.
typedef enum {
    TC_IDLE,
    TC_UNLOAD_CUT,
    TC_UNLOAD_WAIT_CUT,
    TC_UNLOAD_REVERSE,
    TC_UNLOAD_WAIT_OUT,
    TC_UNLOAD_WAIT_Y,
    TC_UNLOAD_WAIT_TH,
    TC_UNLOAD_DONE,
    TC_SWAP,
    TC_LOAD_START,
    TC_LOAD_WAIT_OUT,
    TC_LOAD_WAIT_TH,
    TC_LOAD_DONE,
    TC_RELOAD_WAIT_Y,
    TC_RELOAD_APPROACH,
    TC_RELOAD_FOLLOW,
    TC_ERROR
} tc_state_t;

/// @brief Toolchange/RELOAD runtime context.
typedef struct {
    tc_state_t state;
    int target_lane;
    int from_lane;
    uint32_t phase_start_ms;
    uint32_t ready_to_join_since_ms;
    uint32_t reload_tick_ms;
    int reload_current_sps;
    uint32_t last_compression_ms;
    uint32_t wall_critical_since_ms;
    bool unload_cut_done;
} tc_ctx_t;

/// @brief Quantized buffer state.
typedef enum { BUF_NEUTRAL, BUF_TENSION, BUF_COMPRESSION, BUF_FAULT } buf_state_t;

/// @brief Buffer dwell, velocity, and estimator sample state.
typedef struct {
    buf_state_t state;
    uint32_t entered_ms;
    uint32_t dwell_ms;
    float arm_vel_mm_s;
    int lane_idx_at_entry;
    int mmu_sps_at_entry;
    uint32_t mmu_sps_dwell_sum;
    uint32_t mmu_sps_dwell_samples;
} buf_tracker_t;

static inline bool on_al(const debounced_input_t *d) {
    return d->stable != 0;
}

static inline bool lane_in_present(lane_t *L) {
    return on_al(&L->in_sw);
}

static inline bool lane_out_present(lane_t *L) {
    return on_al(&L->out_sw);
}

/// @brief Write a single-lane-id digit ('1'/'2') plus NUL into a 2-char buffer,
///        for the many cmd_event() payloads that report a lane number.
static inline void lane_id_str(char out[2], int lane_id) {
    out[0] = (char)('0' + lane_id);
    out[1] = '\0';
}

extern int g_feed_sps;
extern int g_rev_sps;
extern int g_auto_sps;
extern int g_motion_startup_ms;
extern int g_runout_cooldown_ms;
extern int g_post_print_stab_delay_ms;
extern float g_sync_psf_decay_sps_per_s;
extern int g_reload_mode;
extern int g_reload_y_timeout_ms;
extern int g_reload_join_delay_ms;
extern int g_join_sps;
extern int g_press_sps;
extern int g_compression_sps;
extern int g_reload_touch_settle_ms;
extern int g_reload_touch_boost_ms;
extern int g_reload_touch_floor_pct;
extern int g_buf_stab_sps;
extern int g_follow_timeout_ms[NUM_LANES];
extern int g_zone_bias_base_sps;
extern int g_zone_bias_ramp_sps_s;
extern int g_zone_bias_max_sps;
extern float g_est_alpha_min;
extern float g_est_alpha_max;
extern float g_reload_lean_factor;
extern int g_ramp_step_sps;
extern int g_ramp_tick_ms;
extern int g_tmc_run_current_ma[NUM_LANES];
extern int g_tmc_hold_current_ma[NUM_LANES];
extern int g_tmc_microsteps[NUM_LANES];
extern int g_tmc_stealthchop_sps[NUM_LANES];
extern float g_tmc_rotation_distance[NUM_LANES];
extern float g_tmc_gear_ratio[NUM_LANES];
extern int g_tmc_full_steps[NUM_LANES];
extern int g_tmc_tbl[NUM_LANES];
extern int g_tmc_toff[NUM_LANES];
extern int g_tmc_hstrt[NUM_LANES];
extern int g_tmc_hend[NUM_LANES];
extern bool g_tmc_interpolate[NUM_LANES];
/// @brief Sync-Feedback Sensor type values for BUF_SENSOR_TYPE.
/// Kept as plain ints (BUF_SENSOR_TYPE is a config-backed runtime int persisted
/// in flash), but named so call sites read intent instead of a bare 0/1.
enum {
    BUF_SENSOR_TYPE_D = 0, ///< dual-endstop (two microswitches)
    BUF_SENSOR_TYPE_P = 1  ///< proportional analog / Hall-effect
};
/// @brief Sync-Feedback Sensor type: D=0 dual-switch, P=1 proportional analog.
extern int g_buf_sensor_type;

extern float g_buf_psf_max_comp;
extern float g_buf_psf_max_tens;
extern float g_buf_psf_neutral;
extern float g_buf_goal;
extern float g_buf_analog_alpha;
extern int g_sync_kp_sps;
extern float g_kd_psf;
extern int g_sync_overshoot_pct;
extern int g_sync_reserve_pct;
extern int g_ts_buf_fallback_ms;
extern int g_servo_open_us;
extern int g_servo_close_us;
extern int g_servo_block_us;
extern int g_servo_settle_ms;
extern int g_cut_feed_sps;
extern int g_cut_feed_mm;
extern int g_cut_length_mm;
extern int g_cut_amount;
extern int g_cut_timeout_settle_ms;
extern int g_cut_timeout_feed_ms;
extern int g_tc_timeout_cut_ms;
extern int g_load_max_mm;
extern int g_unload_max_mm;
extern int g_unload_tension_block_ms;
extern int g_tc_timeout_th_ms;
extern int g_tc_timeout_y_ms;
extern int g_sync_max_sps;
extern int g_global_max_sps;
extern int g_sync_min_sps;
extern int g_sync_ramp_up_sps;
extern int g_sync_ramp_dn_sps;
extern int g_sync_tick_ms;
extern float g_sync_psf_slew_per_mm;
extern float g_sync_psf_filter_mm;
extern int g_psf_stab_stagnant_ms;
extern float g_psf_stab_stagnant_norm;
extern int g_psf_stab_rail_break_ms;
extern int g_pre_ramp_sps;
extern int g_buf_hyst_ms;
extern int g_buf_predict_thr_ms;
extern float g_buf_switch_span_half_mm;
extern int g_sync_auto_stop_ms;
extern int g_sync_tension_dwell_stop_ms;
extern int g_sync_tension_ramp_delay_ms;
extern int g_sync_overshoot_neutral_extend;
extern float g_sync_compression_bias_frac;
extern int g_neutral_creep_timeout_ms;
extern int g_neutral_creep_rate_sps_per_s;
extern int g_neutral_creep_cap_frac;
extern float g_buf_variance_blend_frac;
extern float g_buf_variance_blend_ref_mm;
extern float g_buf_pos_raw_status;
extern float g_sync_reserve_integral_gain;
extern float g_sync_reserve_integral_clamp_mm;
extern int g_sync_reserve_integral_decay_ms;
extern float g_est_sigma_hard_cap_mm;
extern float g_est_low_cf_warn_threshold;
extern float g_est_fallback_cf_threshold;
extern float g_relay_catchup_frac;
extern float g_relay_neutral_frac;
extern int g_sync_relay_trim_step_sps;
extern int g_sync_relay_trim_clamp_sps;
extern float g_sync_compression_drain_frac;
extern float g_sync_compression_drain_budget_mm;
extern float g_sync_est_attack_alpha;
extern float g_sync_tension_fast_mm_s;
extern int g_sync_tension_probe_max_sps;
extern int g_sync_tension_probe_up_sps_per_s;
extern int g_sync_tension_probe_down_sps_per_s;
extern int g_sync_tension_probe_neutral_sps_per_s;
extern float g_relay_min_flip_mm;
extern int g_relay_collapse_delay_ms;
extern int g_relay_collapse_ramp_mult;
extern int g_relay_collapse_cap_ms;
#define BUF_DRIFT_CLAMP_LIMIT_MM 8.0f
extern int g_buf_drift_ewma_tau_ms;
extern int g_buf_drift_min_samples;
extern float g_buf_drift_apply_thr_mm;
extern float g_buf_drift_clamp_mm;
extern float g_buf_drift_apply_min_cf;
extern int g_tension_risk_window_ms;
extern int g_tension_risk_threshold;
extern int g_autoload_max_mm;
extern int g_auto_mode;
extern bool g_auto_preload;
extern int g_autoload_retract_mm;
extern bool g_enable_cutter;
extern bool g_unload_cut;
extern int g_dist_in_out;
extern int g_dist_out_y;
extern int g_dist_y_buf;
extern int g_buf_body_len;
extern int g_buf_max_travel_mm;
extern float g_mm_per_step[NUM_LANES];
extern uint32_t g_shadow_ihold_irun[NUM_LANES];
extern bool g_shadow_ihold_irun_valid[NUM_LANES];
extern bool g_shadow_vsense[NUM_LANES];
extern lane_t g_lane_l1;
extern lane_t g_lane_l2;
extern debounced_input_t g_y_split;
extern debounced_input_t g_buf_tension_din;
extern debounced_input_t g_buf_compression_din;
extern tmc_t g_tmc_l1;
extern tmc_t g_tmc_l2;
extern tc_ctx_t g_tc_ctx;
extern volatile uint32_t g_now_ms;
extern int g_active_lane;
extern bool g_toolhead_has_filament;
extern bool g_sync_enabled;
extern bool g_sync_auto_started;
extern bool g_sync_tail_assist_active;
extern uint32_t g_sync_idle_since_ms;
extern int g_sync_current_sps;
extern int g_baseline_target_sps;
extern int g_baseline_sps;
extern float g_baseline_alpha;
extern uint32_t g_sync_fast_brake_until_ms;
extern char g_marker_tag[MARKER_TAG_LEN];
extern uint16_t g_marker_seq;

extern buf_tracker_t g_buf;
extern float g_extruder_est_sps;
extern float g_buf_pos;
extern bool g_boot_stabilizing;

extern int sync_neutral_creep_sps(void);
int mm_per_min_to_sps_idx(float mm_per_min, int idx);
int mm_per_min_to_sps(float mm_per_min);
float sps_to_mm_per_min_idx(int sps, int idx);
float sps_to_mm_per_min(int sps);
int clamp_i(int v, int lo, int hi);
float clamp_f(float v, float lo, float hi);
int lane_to_idx(int ln);
uint32_t build_ihold_irun_reg(int run_ma, int hold_ma, bool vsense);
void sync_currents_from_ihold_irun(int ln, uint32_t reg);
void set_toolhead_filament(bool present);
void set_active_lane(int lane);
lane_t *lane_ptr(int lane);
int other_lane(int lane);
