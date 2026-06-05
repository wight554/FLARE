/// @file main.c
/// @brief Top-level firmware entry: hardware init, runtime globals, autopreload,
///        status LEDs, and the cooperative main-loop that ticks every subsystem.
/// @details Non-blocking superloop — each tick polls sensors then advances motion,
///          toolchange, sync, and protocol without blocking. See BEHAVIOR.md
///          "Boot sequence" and "Autopreload"; ownership in spec project-architecture.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "controller_shared.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

#include "cutter.h"
#include "motion.h"
#include "neopixel.h"
#include "protocol.h"
#include "settings_store.h"
#include "sync.h"
#include "tmc2209.h"
#include "toolchange.h"
#include <math.h>

#define SECONDS_PER_MINUTE_F 60.0f
#define ROUND_TO_NEAREST_F 0.5f
#define DISPLAY_ROUNDING_OFFSET_F 0.05f
#define TMC_RSENSE_SERIES_OHM 0.020f
#define TMC_VREF_VSENSE_LOW 0.180f
#define TMC_VREF_VSENSE_HIGH 0.325f
#define SQRT_2_F 1.41421356f
#define MA_PER_AMP_F 1000.0f
#define TMC_CURRENT_SCALE_F 32.0f
#define TMC_MAX_CURRENT_MA 2000
#define TMC_CS_MAX 31
#define TMC_CS_MASK 0x1Fu
#define TMC_IRUN_SHIFT 8u
#define TMC_IRUNDELAY_VALUE 8u
#define TMC_IRUNDELAY_SHIFT 16u
#define MIN_MM_PER_STEP_F 1e-6f
#define LED_UPDATE_INTERVAL_MS 50u
#define LED_IDLE_GREEN 20
#define LED_LOADING_BLUE 120
#define LED_ACTIVE_GREEN 200
#define LED_TC_RED 180
#define LED_TC_GREEN 140
#define LED_ERROR_RED 200
#define LED_CUT_PHASE_MS 32u
#define LED_CUT_PHASE_MASK 0x0Fu
#define LED_CUT_HALF_PHASE 8u
#define LED_CUT_MAX_PHASE 15u
#define LED_CUT_BRIGHTNESS_STEP 32u
#define BOOT_SENSOR_SETTLE_SAMPLES 25
#define USB_STARTUP_SETTLE_MS 200u
#define BOOT_STAB_FIRST_MS 2500u
#define MAIN_LOOP_SLEEP_US 100u

// ===================== Tunables =====================
int g_feed_sps = CONF_FEED_SPS;
int g_rev_sps = CONF_REV_SPS;
int g_auto_sps = CONF_AUTO_SPS;

int g_motion_startup_ms = FLARE_INT_MOTION_STARTUP_MS;

int g_runout_cooldown_ms = CONF_RUNOUT_COOLDOWN_MS;
int g_post_print_stab_delay_ms = FLARE_INT_POST_PRINT_STAB_DELAY_MS;
float g_sync_psf_decay_sps_per_s = FLARE_INT_SYNC_PSF_DECAY_SPS_PER_S;
int g_reload_mode = CONF_RELOAD_MODE;
int g_reload_y_timeout_ms = FLARE_INT_RELOAD_Y_TIMEOUT_MS;
int g_reload_join_delay_ms = CONF_RELOAD_JOIN_DELAY_MS;
int g_join_sps = CONF_JOIN_SPS;
int g_press_sps = CONF_PRESS_SPS;
int g_compression_sps = CONF_COMPRESSION_SPS;
int g_reload_touch_settle_ms = CONF_RELOAD_TOUCH_SETTLE_MS;
int g_reload_touch_boost_ms = CONF_RELOAD_TOUCH_BOOST_MS;
int g_reload_touch_floor_pct = CONF_RELOAD_TOUCH_FLOOR_PCT;
int g_buf_stab_sps = CONF_BUF_STAB_SPS; /* not persisted; settings_defaults() re-sets it, but the
                                         NVM-load path does not — must init non-zero or BS returns
                                         BUF_STAB_UNAVAILABLE on a board with saved settings */
