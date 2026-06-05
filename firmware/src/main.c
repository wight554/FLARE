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

// ===================== Tunables =====================
int FEED_SPS = CONF_FEED_SPS;
int REV_SPS = CONF_REV_SPS;
int AUTO_SPS = CONF_AUTO_SPS;

int MOTION_STARTUP_MS = FLARE_INT_MOTION_STARTUP_MS;

int RUNOUT_COOLDOWN_MS = CONF_RUNOUT_COOLDOWN_MS;
int POST_PRINT_STAB_DELAY_MS = FLARE_INT_POST_PRINT_STAB_DELAY_MS;
float SYNC_PSF_DECAY_SPS_PER_S = FLARE_INT_SYNC_PSF_DECAY_SPS_PER_S;
int RELOAD_MODE = CONF_RELOAD_MODE;
int RELOAD_Y_TIMEOUT_MS = FLARE_INT_RELOAD_Y_TIMEOUT_MS;
int RELOAD_JOIN_DELAY_MS = CONF_RELOAD_JOIN_DELAY_MS;
int JOIN_SPS = CONF_JOIN_SPS;
int PRESS_SPS = CONF_PRESS_SPS;
int COMPRESSION_SPS = CONF_COMPRESSION_SPS;
int RELOAD_TOUCH_SETTLE_MS = CONF_RELOAD_TOUCH_SETTLE_MS;
int RELOAD_TOUCH_BOOST_MS = CONF_RELOAD_TOUCH_BOOST_MS;
int RELOAD_TOUCH_FLOOR_PCT = CONF_RELOAD_TOUCH_FLOOR_PCT;
int BUF_STAB_SPS = CONF_BUF_STAB_SPS; /* not persisted; settings_defaults() re-sets it, but the
                                         NVM-load path does not — must init non-zero or BS returns
                                         BUF_STAB_UNAVAILABLE on a board with saved settings */
int FOLLOW_TIMEOUT_MS[NUM_LANES] = {CONF_L1_FOLLOW_TIMEOUT_MS, CONF_L2_FOLLOW_TIMEOUT_MS};

int ZONE_BIAS_BASE_SPS = CONF_ZONE_BIAS_BASE_SPS;
int ZONE_BIAS_RAMP_SPS_S = CONF_ZONE_BIAS_RAMP_SPS_S;
int ZONE_BIAS_MAX_SPS = CONF_ZONE_BIAS_MAX_SPS;
float EST_ALPHA_MIN = FLARE_INT_EST_ALPHA_MIN;
float EST_ALPHA_MAX = FLARE_INT_EST_ALPHA_MAX;
float RELOAD_LEAN_FACTOR = FLARE_INT_RELOAD_LEAN_FACTOR;

int RAMP_STEP_SPS = CONF_RAMP_STEP_SPS;
int RAMP_TICK_MS = CONF_RAMP_TICK_MS;

int TMC_RUN_CURRENT_MA[NUM_LANES] = {CONF_L1_RUN_CURRENT_MA, CONF_L2_RUN_CURRENT_MA};
int TMC_HOLD_CURRENT_MA[NUM_LANES] = {CONF_L1_HOLD_CURRENT_MA, CONF_L2_HOLD_CURRENT_MA};
int TMC_MICROSTEPS[NUM_LANES] = {CONF_L1_MICROSTEPS, CONF_L2_MICROSTEPS};
int TMC_STEALTHCHOP_SPS[NUM_LANES] = {CONF_L1_STEALTHCHOP_THRESHOLD, CONF_L2_STEALTHCHOP_THRESHOLD};
float TMC_ROTATION_DISTANCE[NUM_LANES] = {CONF_L1_ROTATION_DISTANCE, CONF_L2_ROTATION_DISTANCE};
float TMC_GEAR_RATIO[NUM_LANES] = {CONF_L1_GEAR_RATIO, CONF_L2_GEAR_RATIO};
int TMC_FULL_STEPS[NUM_LANES] = {CONF_L1_FULL_STEPS, CONF_L2_FULL_STEPS};
int TMC_TBL[NUM_LANES] = {CONF_L1_TBL, CONF_L2_TBL};
int TMC_TOFF[NUM_LANES] = {CONF_L1_TOFF, CONF_L2_TOFF};
int TMC_HSTRT[NUM_LANES] = {CONF_L1_HSTRT, CONF_L2_HSTRT};
int TMC_HEND[NUM_LANES] = {CONF_L1_HEND, CONF_L2_HEND};
bool TMC_INTERPOLATE[NUM_LANES] = {CONF_L1_INTPOL, CONF_L2_INTPOL};

