/// @file settings_store.c
/// @brief Flash-backed persistence: settings_t defaults, save/load, versioning,
///        and TMC apply helpers.
/// @details Activity-gated saves (never write flash mid-motion). Bump
///          SETTINGS_VERSION whenever a settings_t field is added/removed. See spec
///          persistence-contract; CONTEXT.md settings pattern.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pico/flash.h"

#include "hardware/flash.h"
#include "hardware/sync.h"

#include "controller_shared.h"
#include "motion.h"
#include "settings_store.h"
#include "sync.h"

#define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC 0x4e4f5346u
#define SETTINGS_VERSION 59u

enum {
    SETTINGS_FLASH_BUFFER_BYTES = 512,
    CRC32_BITS_PER_BYTE = 8,
    BUF_STAB_MIN_SPS = 10,
    BUF_STAB_MAX_SPS = 10000,
    BUF_TRAVEL_MIN_MM = 10,
    BUF_TRAVEL_MAX_MM = 1000,
    SYNC_RESERVE_MAX_PCT = 150,
    TMC_VSENSE_THRESHOLD_MA = 980,
};

static const uint32_t CRC32_INITIAL_VALUE = 0xFFFFFFFFu;
static const uint32_t CRC32_POLYNOMIAL = 0xEDB88320u;
static const float BUF_SWITCH_SPAN_MIN_MM = 2.0f;
static const float GLOBAL_MAX_MIN_MM_MIN = 1000.0f;
static const float GLOBAL_MAX_MAX_MM_MIN = 12000.0f;
static const float RELAY_FRAC_MIN = 0.5f;
static const float RELAY_FRAC_MAX = 3.0f;
static const float COMPRESSION_DRAIN_MAX_FRAC = 0.9f;
static const float COMPRESSION_DRAIN_BUDGET_MAX_MM = 25.0f;
static const float SYNC_EST_ATTACK_MIN_ALPHA = 0.65f;
static const float SYNC_TENSION_FAST_MAX_MM_S = 200.0f;
static const float TENSION_PROBE_MAX_MM_MIN = 6000.0f;
static const float TENSION_PROBE_RAMP_MAX_MM_MIN = 12000.0f;
static const float COMPRESSION_BIAS_MAX_FRAC = 0.7f;

typedef struct {
    uint32_t magic;
    uint32_t version;

    int feed_sps, rev_sps, auto_sps;
    int sync_max_sps, global_max_sps, sync_min_sps;
    int sync_auto_stop_ms;
    int load_max_mm;
    int unload_max_mm;
    int unload_tension_block_ms;
    int reload_join_delay_ms;
    int autoload_max_mm;
    int auto_mode;
    int dist_in_out, dist_out_y, dist_y_buf, buf_body_len, buf_max_travel_mm;
    float buf_switch_span_mm;
    int baseline_sps;
    int autoload_retract_mm;

    int servo_open_us, servo_close_us, servo_block_us;
    int servo_settle_ms;
    int cut_feed_sps;
    int cut_feed_mm, cut_length_mm, cut_amount;

    int runout_cooldown_ms;

    int buf_sensor_type;
    int buf_home_state;
    float buf_psf_max_comp, buf_psf_max_tens, buf_psf_neutral, buf_psf_goal;
    int sync_kp_sps;
    int sync_reserve_pct;

    int join_sps;
    int press_sps;
    int compression_sps;
    int follow_timeout_ms[NUM_LANES];

    bool auto_preload;
    bool enable_cutter;
    bool unload_cut;
    bool reload_mode;

    float tmc_rotation_distance[NUM_LANES];
    float tmc_gear_ratio[NUM_LANES];
    int tmc_full_steps[NUM_LANES];
    int tmc_microsteps[NUM_LANES];
    int tmc_tbl[NUM_LANES], tmc_toff[NUM_LANES], tmc_hstrt[NUM_LANES], tmc_hend[NUM_LANES];
    bool tmc_interpolate[NUM_LANES];
    int tmc_stealthchop_sps[NUM_LANES];
    int tmc_run_current_ma[NUM_LANES], tmc_hold_current_ma[NUM_LANES];

    float relay_catchup_frac;
    float relay_neutral_frac;

    float sync_compression_bias_frac;

    uint32_t crc32;
} settings_t;