int g_follow_timeout_ms[NUM_LANES] = {CONF_L1_FOLLOW_TIMEOUT_MS, CONF_L2_FOLLOW_TIMEOUT_MS};

int g_zone_bias_base_sps = CONF_ZONE_BIAS_BASE_SPS;
int g_zone_bias_ramp_sps_s = CONF_ZONE_BIAS_RAMP_SPS_S;
int g_zone_bias_max_sps = CONF_ZONE_BIAS_MAX_SPS;
float g_est_alpha_min = FLARE_INT_EST_ALPHA_MIN;
float g_est_alpha_max = FLARE_INT_EST_ALPHA_MAX;
float g_reload_lean_factor = FLARE_INT_RELOAD_LEAN_FACTOR;

int g_ramp_step_sps = CONF_RAMP_STEP_SPS;
int g_ramp_tick_ms = CONF_RAMP_TICK_MS;

int g_tmc_run_current_ma[NUM_LANES] = {CONF_L1_RUN_CURRENT_MA, CONF_L2_RUN_CURRENT_MA};
int g_tmc_hold_current_ma[NUM_LANES] = {CONF_L1_HOLD_CURRENT_MA, CONF_L2_HOLD_CURRENT_MA};
int g_tmc_microsteps[NUM_LANES] = {CONF_L1_MICROSTEPS, CONF_L2_MICROSTEPS};
int g_tmc_stealthchop_sps[NUM_LANES] = {CONF_L1_STEALTHCHOP_THRESHOLD, CONF_L2_STEALTHCHOP_THRESHOLD};
float g_tmc_rotation_distance[NUM_LANES] = {CONF_L1_ROTATION_DISTANCE, CONF_L2_ROTATION_DISTANCE};
float g_tmc_gear_ratio[NUM_LANES] = {CONF_L1_GEAR_RATIO, CONF_L2_GEAR_RATIO};
int g_tmc_full_steps[NUM_LANES] = {CONF_L1_FULL_STEPS, CONF_L2_FULL_STEPS};
int g_tmc_tbl[NUM_LANES] = {CONF_L1_TBL, CONF_L2_TBL};
int g_tmc_toff[NUM_LANES] = {CONF_L1_TOFF, CONF_L2_TOFF};
int g_tmc_hstrt[NUM_LANES] = {CONF_L1_HSTRT, CONF_L2_HSTRT};
int g_tmc_hend[NUM_LANES] = {CONF_L1_HEND, CONF_L2_HEND};
bool g_tmc_interpolate[NUM_LANES] = {CONF_L1_INTPOL, CONF_L2_INTPOL};

int g_buf_sensor_type = CONF_BUF_SENSOR_TYPE;
int g_buf_home_state = CONF_BUF_HOME_STATE;
float g_buf_psf_max_comp = CONF_BUF_PSF_MAX_COMP;
float g_buf_psf_max_tens = CONF_BUF_PSF_MAX_TENS;
float g_buf_psf_neutral = CONF_BUF_PSF_NEUTRAL;
float g_buf_goal = CONF_BUF_GOAL;
float g_buf_analog_alpha = FLARE_INT_BUF_ANALOG_ALPHA;
int g_sync_kp_sps = CONF_SYNC_KP_SPS;
float g_kd_psf = FLARE_INT_KD_PSF;
int g_sync_overshoot_pct = FLARE_INT_SYNC_OVERSHOOT_PCT;
int g_sync_reserve_pct = CONF_SYNC_RESERVE_PCT;
int g_ts_buf_fallback_ms = FLARE_INT_TS_BUF_FALLBACK_MS;

int g_servo_open_us = CONF_SERVO_OPEN_US;
int g_servo_close_us = CONF_SERVO_CLOSE_US;
int g_servo_block_us = CONF_SERVO_BLOCK_US;
int g_servo_settle_ms = CONF_SERVO_SETTLE_MS;
int g_cut_feed_sps = CONF_CUT_FEED_SPS;
int g_cut_feed_mm = CONF_CUT_FEED_MM;
int g_cut_length_mm = CONF_CUT_LENGTH_MM;
int g_cut_amount = CONF_CUT_AMOUNT;
int g_cut_timeout_settle_ms = FLARE_INT_CUT_TIMEOUT_SETTLE_MS;
int g_cut_timeout_feed_ms = FLARE_INT_CUT_TIMEOUT_FEED_MS;