int BUF_SENSOR_TYPE = CONF_BUF_SENSOR_TYPE;
int BUF_HOME_STATE = CONF_BUF_HOME_STATE;
float BUF_PSF_MAX_COMP = CONF_BUF_PSF_MAX_COMP;
float BUF_PSF_MAX_TENS = CONF_BUF_PSF_MAX_TENS;
float BUF_PSF_NEUTRAL = CONF_BUF_PSF_NEUTRAL;
float BUF_GOAL = CONF_BUF_GOAL;
float BUF_ANALOG_ALPHA = FLARE_INT_BUF_ANALOG_ALPHA;
int SYNC_KP_SPS = CONF_SYNC_KP_SPS;
float KD_PSF = FLARE_INT_KD_PSF;
int SYNC_OVERSHOOT_PCT = FLARE_INT_SYNC_OVERSHOOT_PCT;
int SYNC_RESERVE_PCT = CONF_SYNC_RESERVE_PCT;
int TS_BUF_FALLBACK_MS = FLARE_INT_TS_BUF_FALLBACK_MS;

int SERVO_OPEN_US = CONF_SERVO_OPEN_US;
int SERVO_CLOSE_US = CONF_SERVO_CLOSE_US;
int SERVO_BLOCK_US = CONF_SERVO_BLOCK_US;
int SERVO_SETTLE_MS = CONF_SERVO_SETTLE_MS;
int CUT_FEED_SPS = CONF_CUT_FEED_SPS;
int CUT_FEED_MM = CONF_CUT_FEED_MM;
int CUT_LENGTH_MM = CONF_CUT_LENGTH_MM;
int CUT_AMOUNT = CONF_CUT_AMOUNT;
int CUT_TIMEOUT_SETTLE_MS = FLARE_INT_CUT_TIMEOUT_SETTLE_MS;
int CUT_TIMEOUT_FEED_MS = FLARE_INT_CUT_TIMEOUT_FEED_MS;

int TC_TIMEOUT_CUT_MS = FLARE_INT_TC_TIMEOUT_CUT_MS;
int LOAD_MAX_MM = CONF_LOAD_MAX_MM;
int UNLOAD_MAX_MM = CONF_UNLOAD_MAX_MM;
int UNLOAD_TENSION_BLOCK_MS = CONF_UNLOAD_TENSION_BLOCK_MS;
int TC_TIMEOUT_TH_MS = FLARE_INT_TC_TIMEOUT_TH_MS;
int TC_TIMEOUT_Y_MS = FLARE_INT_TC_TIMEOUT_Y_MS;

int SYNC_MAX_SPS = CONF_SYNC_MAX_SPS;
int GLOBAL_MAX_SPS = CONF_GLOBAL_MAX_SPS;
int SYNC_MIN_SPS = CONF_SYNC_MIN_SPS;
int SYNC_RAMP_UP_SPS = CONF_SYNC_RAMP_UP_SPS;
int SYNC_RAMP_DN_SPS = CONF_SYNC_RAMP_DN_SPS;
int SYNC_TICK_MS = CONF_SYNC_TICK_MS;
/* Type-P output smoothing — runtime-tunable, not persisted (re-init from CONF
   each boot, matching BUF_STAB_SPS). Tune live via SET; reflash changes the default. */