_Static_assert(sizeof(settings_t) <= SETTINGS_FLASH_BUFFER_BYTES,
               "settings_t exceeds two flash pages - expand buffer in settings_save()");

static uint32_t crc32_buf(const uint8_t *data, size_t len) {
    uint32_t crc = CRC32_INITIAL_VALUE;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < CRC32_BITS_PER_BYTE; j++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (CRC32_POLYNOMIAL & mask);
        }
    }
    return ~crc;
}

static float buf_switch_span_half_from_full(float span_mm, int max_travel_mm) {
    float max_span_mm = (float)max_travel_mm;
    if (max_span_mm < BUF_SWITCH_SPAN_MIN_MM)
        max_span_mm = BUF_SWITCH_SPAN_MIN_MM;
    return clamp_f(span_mm, BUF_SWITCH_SPAN_MIN_MM, max_span_mm) * HALF_F;
}

static void settings_defaults_tmc(void) {
    const int follow_timeout_ms[NUM_LANES] = {CONF_L1_FOLLOW_TIMEOUT_MS, CONF_L2_FOLLOW_TIMEOUT_MS};
    const int run_current_ma[NUM_LANES] = {CONF_L1_RUN_CURRENT_MA, CONF_L2_RUN_CURRENT_MA};
    const int hold_current_ma[NUM_LANES] = {CONF_L1_HOLD_CURRENT_MA, CONF_L2_HOLD_CURRENT_MA};
    const int microsteps[NUM_LANES] = {CONF_L1_MICROSTEPS, CONF_L2_MICROSTEPS};
    const int stealthchop_sps[NUM_LANES] = {CONF_L1_STEALTHCHOP_THRESHOLD,
                                            CONF_L2_STEALTHCHOP_THRESHOLD};

    for (int i = 0; i < NUM_LANES; i++) {
        FOLLOW_TIMEOUT_MS[i] = follow_timeout_ms[i];
        TMC_RUN_CURRENT_MA[i] = run_current_ma[i];
        TMC_HOLD_CURRENT_MA[i] = hold_current_ma[i];
        TMC_MICROSTEPS[i] = microsteps[i];
        TMC_STEALTHCHOP_SPS[i] = stealthchop_sps[i];
    }

    MM_PER_STEP[0] = CONF_L1_MM_PER_STEP;
    MM_PER_STEP[1] = CONF_L2_MM_PER_STEP;

    TMC_ROTATION_DISTANCE[0] = CONF_L1_ROTATION_DISTANCE;
    TMC_ROTATION_DISTANCE[1] = CONF_L2_ROTATION_DISTANCE;
    TMC_GEAR_RATIO[0] = CONF_L1_GEAR_RATIO;
    TMC_GEAR_RATIO[1] = CONF_L2_GEAR_RATIO;
    TMC_FULL_STEPS[0] = CONF_L1_FULL_STEPS;
    TMC_FULL_STEPS[1] = CONF_L2_FULL_STEPS;
    TMC_MICROSTEPS[0] = CONF_L1_MICROSTEPS;
    TMC_MICROSTEPS[1] = CONF_L2_MICROSTEPS;
    TMC_TBL[0] = CONF_L1_TBL;
    TMC_TBL[1] = CONF_L2_TBL;
    TMC_TOFF[0] = CONF_L1_TOFF;
    TMC_TOFF[1] = CONF_L2_TOFF;
    TMC_HSTRT[0] = CONF_L1_HSTRT;
    TMC_HSTRT[1] = CONF_L2_HSTRT;
    TMC_HEND[0] = CONF_L1_HEND;
    TMC_HEND[1] = CONF_L2_HEND;
    TMC_INTERPOLATE[0] = CONF_L1_INTPOL;
    TMC_INTERPOLATE[1] = CONF_L2_INTPOL;
    TMC_STEALTHCHOP_SPS[0] = CONF_L1_STEALTHCHOP_THRESHOLD;
    TMC_STEALTHCHOP_SPS[1] = CONF_L2_STEALTHCHOP_THRESHOLD;
    TMC_RUN_CURRENT_MA[0] = CONF_L1_RUN_CURRENT_MA;
    TMC_RUN_CURRENT_MA[1] = CONF_L2_RUN_CURRENT_MA;
    TMC_HOLD_CURRENT_MA[0] = CONF_L1_HOLD_CURRENT_MA;
    TMC_HOLD_CURRENT_MA[1] = CONF_L2_HOLD_CURRENT_MA;
}