int g_tc_timeout_cut_ms = FLARE_INT_TC_TIMEOUT_CUT_MS;
int g_load_max_mm = CONF_LOAD_MAX_MM;
int g_unload_max_mm = CONF_UNLOAD_MAX_MM;
int g_unload_tension_block_ms = CONF_UNLOAD_TENSION_BLOCK_MS;
int g_tc_timeout_th_ms = FLARE_INT_TC_TIMEOUT_TH_MS;
int g_tc_timeout_y_ms = FLARE_INT_TC_TIMEOUT_Y_MS;

int g_sync_max_sps = CONF_SYNC_MAX_SPS;
int g_global_max_sps = CONF_GLOBAL_MAX_SPS;
int g_sync_min_sps = CONF_SYNC_MIN_SPS;
int g_sync_ramp_up_sps = CONF_SYNC_RAMP_UP_SPS;
int g_sync_ramp_dn_sps = CONF_SYNC_RAMP_DN_SPS;
int g_sync_tick_ms = CONF_SYNC_TICK_MS;
/* Type-P output smoothing — runtime-tunable, not persisted (re-init from CONF
   each boot, matching BUF_STAB_SPS). Tune live via SET; reflash changes the default. */
float g_sync_psf_slew_per_mm = CONF_SYNC_PSF_SLEW_PER_MM;
float g_sync_psf_filter_mm = CONF_SYNC_PSF_FILTER_MM;
int g_psf_stab_stagnant_ms = CONF_PSF_STAB_STAGNANT_MS;
float g_psf_stab_stagnant_norm = CONF_PSF_STAB_STAGNANT_NORM;
int g_psf_stab_rail_break_ms = CONF_PSF_STAB_RAIL_BREAK_MS;
int g_pre_ramp_sps = CONF_PRE_RAMP_SPS;
int g_buf_hyst_ms = FLARE_INT_BUF_HYST_MS;
int g_buf_predict_thr_ms = FLARE_INT_BUF_PREDICT_THR_MS;
float g_buf_switch_span_half_mm = CONF_BUF_SWITCH_SPAN_MM * HALF_F;
int g_sync_auto_stop_ms = CONF_SYNC_AUTO_STOP_MS;
int g_sync_tension_dwell_stop_ms = FLARE_INT_SYNC_TENSION_DWELL_STOP_MS;
int g_sync_tension_ramp_delay_ms = FLARE_INT_SYNC_TENSION_RAMP_DELAY_MS;
int g_sync_overshoot_neutral_extend = FLARE_INT_SYNC_OVERSHOOT_NEUTRAL_EXTEND;
float g_sync_compression_bias_frac = CONF_SYNC_COMPRESSION_BIAS_FRAC;
int g_neutral_creep_timeout_ms = FLARE_INT_NEUTRAL_CREEP_TIMEOUT_MS;
int g_neutral_creep_rate_sps_per_s = FLARE_INT_NEUTRAL_CREEP_RATE_SPS_PER_S;
int g_neutral_creep_cap_frac = FLARE_INT_NEUTRAL_CREEP_CAP_FRAC;
float g_buf_variance_blend_frac = FLARE_INT_BUF_VARIANCE_BLEND_FRAC;
float g_buf_variance_blend_ref_mm = FLARE_INT_BUF_VARIANCE_BLEND_REF_MM;
float g_sync_reserve_integral_gain = FLARE_INT_SYNC_RESERVE_INTEGRAL_GAIN;
float g_sync_reserve_integral_clamp_mm = FLARE_INT_SYNC_RESERVE_INTEGRAL_CLAMP_MM;
int g_sync_reserve_integral_decay_ms = FLARE_INT_SYNC_RESERVE_INTEGRAL_DECAY_MS;
float g_est_sigma_hard_cap_mm = FLARE_INT_EST_SIGMA_HARD_CAP_MM;
float g_est_low_cf_warn_threshold = FLARE_INT_EST_LOW_CF_WARN_THRESHOLD;
float g_est_fallback_cf_threshold = FLARE_INT_EST_FALLBACK_CF_THRESHOLD;
float g_relay_catchup_frac = CONF_RELAY_CATCHUP_FRAC;
float g_relay_neutral_frac = CONF_RELAY_NEUTRAL_FRAC;
int g_sync_relay_trim_step_sps = CONF_SYNC_RELAY_TRIM_STEP_SPS;
int g_sync_relay_trim_clamp_sps = CONF_SYNC_RELAY_TRIM_CLAMP_SPS;
float g_sync_compression_drain_frac = CONF_SYNC_COMPRESSION_DRAIN_FRAC;
float g_sync_compression_drain_budget_mm = CONF_SYNC_COMPRESSION_DRAIN_BUDGET_MM;
float g_sync_est_attack_alpha = CONF_SYNC_EST_ATTACK_ALPHA;
float g_sync_tension_fast_mm_s = CONF_SYNC_TENSION_FAST_MM_S;
int g_sync_tension_probe_max_sps = CONF_SYNC_TENSION_PROBE_MAX_SPS;
int g_sync_tension_probe_up_sps_per_s = CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S;
int g_sync_tension_probe_down_sps_per_s = CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S;
int g_sync_tension_probe_neutral_sps_per_s = CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S;
float g_relay_min_flip_mm = FLARE_INT_RELAY_MIN_FLIP_MM;
int g_relay_collapse_delay_ms = FLARE_INT_RELAY_COLLAPSE_DELAY_MS;
int g_relay_collapse_ramp_mult = FLARE_INT_RELAY_COLLAPSE_RAMP_MULT;
int g_relay_collapse_cap_ms = FLARE_INT_RELAY_COLLAPSE_CAP_MS;
int g_buf_drift_ewma_tau_ms = FLARE_INT_BUF_DRIFT_EWMA_TAU_MS;
int g_buf_drift_min_samples = FLARE_INT_BUF_DRIFT_MIN_SAMPLES;
float g_buf_drift_apply_thr_mm = FLARE_INT_BUF_DRIFT_APPLY_THR_MM;
float g_buf_drift_clamp_mm = FLARE_INT_BUF_DRIFT_CLAMP_MM;
float g_buf_drift_apply_min_cf = FLARE_INT_BUF_DRIFT_APPLY_MIN_CF;
int g_tension_risk_window_ms = FLARE_INT_TENSION_RISK_WINDOW_MS;
int g_tension_risk_threshold = FLARE_INT_TENSION_RISK_THRESHOLD;
int g_autoload_max_mm = CONF_AUTOLOAD_MAX_MM;
int g_auto_mode = 1; // 1=Automated flow, 0=Host-controlled flow
bool g_auto_preload = true;
int g_autoload_retract_mm = CONF_AUTOLOAD_RETRACT_MM;
bool g_enable_cutter = CONF_ENABLE_CUTTER;
bool g_unload_cut = CONF_UNLOAD_CUT;