float SYNC_PSF_SLEW_PER_MM = CONF_SYNC_PSF_SLEW_PER_MM;
float SYNC_PSF_FILTER_MM = CONF_SYNC_PSF_FILTER_MM;
int PSF_STAB_STAGNANT_MS = CONF_PSF_STAB_STAGNANT_MS;
float PSF_STAB_STAGNANT_NORM = CONF_PSF_STAB_STAGNANT_NORM;
int PSF_STAB_RAIL_BREAK_MS = CONF_PSF_STAB_RAIL_BREAK_MS;
int PRE_RAMP_SPS = CONF_PRE_RAMP_SPS;
int BUF_HYST_MS = FLARE_INT_BUF_HYST_MS;
int BUF_PREDICT_THR_MS = FLARE_INT_BUF_PREDICT_THR_MS;
float BUF_SWITCH_SPAN_HALF_MM = CONF_BUF_SWITCH_SPAN_MM * 0.5f;
int SYNC_AUTO_STOP_MS = CONF_SYNC_AUTO_STOP_MS;
int SYNC_TENSION_DWELL_STOP_MS = FLARE_INT_SYNC_TENSION_DWELL_STOP_MS;
int SYNC_TENSION_RAMP_DELAY_MS = FLARE_INT_SYNC_TENSION_RAMP_DELAY_MS;
int SYNC_OVERSHOOT_NEUTRAL_EXTEND = FLARE_INT_SYNC_OVERSHOOT_NEUTRAL_EXTEND;
float SYNC_COMPRESSION_BIAS_FRAC = CONF_SYNC_COMPRESSION_BIAS_FRAC;
int NEUTRAL_CREEP_TIMEOUT_MS = FLARE_INT_NEUTRAL_CREEP_TIMEOUT_MS;
int NEUTRAL_CREEP_RATE_SPS_PER_S = FLARE_INT_NEUTRAL_CREEP_RATE_SPS_PER_S;
int NEUTRAL_CREEP_CAP_FRAC = FLARE_INT_NEUTRAL_CREEP_CAP_FRAC;
float BUF_VARIANCE_BLEND_FRAC = FLARE_INT_BUF_VARIANCE_BLEND_FRAC;
float BUF_VARIANCE_BLEND_REF_MM = FLARE_INT_BUF_VARIANCE_BLEND_REF_MM;
float SYNC_RESERVE_INTEGRAL_GAIN = FLARE_INT_SYNC_RESERVE_INTEGRAL_GAIN;
float SYNC_RESERVE_INTEGRAL_CLAMP_MM = FLARE_INT_SYNC_RESERVE_INTEGRAL_CLAMP_MM;
int SYNC_RESERVE_INTEGRAL_DECAY_MS = FLARE_INT_SYNC_RESERVE_INTEGRAL_DECAY_MS;
float EST_SIGMA_HARD_CAP_MM = FLARE_INT_EST_SIGMA_HARD_CAP_MM;
float EST_LOW_CF_WARN_THRESHOLD = FLARE_INT_EST_LOW_CF_WARN_THRESHOLD;
float EST_FALLBACK_CF_THRESHOLD = FLARE_INT_EST_FALLBACK_CF_THRESHOLD;
float RELAY_CATCHUP_FRAC = CONF_RELAY_CATCHUP_FRAC;
float RELAY_NEUTRAL_FRAC = CONF_RELAY_NEUTRAL_FRAC;
int SYNC_RELAY_TRIM_STEP_SPS = CONF_SYNC_RELAY_TRIM_STEP_SPS;
int SYNC_RELAY_TRIM_CLAMP_SPS = CONF_SYNC_RELAY_TRIM_CLAMP_SPS;
float SYNC_COMPRESSION_DRAIN_FRAC = CONF_SYNC_COMPRESSION_DRAIN_FRAC;
float SYNC_COMPRESSION_DRAIN_BUDGET_MM = CONF_SYNC_COMPRESSION_DRAIN_BUDGET_MM;
float SYNC_EST_ATTACK_ALPHA = CONF_SYNC_EST_ATTACK_ALPHA;
float SYNC_TENSION_FAST_MM_S = CONF_SYNC_TENSION_FAST_MM_S;
int SYNC_TENSION_PROBE_MAX_SPS = CONF_SYNC_TENSION_PROBE_MAX_SPS;
int SYNC_TENSION_PROBE_UP_SPS_PER_S = CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S;
int SYNC_TENSION_PROBE_DOWN_SPS_PER_S = CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S;
int SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S = CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S;
float RELAY_MIN_FLIP_MM = FLARE_INT_RELAY_MIN_FLIP_MM;
int RELAY_COLLAPSE_DELAY_MS = FLARE_INT_RELAY_COLLAPSE_DELAY_MS;
int RELAY_COLLAPSE_RAMP_MULT = FLARE_INT_RELAY_COLLAPSE_RAMP_MULT;
int RELAY_COLLAPSE_CAP_MS = FLARE_INT_RELAY_COLLAPSE_CAP_MS;
int BUF_DRIFT_EWMA_TAU_MS = FLARE_INT_BUF_DRIFT_EWMA_TAU_MS;
int BUF_DRIFT_MIN_SAMPLES = FLARE_INT_BUF_DRIFT_MIN_SAMPLES;
float BUF_DRIFT_APPLY_THR_MM = FLARE_INT_BUF_DRIFT_APPLY_THR_MM;
float BUF_DRIFT_CLAMP_MM = FLARE_INT_BUF_DRIFT_CLAMP_MM;
float BUF_DRIFT_APPLY_MIN_CF = FLARE_INT_BUF_DRIFT_APPLY_MIN_CF;
int TENSION_RISK_WINDOW_MS = FLARE_INT_TENSION_RISK_WINDOW_MS;
int TENSION_RISK_THRESHOLD = FLARE_INT_TENSION_RISK_THRESHOLD;
int AUTOLOAD_MAX_MM = CONF_AUTOLOAD_MAX_MM;
int AUTO_MODE = 1; // 1=Automated flow, 0=Host-controlled flow
bool AUTO_PRELOAD = true;
int AUTOLOAD_RETRACT_MM = CONF_AUTOLOAD_RETRACT_MM;
bool ENABLE_CUTTER = CONF_ENABLE_CUTTER;
bool UNLOAD_CUT = CONF_UNLOAD_CUT;