static void settings_defaults_sync(void) {
    BUF_SENSOR_TYPE = CONF_BUF_SENSOR_TYPE;
    BUF_HOME_STATE = CONF_BUF_HOME_STATE;
    BUF_PSF_MAX_COMP = CONF_BUF_PSF_MAX_COMP;
    BUF_PSF_MAX_TENS = CONF_BUF_PSF_MAX_TENS;
    BUF_PSF_NEUTRAL = CONF_BUF_PSF_NEUTRAL;
    BUF_GOAL = CONF_BUF_GOAL;
    SYNC_KP_SPS = CONF_SYNC_KP_SPS;
    SYNC_RESERVE_PCT = clamp_i(CONF_SYNC_RESERVE_PCT, 0, SYNC_RESERVE_MAX_PCT);
    RELAY_CATCHUP_FRAC = clamp_f(CONF_RELAY_CATCHUP_FRAC, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    RELAY_NEUTRAL_FRAC = clamp_f(CONF_RELAY_NEUTRAL_FRAC, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    SYNC_COMPRESSION_DRAIN_FRAC =
        clamp_f(CONF_SYNC_COMPRESSION_DRAIN_FRAC, 0.0f, COMPRESSION_DRAIN_MAX_FRAC);
    SYNC_COMPRESSION_DRAIN_BUDGET_MM =
        clamp_f(CONF_SYNC_COMPRESSION_DRAIN_BUDGET_MM, 0.0f, COMPRESSION_DRAIN_BUDGET_MAX_MM);
    SYNC_EST_ATTACK_ALPHA = clamp_f(CONF_SYNC_EST_ATTACK_ALPHA, SYNC_EST_ATTACK_MIN_ALPHA, 1.0f);
    SYNC_TENSION_FAST_MM_S = clamp_f(CONF_SYNC_TENSION_FAST_MM_S, 1.0f, SYNC_TENSION_FAST_MAX_MM_S);
    SYNC_TENSION_PROBE_MAX_SPS =
        clamp_i(CONF_SYNC_TENSION_PROBE_MAX_SPS, 0, mm_per_min_to_sps(TENSION_PROBE_MAX_MM_MIN));
    SYNC_TENSION_PROBE_UP_SPS_PER_S = clamp_i(CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S, 0,
                                              mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    SYNC_TENSION_PROBE_DOWN_SPS_PER_S = clamp_i(CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S, 0,
                                                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S =
        clamp_i(CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S, 0,
                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));

    SYNC_COMPRESSION_BIAS_FRAC =
        clamp_f(CONF_SYNC_COMPRESSION_BIAS_FRAC, 0.0f, COMPRESSION_BIAS_MAX_FRAC);
    flow_schedule_reset_runtime();

    BUF_STAB_SPS = clamp_i(CONF_BUF_STAB_SPS, BUF_STAB_MIN_SPS, BUF_STAB_MAX_SPS);
    JOIN_SPS = CONF_JOIN_SPS;
    PRESS_SPS = CONF_PRESS_SPS;
    COMPRESSION_SPS = CONF_COMPRESSION_SPS;
}

static void settings_defaults_motion(void) {
    FEED_SPS = CONF_FEED_SPS;
    REV_SPS = CONF_REV_SPS;
    AUTO_SPS = CONF_AUTO_SPS;

    GLOBAL_MAX_SPS = clamp_i(CONF_GLOBAL_MAX_SPS, mm_per_min_to_sps(GLOBAL_MAX_MIN_MM_MIN),
                             mm_per_min_to_sps(GLOBAL_MAX_MAX_MM_MIN));
    SYNC_MAX_SPS = sync_clamp_max_sps(CONF_SYNC_MAX_SPS);
    SYNC_MIN_SPS = CONF_SYNC_MIN_SPS;
    SYNC_RAMP_UP_SPS = CONF_SYNC_RAMP_UP_SPS;
    SYNC_RAMP_DN_SPS = CONF_SYNC_RAMP_DN_SPS;
    PRE_RAMP_SPS = CONF_PRE_RAMP_SPS;
    SYNC_AUTO_STOP_MS = CONF_SYNC_AUTO_STOP_MS;
    AUTOLOAD_MAX_MM = CONF_AUTOLOAD_MAX_MM;
    LOAD_MAX_MM = CONF_LOAD_MAX_MM;
    UNLOAD_MAX_MM = CONF_UNLOAD_MAX_MM;
    UNLOAD_TENSION_BLOCK_MS = CONF_UNLOAD_TENSION_BLOCK_MS;
    RELOAD_JOIN_DELAY_MS = CONF_RELOAD_JOIN_DELAY_MS;
    RELOAD_MODE = CONF_RELOAD_MODE;
    AUTO_MODE = 1;
    AUTO_PRELOAD = true;
    DIST_IN_OUT = CONF_DIST_IN_OUT;
    DIST_OUT_Y = CONF_DIST_OUT_Y;
    DIST_Y_BUF = CONF_DIST_Y_BUF;
    BUF_BODY_LEN = CONF_BUF_BODY_LEN;
    BUF_MAX_TRAVEL_MM = clamp_i(CONF_BUF_MAX_TRAVEL_MM, BUF_TRAVEL_MIN_MM, BUF_TRAVEL_MAX_MM);
    BUF_SWITCH_SPAN_HALF_MM =
        buf_switch_span_half_from_full(CONF_BUF_SWITCH_SPAN_MM, BUF_MAX_TRAVEL_MM);
    ZONE_BIAS_BASE_SPS = CONF_ZONE_BIAS_BASE_SPS;
    ZONE_BIAS_RAMP_SPS_S = CONF_ZONE_BIAS_RAMP_SPS_S;
    ZONE_BIAS_MAX_SPS = CONF_ZONE_BIAS_MAX_SPS;
    g_baseline_target_sps = CONF_BASELINE_SPS;
    g_baseline_sps = CONF_BASELINE_SPS;
    AUTO_PRELOAD = true;
    AUTOLOAD_RETRACT_MM = CONF_AUTOLOAD_RETRACT_MM;
    ENABLE_CUTTER = CONF_ENABLE_CUTTER;
    UNLOAD_CUT = CONF_UNLOAD_CUT;
    RAMP_STEP_SPS = CONF_RAMP_STEP_SPS;
}

static void settings_defaults_servo_cutter(void) {
    SERVO_OPEN_US = CONF_SERVO_OPEN_US;
    SERVO_CLOSE_US = CONF_SERVO_CLOSE_US;
    SERVO_BLOCK_US = CONF_SERVO_BLOCK_US;
    SERVO_SETTLE_MS = CONF_SERVO_SETTLE_MS;
    CUT_FEED_SPS = CONF_CUT_FEED_SPS;
    CUT_FEED_MM = CONF_CUT_FEED_MM;
    CUT_LENGTH_MM = CONF_CUT_LENGTH_MM;
    CUT_AMOUNT = CONF_CUT_AMOUNT;

    RUNOUT_COOLDOWN_MS = CONF_RUNOUT_COOLDOWN_MS;
}

void settings_defaults(void) {
    settings_defaults_motion();
    settings_defaults_tmc();
    settings_defaults_servo_cutter();
    settings_defaults_sync();

    motion_limit_runtime_rates(false);
}

void settings_save(void) {
    settings_t s = {0};
    s.magic = SETTINGS_MAGIC;
    s.version = SETTINGS_VERSION;

    s.feed_sps = FEED_SPS;
    s.rev_sps = REV_SPS;
    s.auto_sps = AUTO_SPS;

    s.sync_max_sps = SYNC_MAX_SPS;
    s.global_max_sps = GLOBAL_MAX_SPS;
    s.sync_min_sps = SYNC_MIN_SPS;
    s.sync_auto_stop_ms = SYNC_AUTO_STOP_MS;
    s.autoload_max_mm = AUTOLOAD_MAX_MM;
    s.load_max_mm = LOAD_MAX_MM;
    s.unload_max_mm = UNLOAD_MAX_MM;
    s.unload_tension_block_ms = UNLOAD_TENSION_BLOCK_MS;
    s.reload_join_delay_ms = RELOAD_JOIN_DELAY_MS;
    s.auto_mode = AUTO_MODE;
    s.auto_preload = AUTO_PRELOAD ? 1 : 0;
    s.buf_switch_span_mm = BUF_SWITCH_SPAN_HALF_MM * FULL_SPAN_MULT_F;
    s.dist_in_out = DIST_IN_OUT;
    s.dist_out_y = DIST_OUT_Y;
    s.dist_y_buf = DIST_Y_BUF;
    s.buf_body_len = BUF_BODY_LEN;
    s.buf_max_travel_mm = BUF_MAX_TRAVEL_MM;
    s.baseline_sps = g_baseline_target_sps;
    s.auto_preload = AUTO_PRELOAD;
    s.autoload_retract_mm = AUTOLOAD_RETRACT_MM;
    s.enable_cutter = ENABLE_CUTTER;
    s.unload_cut = UNLOAD_CUT;

    s.servo_open_us = SERVO_OPEN_US;
    s.servo_close_us = SERVO_CLOSE_US;
    s.servo_block_us = SERVO_BLOCK_US;
    s.servo_settle_ms = SERVO_SETTLE_MS;
    s.cut_feed_sps = CUT_FEED_SPS;
    s.cut_feed_mm = CUT_FEED_MM;
    s.cut_length_mm = CUT_LENGTH_MM;
    s.cut_amount = CUT_AMOUNT;

    s.runout_cooldown_ms = RUNOUT_COOLDOWN_MS;

    s.buf_sensor_type = BUF_SENSOR_TYPE;
    s.buf_home_state = BUF_HOME_STATE;
    s.buf_psf_max_comp = BUF_PSF_MAX_COMP;
    s.buf_psf_max_tens = BUF_PSF_MAX_TENS;
    s.buf_psf_neutral = BUF_PSF_NEUTRAL;
    s.buf_psf_goal = BUF_GOAL;
    s.sync_kp_sps = SYNC_KP_SPS;
    s.sync_reserve_pct = SYNC_RESERVE_PCT;
    s.join_sps = JOIN_SPS;
    s.press_sps = PRESS_SPS;
    s.compression_sps = COMPRESSION_SPS;

    s.reload_mode = (bool)RELOAD_MODE;
    for (int i = 0; i < NUM_LANES; i++) {
        s.follow_timeout_ms[i] = FOLLOW_TIMEOUT_MS[i];
    }

    s.relay_catchup_frac = RELAY_CATCHUP_FRAC;
    s.relay_neutral_frac = RELAY_NEUTRAL_FRAC;

    s.sync_compression_bias_frac = SYNC_COMPRESSION_BIAS_FRAC;

    for (int i = 0; i < NUM_LANES; i++) {
        s.tmc_rotation_distance[i] = TMC_ROTATION_DISTANCE[i];
        s.tmc_gear_ratio[i] = TMC_GEAR_RATIO[i];
        s.tmc_full_steps[i] = TMC_FULL_STEPS[i];
        s.tmc_microsteps[i] = TMC_MICROSTEPS[i];
        s.tmc_tbl[i] = TMC_TBL[i];
        s.tmc_toff[i] = TMC_TOFF[i];
        s.tmc_hstrt[i] = TMC_HSTRT[i];
        s.tmc_hend[i] = TMC_HEND[i];
        s.tmc_interpolate[i] = TMC_INTERPOLATE[i];
        s.tmc_stealthchop_sps[i] = TMC_STEALTHCHOP_SPS[i];
        s.tmc_run_current_ma[i] = TMC_RUN_CURRENT_MA[i];
        s.tmc_hold_current_ma[i] = TMC_HOLD_CURRENT_MA[i];
    }

    // CRC covers every field up to (not including) crc32 itself; settings_load
    // recomputes it and rejects the sector if it doesn't match (corrupt/partial write).
    s.crc32 = crc32_buf((const uint8_t *)&s, offsetof(settings_t, crc32));

    uint8_t buffer[SETTINGS_FLASH_BUFFER_BYTES] = {0};
    memcpy(buffer, &s, sizeof(s));

    // Writing flash on the RP2040 stalls code execution from flash (XIP). Stop all
    // motion first (no steps will be generated during the write), then disable
    // interrupts so no ISR tries to run from flash mid-erase. Erase the sector,
    // then program the buffer. Caller must guarantee we are not mid-motion.
    stop_all();

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_FLASH_OFFSET, buffer, SETTINGS_FLASH_BUFFER_BYTES);
    restore_interrupts(ints);
}

void sync_tmc_settings(int lane) {
    int idx = lane_to_idx(lane);
    tmc_t *tmc = (lane == 1) ? &g_tmc_l1 : &g_tmc_l2;

    MM_PER_STEP[idx] =
        TMC_ROTATION_DISTANCE[idx] /
        ((float)TMC_FULL_STEPS[idx] * TMC_GEAR_RATIO[idx] * (float)TMC_MICROSTEPS[idx]);

    tmc_setup_chopconf(tmc, TMC_MICROSTEPS[idx], TMC_TOFF[idx], TMC_TBL[idx], TMC_HSTRT[idx],
                       TMC_HEND[idx], TMC_INTERPOLATE[idx]);
    tmc_set_stealthchop_sps(tmc, TMC_STEALTHCHOP_SPS[idx], TMC_MICROSTEPS[idx]);
    tmc_set_run_current_ma(tmc, TMC_RUN_CURRENT_MA[idx], TMC_HOLD_CURRENT_MA[idx]);

    // Synchronize shadow state for protocol reporting
    g_shadow_vsense[idx] = (TMC_RUN_CURRENT_MA[idx] <= TMC_VSENSE_THRESHOLD_MA);
    g_shadow_ihold_irun[idx] = build_ihold_irun_reg(TMC_RUN_CURRENT_MA[idx],
                                                    TMC_HOLD_CURRENT_MA[idx], g_shadow_vsense[idx]);
    g_shadow_ihold_irun_valid[idx] = true;
}

static void tmc_apply_all(void) {
    tmc_set_pwmconf(&g_tmc_l1);
    tmc_set_pwmconf(&g_tmc_l2);
    tmc_set_stealthchop_sps(&g_tmc_l1, TMC_STEALTHCHOP_SPS[0], TMC_MICROSTEPS[0]);
    tmc_set_stealthchop_sps(&g_tmc_l2, TMC_STEALTHCHOP_SPS[1], TMC_MICROSTEPS[1]);
    tmc_setup_chopconf(&g_tmc_l1, TMC_MICROSTEPS[0], TMC_TOFF[0], TMC_TBL[0], TMC_HSTRT[0],
                       TMC_HEND[0], TMC_INTERPOLATE[0]);
    tmc_setup_chopconf(&g_tmc_l2, TMC_MICROSTEPS[1], TMC_TOFF[1], TMC_TBL[1], TMC_HSTRT[1],
                       TMC_HEND[1], TMC_INTERPOLATE[1]);
    tmc_set_run_current_ma(&g_tmc_l1, TMC_RUN_CURRENT_MA[0], TMC_HOLD_CURRENT_MA[0]);
    tmc_set_run_current_ma(&g_tmc_l2, TMC_RUN_CURRENT_MA[1], TMC_HOLD_CURRENT_MA[1]);

    g_shadow_vsense[0] = (TMC_RUN_CURRENT_MA[0] <= TMC_VSENSE_THRESHOLD_MA);
    g_shadow_vsense[1] = (TMC_RUN_CURRENT_MA[1] <= TMC_VSENSE_THRESHOLD_MA);
    g_shadow_ihold_irun[0] =
        build_ihold_irun_reg(TMC_RUN_CURRENT_MA[0], TMC_HOLD_CURRENT_MA[0], g_shadow_vsense[0]);
    g_shadow_ihold_irun[1] =
        build_ihold_irun_reg(TMC_RUN_CURRENT_MA[1], TMC_HOLD_CURRENT_MA[1], g_shadow_vsense[1]);
    g_shadow_ihold_irun_valid[0] = true;
    g_shadow_ihold_irun_valid[1] = true;
}

static void settings_load_motion(const settings_t *s) {
    FEED_SPS = s->feed_sps;
    REV_SPS = s->rev_sps;
    AUTO_SPS = s->auto_sps;

    GLOBAL_MAX_SPS = clamp_i(s->global_max_sps, mm_per_min_to_sps(GLOBAL_MAX_MIN_MM_MIN),
                             mm_per_min_to_sps(GLOBAL_MAX_MAX_MM_MIN));
    SYNC_MAX_SPS = sync_clamp_max_sps(s->sync_max_sps);
    SYNC_MIN_SPS = s->sync_min_sps;
    SYNC_AUTO_STOP_MS = s->sync_auto_stop_ms;
    AUTOLOAD_MAX_MM = s->autoload_max_mm;
    LOAD_MAX_MM = s->load_max_mm;
    UNLOAD_MAX_MM = s->unload_max_mm;
    UNLOAD_TENSION_BLOCK_MS = s->unload_tension_block_ms;
    RELOAD_JOIN_DELAY_MS = s->reload_join_delay_ms;
    AUTO_MODE = s->auto_mode;
    AUTO_PRELOAD = (s->auto_preload != 0);
    BUF_MAX_TRAVEL_MM = clamp_i(s->buf_max_travel_mm, BUF_TRAVEL_MIN_MM, BUF_TRAVEL_MAX_MM);
    BUF_SWITCH_SPAN_HALF_MM =
        buf_switch_span_half_from_full(s->buf_switch_span_mm, BUF_MAX_TRAVEL_MM);
    DIST_IN_OUT = s->dist_in_out;
    DIST_OUT_Y = s->dist_out_y;
    DIST_Y_BUF = s->dist_y_buf;
    BUF_BODY_LEN = s->buf_body_len;
    g_baseline_target_sps = motion_clamp_rate_sps(s->baseline_sps);
    g_baseline_sps = g_baseline_target_sps;
    AUTO_PRELOAD = s->auto_preload;
    AUTOLOAD_RETRACT_MM = s->autoload_retract_mm;
    ENABLE_CUTTER = s->enable_cutter;
    UNLOAD_CUT = s->unload_cut;
}

static void settings_load_tmc(const settings_t *s) {
    for (int i = 0; i < NUM_LANES; i++) {
        FOLLOW_TIMEOUT_MS[i] = s->follow_timeout_ms[i];
        TMC_ROTATION_DISTANCE[i] = s->tmc_rotation_distance[i];
        TMC_GEAR_RATIO[i] = s->tmc_gear_ratio[i];
        TMC_FULL_STEPS[i] = s->tmc_full_steps[i];
        TMC_MICROSTEPS[i] = s->tmc_microsteps[i];
        TMC_TBL[i] = s->tmc_tbl[i];
        TMC_TOFF[i] = s->tmc_toff[i];
        TMC_HSTRT[i] = s->tmc_hstrt[i];
        TMC_HEND[i] = s->tmc_hend[i];
        TMC_INTERPOLATE[i] = s->tmc_interpolate[i];
        TMC_STEALTHCHOP_SPS[i] = s->tmc_stealthchop_sps[i];
        TMC_RUN_CURRENT_MA[i] = s->tmc_run_current_ma[i];
        TMC_HOLD_CURRENT_MA[i] = s->tmc_hold_current_ma[i];
        MM_PER_STEP[i] = TMC_ROTATION_DISTANCE[i] /
                         ((float)TMC_FULL_STEPS[i] * TMC_GEAR_RATIO[i] * (float)TMC_MICROSTEPS[i]);
    }
}

static void settings_load_servo_cutter(const settings_t *s) {
    SERVO_OPEN_US = s->servo_open_us;
    SERVO_CLOSE_US = s->servo_close_us;
    SERVO_BLOCK_US = s->servo_block_us;
    SERVO_SETTLE_MS = s->servo_settle_ms;
    CUT_FEED_SPS = s->cut_feed_sps;
    CUT_FEED_MM = s->cut_feed_mm;
    CUT_LENGTH_MM = s->cut_length_mm;
    CUT_AMOUNT = s->cut_amount;

    RUNOUT_COOLDOWN_MS = s->runout_cooldown_ms;
}

static void settings_load_sync_reload(const settings_t *s) {
    BUF_SENSOR_TYPE = s->buf_sensor_type;
    BUF_HOME_STATE = clamp_i(s->buf_home_state, 0, 2);
    BUF_PSF_MAX_COMP = s->buf_psf_max_comp;
    BUF_PSF_MAX_TENS = s->buf_psf_max_tens;
    BUF_PSF_NEUTRAL = s->buf_psf_neutral;
    BUF_GOAL = s->buf_psf_goal;
    SYNC_KP_SPS = s->sync_kp_sps;
    SYNC_RESERVE_PCT = clamp_i(s->sync_reserve_pct, 0, SYNC_RESERVE_MAX_PCT);
    RELAY_CATCHUP_FRAC = clamp_f(s->relay_catchup_frac, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    RELAY_NEUTRAL_FRAC = clamp_f(s->relay_neutral_frac, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    SYNC_COMPRESSION_DRAIN_FRAC =
        clamp_f(CONF_SYNC_COMPRESSION_DRAIN_FRAC, 0.0f, COMPRESSION_DRAIN_MAX_FRAC);
    SYNC_COMPRESSION_DRAIN_BUDGET_MM =
        clamp_f(CONF_SYNC_COMPRESSION_DRAIN_BUDGET_MM, 0.0f, COMPRESSION_DRAIN_BUDGET_MAX_MM);
    SYNC_EST_ATTACK_ALPHA = clamp_f(CONF_SYNC_EST_ATTACK_ALPHA, SYNC_EST_ATTACK_MIN_ALPHA, 1.0f);
    SYNC_TENSION_FAST_MM_S = clamp_f(CONF_SYNC_TENSION_FAST_MM_S, 1.0f, SYNC_TENSION_FAST_MAX_MM_S);
    SYNC_TENSION_PROBE_MAX_SPS =
        clamp_i(CONF_SYNC_TENSION_PROBE_MAX_SPS, 0, mm_per_min_to_sps(TENSION_PROBE_MAX_MM_MIN));
    SYNC_TENSION_PROBE_UP_SPS_PER_S = clamp_i(CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S, 0,
                                              mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    SYNC_TENSION_PROBE_DOWN_SPS_PER_S = clamp_i(CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S, 0,
                                                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S =
        clamp_i(CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S, 0,
                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));

    SYNC_COMPRESSION_BIAS_FRAC =
        clamp_f(s->sync_compression_bias_frac, 0.0f, COMPRESSION_BIAS_MAX_FRAC);
    flow_schedule_reset_runtime();

    RELOAD_MODE = s->reload_mode ? 1 : 0;
    JOIN_SPS = s->join_sps;
    PRESS_SPS = s->press_sps;
    COMPRESSION_SPS = s->compression_sps;
}

void settings_load(void) {
    // Read settings straight from memory-mapped flash (XIP). Three guards must all
    // pass or we fall back to compiled defaults: magic (is this our sector at all),
    // version (a firmware change that altered settings_t invalidates old layout -
    // bump SETTINGS_VERSION to force this path), and CRC (intact, fully-written).
    const settings_t *s = (const settings_t *)(XIP_BASE + SETTINGS_FLASH_OFFSET);

    if (s->magic != SETTINGS_MAGIC || s->version != SETTINGS_VERSION) {
        settings_defaults();
        tmc_apply_all();
        return;
    }

    uint32_t crc = crc32_buf((const uint8_t *)s, offsetof(settings_t, crc32));
    if (crc != s->crc32) {
        settings_defaults();
        tmc_apply_all();
        return;
    }

    settings_load_motion(s);
    settings_load_tmc(s);
    settings_load_servo_cutter(s);
    settings_load_sync_reload(s);

    motion_limit_runtime_rates(false);

    tmc_apply_all();
}
