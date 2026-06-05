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

_Static_assert(sizeof(settings_t) <= 512,
               "settings_t exceeds two flash pages - expand buffer in settings_save()");

static uint32_t crc32_buf(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static float buf_switch_span_half_from_full(float span_mm, int max_travel_mm) {
    float max_span_mm = (float)max_travel_mm;
    if (max_span_mm < 2.0f)
        max_span_mm = 2.0f;
    return clamp_f(span_mm, 2.0f, max_span_mm) * 0.5f;
}

void settings_defaults(void) {
    FEED_SPS = CONF_FEED_SPS;
    REV_SPS = CONF_REV_SPS;
    AUTO_SPS = CONF_AUTO_SPS;

    GLOBAL_MAX_SPS =
        clamp_i(CONF_GLOBAL_MAX_SPS, mm_per_min_to_sps(1000.0f), mm_per_min_to_sps(12000.0f));
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
    BUF_MAX_TRAVEL_MM = clamp_i(CONF_BUF_MAX_TRAVEL_MM, 10, 1000);
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

    for (int i = 0; i < NUM_LANES; i++) {
        FOLLOW_TIMEOUT_MS[i] = (i == 0) ? CONF_L1_FOLLOW_TIMEOUT_MS : CONF_L2_FOLLOW_TIMEOUT_MS;
        TMC_RUN_CURRENT_MA[i] = (i == 0) ? CONF_L1_RUN_CURRENT_MA : CONF_L2_RUN_CURRENT_MA;
        TMC_HOLD_CURRENT_MA[i] = (i == 0) ? CONF_L1_HOLD_CURRENT_MA : CONF_L2_HOLD_CURRENT_MA;
        TMC_MICROSTEPS[i] = (i == 0) ? CONF_L1_MICROSTEPS : CONF_L2_MICROSTEPS;
        TMC_STEALTHCHOP_SPS[i] =
            (i == 0) ? CONF_L1_STEALTHCHOP_THRESHOLD : CONF_L2_STEALTHCHOP_THRESHOLD;
    }

    SERVO_OPEN_US = CONF_SERVO_OPEN_US;
    SERVO_CLOSE_US = CONF_SERVO_CLOSE_US;
    SERVO_BLOCK_US = CONF_SERVO_BLOCK_US;
    SERVO_SETTLE_MS = CONF_SERVO_SETTLE_MS;
    CUT_FEED_SPS = CONF_CUT_FEED_SPS;
    CUT_FEED_MM = CONF_CUT_FEED_MM;
    CUT_LENGTH_MM = CONF_CUT_LENGTH_MM;
    CUT_AMOUNT = CONF_CUT_AMOUNT;

    RUNOUT_COOLDOWN_MS = CONF_RUNOUT_COOLDOWN_MS;

    RAMP_STEP_SPS = CONF_RAMP_STEP_SPS;

    BUF_SENSOR_TYPE = CONF_BUF_SENSOR_TYPE;
    BUF_HOME_STATE = CONF_BUF_HOME_STATE;
    BUF_PSF_MAX_COMP = CONF_BUF_PSF_MAX_COMP;
    BUF_PSF_MAX_TENS = CONF_BUF_PSF_MAX_TENS;
    BUF_PSF_NEUTRAL = CONF_BUF_PSF_NEUTRAL;
    BUF_GOAL = CONF_BUF_GOAL;
    SYNC_KP_SPS = CONF_SYNC_KP_SPS;
    SYNC_RESERVE_PCT = clamp_i(CONF_SYNC_RESERVE_PCT, 0, 150);
    RELAY_CATCHUP_FRAC = clamp_f(CONF_RELAY_CATCHUP_FRAC, 0.5f, 3.0f);
    RELAY_NEUTRAL_FRAC = clamp_f(CONF_RELAY_NEUTRAL_FRAC, 0.5f, 3.0f);
    SYNC_COMPRESSION_DRAIN_FRAC = clamp_f(CONF_SYNC_COMPRESSION_DRAIN_FRAC, 0.0f, 0.9f);
    SYNC_COMPRESSION_DRAIN_BUDGET_MM = clamp_f(CONF_SYNC_COMPRESSION_DRAIN_BUDGET_MM, 0.0f, 25.0f);
    SYNC_EST_ATTACK_ALPHA = clamp_f(CONF_SYNC_EST_ATTACK_ALPHA, 0.65f, 1.0f);
    SYNC_TENSION_FAST_MM_S = clamp_f(CONF_SYNC_TENSION_FAST_MM_S, 1.0f, 200.0f);
    SYNC_TENSION_PROBE_MAX_SPS =
        clamp_i(CONF_SYNC_TENSION_PROBE_MAX_SPS, 0, mm_per_min_to_sps(6000.0f));
    SYNC_TENSION_PROBE_UP_SPS_PER_S =
        clamp_i(CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S, 0, mm_per_min_to_sps(12000.0f));
    SYNC_TENSION_PROBE_DOWN_SPS_PER_S =
        clamp_i(CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S, 0, mm_per_min_to_sps(12000.0f));
    SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S =
        clamp_i(CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S, 0, mm_per_min_to_sps(12000.0f));

    SYNC_COMPRESSION_BIAS_FRAC = clamp_f(CONF_SYNC_COMPRESSION_BIAS_FRAC, 0.0f, 0.7f);
    flow_schedule_reset_runtime();

    BUF_STAB_SPS = clamp_i(CONF_BUF_STAB_SPS, 10, 10000);
    JOIN_SPS = CONF_JOIN_SPS;
    PRESS_SPS = CONF_PRESS_SPS;
    COMPRESSION_SPS = CONF_COMPRESSION_SPS;

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
    s.buf_switch_span_mm = BUF_SWITCH_SPAN_HALF_MM * 2.0f;
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

    s.crc32 = crc32_buf((const uint8_t *)&s, offsetof(settings_t, crc32));

    uint8_t buffer[512] = {0};
    memcpy(buffer, &s, sizeof(s));

    stop_all();

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_FLASH_OFFSET, buffer, 512);
    restore_interrupts(ints);
}