int g_dist_in_out = CONF_DIST_IN_OUT;
int g_dist_out_y = CONF_DIST_OUT_Y;
int g_dist_y_buf = CONF_DIST_Y_BUF;
int g_buf_body_len = CONF_BUF_BODY_LEN;
int g_buf_max_travel_mm = CONF_BUF_MAX_TRAVEL_MM;

// Derived Physical Path Constants
#define Y_TO_BUF_NEUTRAL ((float)DIST_Y_BUF + (float)BUF_MAX_TRAVEL_MM / 2.0f)

float g_mm_per_step[NUM_LANES] = {CONF_L1_MM_PER_STEP, CONF_L2_MM_PER_STEP};

int mm_per_min_to_sps_idx(float mm_per_min, int idx) {
    return (int)(mm_per_min / SECONDS_PER_MINUTE_F / g_mm_per_step[idx] + ROUND_TO_NEAREST_F);
}
int mm_per_min_to_sps(float mm_per_min) {
    return mm_per_min_to_sps_idx(mm_per_min, 0);
}
float sps_to_mm_per_min_idx(int sps, int idx) {
    return (float)sps * g_mm_per_step[idx] * SECONDS_PER_MINUTE_F + DISPLAY_ROUNDING_OFFSET_F;
}
float sps_to_mm_per_min(int sps) {
    return sps_to_mm_per_min_idx(sps, 0);
}