int DIST_IN_OUT = CONF_DIST_IN_OUT;
int DIST_OUT_Y = CONF_DIST_OUT_Y;
int DIST_Y_BUF = CONF_DIST_Y_BUF;
int BUF_BODY_LEN = CONF_BUF_BODY_LEN;
int BUF_MAX_TRAVEL_MM = CONF_BUF_MAX_TRAVEL_MM;

// Derived Physical Path Constants
#define Y_TO_BUF_NEUTRAL ((float)DIST_Y_BUF + (float)BUF_MAX_TRAVEL_MM / 2.0f)

float MM_PER_STEP[NUM_LANES] = {CONF_L1_MM_PER_STEP, CONF_L2_MM_PER_STEP};

int mm_per_min_to_sps_idx(float mm_per_min, int idx) {
    return (int)(mm_per_min / 60.0f / MM_PER_STEP[idx] + 0.5f);
}
int mm_per_min_to_sps(float mm_per_min) {
    return mm_per_min_to_sps_idx(mm_per_min, 0);
}
float sps_to_mm_per_min_idx(int sps, int idx) {
    return (float)sps * MM_PER_STEP[idx] * 60.0f + 0.05f; // Small offset for display rounding
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
    const float reff = CONF_RSENSE_OHM + 0.020f;
    const float vref = vsense ? 0.180f : 0.325f;
    const float sqrt2 = 1.41421356f;
    float irms = ((float)cs + 1.0f) * vref / (32.0f * reff * sqrt2);
    int ma = (int)(irms * 1000.0f + 0.5f);
    return clamp_i(ma, 0, 2000);
}

static uint8_t ma_to_cs(int ma, bool vsense) {
    if (ma <= 0)
        return 0;
    const float reff = CONF_RSENSE_OHM + 0.020f;
    const float vref = vsense ? 0.180f : 0.325f;
    const float sqrt2 = 1.41421356f;
    float irms = (float)ma / 1000.0f;
    int cs = (int)(32.0f * irms * reff * sqrt2 / vref - 1.0f + 0.5f);
    return (uint8_t)clamp_i(cs, 0, 31);
}

uint32_t build_ihold_irun_reg(int run_ma, int hold_ma, bool vsense) {
    uint8_t irun = ma_to_cs(run_ma, vsense);
    uint8_t ihold = ma_to_cs(hold_ma, vsense);
    return ((uint32_t)ihold) | ((uint32_t)irun << 8) | (8u << 16);
}