void sync_tmc_settings(int lane) {
    int idx = lane_to_idx(lane);
    tmc_t *tmc = (lane == 1) ? &g_tmc_l1 : &g_tmc_l2;

    MM_PER_STEP[idx] = TMC_ROTATION_DISTANCE[idx] /
                       (float)(TMC_FULL_STEPS[idx] * TMC_GEAR_RATIO[idx] * TMC_MICROSTEPS[idx]);

    tmc_setup_chopconf(tmc, TMC_MICROSTEPS[idx], TMC_TOFF[idx], TMC_TBL[idx], TMC_HSTRT[idx],
                       TMC_HEND[idx], TMC_INTERPOLATE[idx]);
    tmc_set_stealthchop_sps(tmc, TMC_STEALTHCHOP_SPS[idx], TMC_MICROSTEPS[idx]);
    tmc_set_run_current_ma(tmc, TMC_RUN_CURRENT_MA[idx], TMC_HOLD_CURRENT_MA[idx]);

    // Synchronize shadow state for protocol reporting
    g_shadow_vsense[idx] = (TMC_RUN_CURRENT_MA[idx] <= 980);
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

    g_shadow_vsense[0] = (TMC_RUN_CURRENT_MA[0] <= 980);
    g_shadow_vsense[1] = (TMC_RUN_CURRENT_MA[1] <= 980);
    g_shadow_ihold_irun[0] =
        build_ihold_irun_reg(TMC_RUN_CURRENT_MA[0], TMC_HOLD_CURRENT_MA[0], g_shadow_vsense[0]);
    g_shadow_ihold_irun[1] =
        build_ihold_irun_reg(TMC_RUN_CURRENT_MA[1], TMC_HOLD_CURRENT_MA[1], g_shadow_vsense[1]);
    g_shadow_ihold_irun_valid[0] = true;
    g_shadow_ihold_irun_valid[1] = true;
}