uint32_t g_shadow_ihold_irun[NUM_LANES] = {0, 0};
bool g_shadow_ihold_irun_valid[NUM_LANES] = {false, false};
bool g_shadow_vsense[NUM_LANES] = {true, true};

// ===================== Helpers =====================
int clamp_i(int value, int lo, int hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

float clamp_f(float value, float lo, float hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

int lane_to_idx(int lane) {
    return (lane == 1) ? 0 : 1;
}

static int cs_to_ma(uint8_t cs, bool vsense) {
    const float reff = CONF_RSENSE_OHM + TMC_RSENSE_SERIES_OHM;
    const float vref = vsense ? TMC_VREF_VSENSE_LOW : TMC_VREF_VSENSE_HIGH;
    float irms = ((float)cs + 1.0f) * vref / (TMC_CURRENT_SCALE_F * reff * SQRT_2_F);
    int ma = (int)(irms * MA_PER_AMP_F + ROUND_TO_NEAREST_F);
    return clamp_i(ma, 0, TMC_MAX_CURRENT_MA);
}

static uint8_t ma_to_cs(int ma, bool vsense) {
    if (ma <= 0)
        return 0;
    const float reff = CONF_RSENSE_OHM + TMC_RSENSE_SERIES_OHM;
    const float vref = vsense ? TMC_VREF_VSENSE_LOW : TMC_VREF_VSENSE_HIGH;
    float irms = (float)ma / MA_PER_AMP_F;
    int cs = (int)(TMC_CURRENT_SCALE_F * irms * reff * SQRT_2_F / vref - 1.0f + ROUND_TO_NEAREST_F);
    return (uint8_t)clamp_i(cs, 0, TMC_CS_MAX);
}

uint32_t build_ihold_irun_reg(int run_ma, int hold_ma, bool vsense) {
    uint8_t irun = ma_to_cs(run_ma, vsense);
    uint8_t ihold = ma_to_cs(hold_ma, vsense);
    return ((uint32_t)ihold) | ((uint32_t)irun << TMC_IRUN_SHIFT) |
           (TMC_IRUNDELAY_VALUE << TMC_IRUNDELAY_SHIFT);
}

void sync_currents_from_ihold_irun(int lane, uint32_t reg) {
    int idx = lane_to_idx(lane);
    uint8_t ihold = (uint8_t)(reg & TMC_CS_MASK);
    uint8_t irun = (uint8_t)((reg >> TMC_IRUN_SHIFT) & TMC_CS_MASK);
    bool vsense = g_shadow_vsense[idx];
    g_tmc_run_current_ma[idx] = cs_to_ma(irun, vsense);
    g_tmc_hold_current_ma[idx] = cs_to_ma(ihold, vsense);
}

// ===================== Globals =====================
lane_t g_lane_l1;
lane_t g_lane_l2;
debounced_input_t g_y_split;

debounced_input_t g_buf_tension_din;
debounced_input_t g_buf_compression_din;

tmc_t g_tmc_l1;
tmc_t g_tmc_l2;

tc_ctx_t g_tc_ctx = {.state = TC_IDLE};

volatile uint32_t g_now_ms = 0;
int g_active_lane = 0;
bool g_toolhead_has_filament = false;

bool g_prev_lane1_in_present = false;
bool g_prev_lane2_in_present = false;

// ===================== Forward declarations =====================
// Toolhead sensor state is always tracked, but in AUTO_MODE sync is governed
// by buffer state rather than TS events.
void set_toolhead_filament(bool present) {
    g_toolhead_has_filament = present;
    if (!g_auto_mode) {
        if (present)
            sync_set_state(SYNC_ACTIVE);
        else
            sync_disable(false);
        if (!present) {
            g_sync_current_sps = 0;
            g_sync_auto_started = false;
            g_sync_tail_assist_active = false;
            g_sync_idle_since_ms = 0;
        }
    }
}

static int detect_active_lane_from_out(void) {
    bool lane1_present = lane_out_present(&g_lane_l1);
    bool lane2_present = lane_out_present(&g_lane_l2);
    if (lane1_present && !lane2_present)
        return 1;
    if (lane2_present && !lane1_present)
        return 2;
    return 0;
}

void set_active_lane(int lane) {
    if (g_active_lane != lane && (g_active_lane == 1 || g_active_lane == 2) && (lane == 1 || lane == 2)) {
        int old_idx = g_active_lane - 1;
        int new_idx = lane - 1;
        if (g_mm_per_step[new_idx] > MIN_MM_PER_STEP_F) {
            g_extruder_est_sps = g_extruder_est_sps * (g_mm_per_step[old_idx] / g_mm_per_step[new_idx]);
        }
    }
    g_active_lane = lane;
    if (lane == 1 || lane == 2) {
        char lane_s[2];
        lane_id_str(lane_s, lane);
        cmd_event("ACTIVE", lane_s);
    } else {
        cmd_event("ACTIVE", "NONE");
    }
}

lane_t *lane_ptr(int lane) {
    if (lane == 1)
        return &g_lane_l1;
    if (lane == 2)
        return &g_lane_l2;
    return NULL;
}

int other_lane(int lane) {
    return (lane == 1) ? 2 : 1;
}

static void autopreload_tick(uint32_t now_ms) {
    if (!g_auto_mode && !g_auto_preload) {
        g_prev_lane1_in_present = lane_in_present(&g_lane_l1);
        g_prev_lane2_in_present = lane_in_present(&g_lane_l2);
        return;
    }

    /* Suppress auto-load only while a BL motor drive is actually moving
     * the lane (PRIME or CATCH). The IN-sensor rising edge while the
     * motor is pulling filament past the sensor would otherwise spuriously
     * launch TASK_LOAD_FULL. During BL_LOCKED the motor is at zero feed —
     * any IN rising edge there is a real operator insertion and must NOT
     * be consumed. After release (BL_IDLE) the gate doesn't apply. */
    if (sync_buffer_lock_motor_moving()) {
        g_prev_lane1_in_present = lane_in_present(&g_lane_l1);
        g_prev_lane2_in_present = lane_in_present(&g_lane_l2);
        return;
    }

    bool in1 = lane_in_present(&g_lane_l1);
    bool in2 = lane_in_present(&g_lane_l2);

    // MMU is completely empty if neither OUT sensor is present.
    bool mmu_empty = !lane_out_present(&g_lane_l1) && !lane_out_present(&g_lane_l2);

    if (in1 && !g_prev_lane1_in_present) {
        if (g_lane_l1.fault == FAULT_DRY_SPIN)
            g_lane_l1.fault = FAULT_NONE;
        if (g_tc_ctx.state == TC_ERROR)
            tc_abort();
        if (g_lane_l1.task == TASK_IDLE &&
            (tc_state() == TC_IDLE || tc_state() == TC_RELOAD_FOLLOW) && !cutter_busy() &&
            !lane_out_present(&g_lane_l1)) {
            if (g_auto_mode && mmu_empty) {
                // Completely empty MMU: auto-load all the way to toolhead.
                lane_start(&g_lane_l1, TASK_LOAD_FULL, g_feed_sps, true, now_ms, (float)g_load_max_mm);
                cmd_event("AUTO_LOAD", "1");
            } else if (g_auto_preload) {
                // Other lane loaded (or AUTO_MODE off): just preload to Y-splitter.
                lane_start(&g_lane_l1, TASK_AUTOLOAD, g_auto_sps, true, now_ms,
                           (float)g_autoload_max_mm);
                cmd_event("PRELOAD", "1");
            }
            if (!lane_out_present(&g_lane_l2))
                set_active_lane(1);
        }
    }

    if (in2 && !g_prev_lane2_in_present) {
        if (g_lane_l2.fault == FAULT_DRY_SPIN)
            g_lane_l2.fault = FAULT_NONE;
        if (g_tc_ctx.state == TC_ERROR)
            tc_abort();
        if (g_lane_l2.task == TASK_IDLE &&
            (tc_state() == TC_IDLE || tc_state() == TC_RELOAD_FOLLOW) && !cutter_busy() &&
            !lane_out_present(&g_lane_l2)) {
            if (g_auto_mode && mmu_empty) {
                lane_start(&g_lane_l2, TASK_LOAD_FULL, g_feed_sps, true, now_ms, (float)g_load_max_mm);
                cmd_event("AUTO_LOAD", "2");
            } else if (g_auto_preload) {
                lane_start(&g_lane_l2, TASK_AUTOLOAD, g_auto_sps, true, now_ms,
                           (float)g_autoload_max_mm);
                cmd_event("PRELOAD", "2");
            }
            if (!lane_out_present(&g_lane_l1))
                set_active_lane(2);
        }
    }

    g_prev_lane1_in_present = in1;
    g_prev_lane2_in_present = in2;
}

// ===================== NeoPixel state =====================
typedef enum { LED_IDLE, LED_LOADING, LED_ACTIVE, LED_TC, LED_ERROR, LED_CUTTING } led_state_t;

static led_state_t led_state_from_system(void) {
    if (g_lane_l1.fault || g_lane_l2.fault || g_tc_ctx.state == TC_ERROR)
        return LED_ERROR;
    if (cutter_busy())
        return LED_CUTTING;
    if (g_tc_ctx.state != TC_IDLE)
        return LED_TC;
    if (g_lane_l1.task == TASK_AUTOLOAD || g_lane_l2.task == TASK_AUTOLOAD)
        return LED_LOADING;
    if (sync_enabled && g_sync_current_sps > 0)
        return LED_ACTIVE;
    return LED_IDLE;
}

static void neopixel_tick(uint32_t now_ms) {
    static uint32_t last_ms = 0;
    if ((now_ms - last_ms) < LED_UPDATE_INTERVAL_MS)
        return;
    last_ms = now_ms;

    switch (led_state_from_system()) {
    case LED_IDLE:
        neopixel_set(0, LED_IDLE_GREEN, 0);
        break;
    case LED_LOADING:
        neopixel_set(0, 0, LED_LOADING_BLUE);
        break;
    case LED_ACTIVE:
        neopixel_set(0, LED_ACTIVE_GREEN, 0);
        break;
    case LED_TC:
        neopixel_set(LED_TC_RED, LED_TC_GREEN, 0);
        break;
    case LED_ERROR:
        neopixel_set(LED_ERROR_RED, 0, 0);
        break;
    case LED_CUTTING: {
        uint8_t phase = (uint8_t)((now_ms / LED_CUT_PHASE_MS) & LED_CUT_PHASE_MASK);
        uint8_t brightness = (phase < LED_CUT_HALF_PHASE)
                                 ? (uint8_t)(phase * LED_CUT_BRIGHTNESS_STEP)
                                 : (uint8_t)((LED_CUT_MAX_PHASE - phase) * LED_CUT_BRIGHTNESS_STEP);
        neopixel_set(brightness, brightness, brightness);
        break;
    }
    }
}

static void settle_boot_sensors(void) {
    // debounced_input_init reads GPIOs once without debounce; sensors may not have settled.
    // Spin debounced_input_update for 25 ms so the 10 ms debounce threshold commits correctly.
    // For Type-P, also poll the ADC pin to let the EWMA filter settle to the true physical value.
    for (int i = 0; i < BOOT_SENSOR_SETTLE_SAMPLES; i++) {
        debounced_input_update(&g_lane_l1.in_sw);
        debounced_input_update(&g_lane_l1.out_sw);
        debounced_input_update(&g_lane_l2.in_sw);
        debounced_input_update(&g_lane_l2.out_sw);
        debounced_input_update(&g_y_split);
        debounced_input_update(&g_buf_tension_din);
        debounced_input_update(&g_buf_compression_din);
        if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
            buf_analog_update();
        }
        sleep_ms(1);
    }
}

// ===================== Main =====================
int main(void) {
    stdio_init_all();
    sleep_ms(USB_STARTUP_SETTLE_MS);

    motor_t motor_l1;
    motor_t motor_l2;
    motor_init(&motor_l1, PIN_L1_EN, PIN_L1_DIR, PIN_L1_STEP, CONF_L1_DIR_INVERT);
    motor_init(&motor_l2, PIN_L2_EN, PIN_L2_DIR, PIN_L2_STEP, CONF_L2_DIR_INVERT);
    tmc_init(&g_tmc_l1, PIN_L1_UART_TX, PIN_L1_UART_RX, 0);
    tmc_init(&g_tmc_l2, PIN_L2_UART_TX, PIN_L2_UART_RX, 0);

    lane_setup(&g_lane_l1, PIN_L1_IN, PIN_L1_OUT, motor_l1, 1, &g_tmc_l1);
    lane_setup(&g_lane_l2, PIN_L2_IN, PIN_L2_OUT, motor_l2, 2, &g_tmc_l2);

    debounced_input_init(&g_y_split, PIN_Y_SPLIT);
    debounced_input_init(&g_buf_tension_din, PIN_BUF_TENSION);
    debounced_input_init(&g_buf_compression_din, PIN_BUF_COMPRESSION);

    adc_init();
    adc_gpio_init(PIN_PSF);

    neopixel_init(PIN_NEOPIXEL);

    settings_load();
    cutter_init();

    settle_boot_sensors();
    sync_init(to_ms_since_boot(get_absolute_time()));

    g_active_lane = detect_active_lane_from_out();
    if (g_active_lane == 0) {
        // Fall back: filament parked before OUT (pre-loaded state).
        // Pick lane 1 first; if only lane 2 has filament, pick lane 2.
        if (lane_in_present(&g_lane_l1) && !lane_out_present(&g_lane_l1))
            g_active_lane = 1;
        else if (lane_in_present(&g_lane_l2) && !lane_out_present(&g_lane_l2))
            g_active_lane = 2;
    }
    g_prev_lane1_in_present = lane_in_present(&g_lane_l1);
    g_prev_lane2_in_present = lane_in_present(&g_lane_l2);

    // One-shot boot buffer neutralization, deferred into the loop. NO retry for
    // now: a single attempt makes the settle-delay honest — if it STAGNANTs, the
    // delay is too short (raise BOOT_STAB_FIRST_MS) rather than being masked by a
    // retry. At ~750ms the motor/TMC + buffer signal were unsettled and the drive
    // hit the rail-break cap; tune BOOT_STAB_FIRST_MS until the single attempt
    // DONEs cleanly.
    bool boot_stab_armed = false;
    while (true) {
        g_now_ms = to_ms_since_boot(get_absolute_time());

        if (!boot_stab_armed && g_active_lane != 0 && g_now_ms >= BOOT_STAB_FIRST_MS) {
            boot_stab_armed = true;
            boot_stabilize_start(g_now_ms);
        }

        // Inputs
        debounced_input_update(&g_lane_l1.in_sw);
        debounced_input_update(&g_lane_l1.out_sw);
        debounced_input_update(&g_lane_l2.in_sw);
        debounced_input_update(&g_lane_l2.out_sw);
        debounced_input_update(&g_y_split);
        debounced_input_update(&g_buf_tension_din);
        debounced_input_update(&g_buf_compression_din);

        // USB commands
        cmd_poll(g_now_ms);

        // Background buffer neutralization: boot startup and optional post-print cleanup.
        buffer_stabilize_tick(g_now_ms);

        // State machines (order matters)
        cutter_tick(g_now_ms);
        tc_tick(g_now_ms);
        autopreload_tick(g_now_ms);
        lane_tick(&g_lane_l1, g_now_ms);
        lane_tick(&g_lane_l2, g_now_ms);
        buf_sensor_tick(g_now_ms);
        sync_tick(g_now_ms);

        // Local indicator
        neopixel_tick(g_now_ms);

        sleep_us(MAIN_LOOP_SLEEP_US);
    }
}