void sync_currents_from_ihold_irun(int lane, uint32_t reg) {
    int idx = lane_to_idx(lane);
    uint8_t ihold = (uint8_t)(reg & 0x1Fu);
    uint8_t irun = (uint8_t)((reg >> 8) & 0x1Fu);
    bool vsense = g_shadow_vsense[idx];
    TMC_RUN_CURRENT_MA[idx] = cs_to_ma(irun, vsense);
    TMC_HOLD_CURRENT_MA[idx] = cs_to_ma(ihold, vsense);
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
int active_lane = 0;
bool toolhead_has_filament = false;

bool prev_lane1_in_present = false;
bool prev_lane2_in_present = false;

// ===================== Forward declarations =====================
// Toolhead sensor state is always tracked, but in AUTO_MODE sync is governed
// by buffer state rather than TS events.
void set_toolhead_filament(bool present) {
    toolhead_has_filament = present;
    if (!AUTO_MODE) {
        if (present)
            sync_set_state(SYNC_ACTIVE);
        else
            sync_disable(false);
        if (!present) {
            sync_current_sps = 0;
            sync_auto_started = false;
            sync_tail_assist_active = false;
            sync_idle_since_ms = 0;
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
    if (active_lane != lane && (active_lane == 1 || active_lane == 2) && (lane == 1 || lane == 2)) {
        int old_idx = active_lane - 1;
        int new_idx = lane - 1;
        if (MM_PER_STEP[new_idx] > 1e-6f) {
            extruder_est_sps = extruder_est_sps * (MM_PER_STEP[old_idx] / MM_PER_STEP[new_idx]);
        }
    }
    active_lane = lane;
    if (lane == 1 || lane == 2) {
        char lane_s[2] = {(char)('0' + lane), 0};
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
    if (!AUTO_MODE && !AUTO_PRELOAD) {
        prev_lane1_in_present = lane_in_present(&g_lane_l1);
        prev_lane2_in_present = lane_in_present(&g_lane_l2);
        return;
    }

    /* Suppress auto-load only while a BL motor drive is actually moving
     * the lane (PRIME or CATCH). The IN-sensor rising edge while the
     * motor is pulling filament past the sensor would otherwise spuriously
     * launch TASK_LOAD_FULL. During BL_LOCKED the motor is at zero feed —
     * any IN rising edge there is a real operator insertion and must NOT
     * be consumed. After release (BL_IDLE) the gate doesn't apply. */
    if (sync_buffer_lock_motor_moving()) {
        prev_lane1_in_present = lane_in_present(&g_lane_l1);
        prev_lane2_in_present = lane_in_present(&g_lane_l2);
        return;
    }

    bool in1 = lane_in_present(&g_lane_l1);
    bool in2 = lane_in_present(&g_lane_l2);

    // MMU is completely empty if neither OUT sensor is present.
    bool mmu_empty = !lane_out_present(&g_lane_l1) && !lane_out_present(&g_lane_l2);

    if (in1 && !prev_lane1_in_present) {
        if (g_lane_l1.fault == FAULT_DRY_SPIN)
            g_lane_l1.fault = FAULT_NONE;
        if (g_tc_ctx.state == TC_ERROR)
            tc_abort();
        if (g_lane_l1.task == TASK_IDLE &&
            (tc_state() == TC_IDLE || tc_state() == TC_RELOAD_FOLLOW) && !cutter_busy() &&
            !lane_out_present(&g_lane_l1)) {
            if (AUTO_MODE && mmu_empty) {
                // Completely empty MMU: auto-load all the way to toolhead.
                lane_start(&g_lane_l1, TASK_LOAD_FULL, FEED_SPS, true, now_ms, (float)LOAD_MAX_MM);
                cmd_event("AUTO_LOAD", "1");
            } else if (AUTO_PRELOAD) {
                // Other lane loaded (or AUTO_MODE off): just preload to Y-splitter.
                lane_start(&g_lane_l1, TASK_AUTOLOAD, AUTO_SPS, true, now_ms,
                           (float)AUTOLOAD_MAX_MM);
                cmd_event("PRELOAD", "1");
            }
            if (!lane_out_present(&g_lane_l2))
                set_active_lane(1);
        }
    }

    if (in2 && !prev_lane2_in_present) {
        if (g_lane_l2.fault == FAULT_DRY_SPIN)
            g_lane_l2.fault = FAULT_NONE;
        if (g_tc_ctx.state == TC_ERROR)
            tc_abort();
        if (g_lane_l2.task == TASK_IDLE &&
            (tc_state() == TC_IDLE || tc_state() == TC_RELOAD_FOLLOW) && !cutter_busy() &&
            !lane_out_present(&g_lane_l2)) {
            if (AUTO_MODE && mmu_empty) {
                lane_start(&g_lane_l2, TASK_LOAD_FULL, FEED_SPS, true, now_ms, (float)LOAD_MAX_MM);
                cmd_event("AUTO_LOAD", "2");
            } else if (AUTO_PRELOAD) {
                lane_start(&g_lane_l2, TASK_AUTOLOAD, AUTO_SPS, true, now_ms,
                           (float)AUTOLOAD_MAX_MM);
                cmd_event("PRELOAD", "2");
            }
            if (!lane_out_present(&g_lane_l1))
                set_active_lane(2);
        }
    }

    prev_lane1_in_present = in1;
    prev_lane2_in_present = in2;
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
    if (sync_enabled && sync_current_sps > 0)
        return LED_ACTIVE;
    return LED_IDLE;
}

static void neopixel_tick(uint32_t now_ms) {
    static uint32_t last_ms = 0;
    if ((now_ms - last_ms) < 50u)
        return;
    last_ms = now_ms;

    switch (led_state_from_system()) {
    case LED_IDLE:
        neopixel_set(0, 20, 0);
        break;
    case LED_LOADING:
        neopixel_set(0, 0, 120);
        break;
    case LED_ACTIVE:
        neopixel_set(0, 200, 0);
        break;
    case LED_TC:
        neopixel_set(180, 140, 0);
        break;
    case LED_ERROR:
        neopixel_set(200, 0, 0);
        break;
    case LED_CUTTING: {
        uint8_t phase = (uint8_t)((now_ms / 32u) & 0x0Fu);
        uint8_t brightness = (phase < 8u) ? (uint8_t)(phase * 32u) : (uint8_t)((15u - phase) * 32u);
        neopixel_set(brightness, brightness, brightness);
        break;
    }
    }
}

static void settle_boot_sensors(void) {
    // debounced_input_init reads GPIOs once without debounce; sensors may not have settled.
    // Spin debounced_input_update for 25 ms so the 10 ms debounce threshold commits correctly.
    // For Type-P, also poll the ADC pin to let the EWMA filter settle to the true physical value.
    for (int i = 0; i < 25; i++) {
        debounced_input_update(&g_lane_l1.in_sw);
        debounced_input_update(&g_lane_l1.out_sw);
        debounced_input_update(&g_lane_l2.in_sw);
        debounced_input_update(&g_lane_l2.out_sw);
        debounced_input_update(&g_y_split);
        debounced_input_update(&g_buf_tension_din);
        debounced_input_update(&g_buf_compression_din);
        if (BUF_SENSOR_TYPE == 1) {
            buf_analog_update();
        }
        sleep_ms(1);
    }
}

// ===================== Main =====================
int main(void) {
    stdio_init_all();
    sleep_ms(200);

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

    active_lane = detect_active_lane_from_out();
    if (active_lane == 0) {
        // Fall back: filament parked before OUT (pre-loaded state).
        // Pick lane 1 first; if only lane 2 has filament, pick lane 2.
        if (lane_in_present(&g_lane_l1) && !lane_out_present(&g_lane_l1))
            active_lane = 1;
        else if (lane_in_present(&g_lane_l2) && !lane_out_present(&g_lane_l2))
            active_lane = 2;
    }
    prev_lane1_in_present = lane_in_present(&g_lane_l1);
    prev_lane2_in_present = lane_in_present(&g_lane_l2);

    // One-shot boot buffer neutralization, deferred into the loop. NO retry for
    // now: a single attempt makes the settle-delay honest — if it STAGNANTs, the
    // delay is too short (raise BOOT_STAB_FIRST_MS) rather than being masked by a
    // retry. At ~750ms the motor/TMC + buffer signal were unsettled and the drive
    // hit the rail-break cap; tune BOOT_STAB_FIRST_MS until the single attempt
    // DONEs cleanly.
    bool boot_stab_armed = false;
    const uint32_t BOOT_STAB_FIRST_MS = 2500;

    while (true) {
        g_now_ms = to_ms_since_boot(get_absolute_time());

        if (!boot_stab_armed && active_lane != 0 && g_now_ms >= BOOT_STAB_FIRST_MS) {
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

        sleep_us(100);
    }
}