void settings_load(void) {
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

    FEED_SPS = s->feed_sps;
    REV_SPS = s->rev_sps;
    AUTO_SPS = s->auto_sps;

    GLOBAL_MAX_SPS =
        clamp_i(s->global_max_sps, mm_per_min_to_sps(1000.0f), mm_per_min_to_sps(12000.0f));
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
    BUF_MAX_TRAVEL_MM = clamp_i(s->buf_max_travel_mm, 10, 1000);
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
                         (float)(TMC_FULL_STEPS[i] * TMC_GEAR_RATIO[i] * TMC_MICROSTEPS[i]);
    }

    SERVO_OPEN_US = s->servo_open_us;
    SERVO_CLOSE_US = s->servo_close_us;
    SERVO_BLOCK_US = s->servo_block_us;
    SERVO_SETTLE_MS = s->servo_settle_ms;
    CUT_FEED_SPS = s->cut_feed_sps;
    CUT_FEED_MM = s->cut_feed_mm;
    CUT_LENGTH_MM = s->cut_length_mm;
    CUT_AMOUNT = s->cut_amount;

    RUNOUT_COOLDOWN_MS = s->runout_cooldown_ms;

    BUF_SENSOR_TYPE = s->buf_sensor_type;
    BUF_HOME_STATE = clamp_i(s->buf_home_state, 0, 2);
    BUF_PSF_MAX_COMP = s->buf_psf_max_comp;
    BUF_PSF_MAX_TENS = s->buf_psf_max_tens;
    BUF_PSF_NEUTRAL = s->buf_psf_neutral;
    BUF_GOAL = s->buf_psf_goal;
    SYNC_KP_SPS = s->sync_kp_sps;
    SYNC_RESERVE_PCT = clamp_i(s->sync_reserve_pct, 0, 150);
    RELAY_CATCHUP_FRAC = clamp_f(s->relay_catchup_frac, 0.5f, 3.0f);
    RELAY_NEUTRAL_FRAC = clamp_f(s->relay_neutral_frac, 0.5f, 3.0f);
    SYNC_COMPRESSION_DRAIN_FRAC = clamp_f(CONF_SYNC_COMPRESSION_DRAIN_FRAC, 0.0f, 0.9f);
    SYNC_COMPRESSION_DRAIN_BUDGET_MM = clamp_f(CONF_SYNC_COMPRESSION_DRAIN_BUDGET_MM, 0.0f, 25.0f);
    SYNC_EST_ATTACK_ALPHA = clamp_f(CONF_SYNC_EST_ATTACK_ALPHA, 0.65f, 1.0f);
    SYNC_TENSION_FAST_MM_S = clamp_f(CONF_SYNC_TENSION_FAST_MM_S, 1.0f, 200.0f);
    SYNC_TENSION_PROBE_MAX_SPS =
        clamp_i(CONF_SYNC_TENSION_PROBE_MAX_SPS, 0, mm_per_min_to_sps(6000.0f));
    SYNC_TENSION_PROBE_UP_SPS_PER_S =
        clamp_i(CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S, 0, mm_per_min_to_sps(12000.0f));
    SYNC_TENSION_PROBE_DOWN_SPS_PER_S =
        clamp_i(CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S, 0, mm_per_min_to_sps(12000.0f));
    SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S =
        clamp_i(CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S, 0, mm_per_min_to_sps(12000.0f));

    SYNC_COMPRESSION_BIAS_FRAC = clamp_f(s->sync_compression_bias_frac, 0.0f, 0.7f);
    flow_schedule_reset_runtime();

    RELOAD_MODE = s->reload_mode ? 1 : 0;
    JOIN_SPS = s->join_sps;
    PRESS_SPS = s->press_sps;
    COMPRESSION_SPS = s->compression_sps;

    motion_limit_runtime_rates(false);

    tmc_apply_all();
}
