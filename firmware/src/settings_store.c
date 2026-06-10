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
#define SETTINGS_VERSION 60u

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
        g_follow_timeout_ms[i] = follow_timeout_ms[i];
        g_tmc_run_current_ma[i] = run_current_ma[i];
        g_tmc_hold_current_ma[i] = hold_current_ma[i];
        g_tmc_microsteps[i] = microsteps[i];
        g_tmc_stealthchop_sps[i] = stealthchop_sps[i];
    }

    g_mm_per_step[0] = CONF_L1_MM_PER_STEP;
    g_mm_per_step[1] = CONF_L2_MM_PER_STEP;

    g_tmc_rotation_distance[0] = CONF_L1_ROTATION_DISTANCE;
    g_tmc_rotation_distance[1] = CONF_L2_ROTATION_DISTANCE;
    g_tmc_gear_ratio[0] = CONF_L1_GEAR_RATIO;
    g_tmc_gear_ratio[1] = CONF_L2_GEAR_RATIO;
    g_tmc_full_steps[0] = CONF_L1_FULL_STEPS;
    g_tmc_full_steps[1] = CONF_L2_FULL_STEPS;
    g_tmc_microsteps[0] = CONF_L1_MICROSTEPS;
    g_tmc_microsteps[1] = CONF_L2_MICROSTEPS;
    g_tmc_tbl[0] = CONF_L1_TBL;
    g_tmc_tbl[1] = CONF_L2_TBL;
    g_tmc_toff[0] = CONF_L1_TOFF;
    g_tmc_toff[1] = CONF_L2_TOFF;
    g_tmc_hstrt[0] = CONF_L1_HSTRT;
    g_tmc_hstrt[1] = CONF_L2_HSTRT;
    g_tmc_hend[0] = CONF_L1_HEND;
    g_tmc_hend[1] = CONF_L2_HEND;
    g_tmc_interpolate[0] = CONF_L1_INTPOL;
    g_tmc_interpolate[1] = CONF_L2_INTPOL;
    g_tmc_stealthchop_sps[0] = CONF_L1_STEALTHCHOP_THRESHOLD;
    g_tmc_stealthchop_sps[1] = CONF_L2_STEALTHCHOP_THRESHOLD;
    g_tmc_run_current_ma[0] = CONF_L1_RUN_CURRENT_MA;
    g_tmc_run_current_ma[1] = CONF_L2_RUN_CURRENT_MA;
    g_tmc_hold_current_ma[0] = CONF_L1_HOLD_CURRENT_MA;
    g_tmc_hold_current_ma[1] = CONF_L2_HOLD_CURRENT_MA;
}

static void settings_defaults_sync(void) {
    g_buf_sensor_type = CONF_BUF_SENSOR_TYPE;
    g_buf_psf_max_comp = CONF_BUF_PSF_MAX_COMP;
    g_buf_psf_max_tens = CONF_BUF_PSF_MAX_TENS;
    g_buf_psf_neutral = CONF_BUF_PSF_NEUTRAL;
    g_buf_goal = CONF_BUF_GOAL;
    g_sync_kp_sps = CONF_SYNC_KP_SPS;
    g_sync_reserve_pct = clamp_i(CONF_SYNC_RESERVE_PCT, 0, SYNC_RESERVE_MAX_PCT);
    g_relay_catchup_frac = clamp_f(CONF_RELAY_CATCHUP_FRAC, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    g_relay_neutral_frac = clamp_f(CONF_RELAY_NEUTRAL_FRAC, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    g_sync_compression_drain_frac =
        clamp_f(CONF_SYNC_COMPRESSION_DRAIN_FRAC, 0.0f, COMPRESSION_DRAIN_MAX_FRAC);
    g_sync_compression_drain_budget_mm =
        clamp_f(CONF_SYNC_COMPRESSION_DRAIN_BUDGET_MM, 0.0f, COMPRESSION_DRAIN_BUDGET_MAX_MM);
    g_sync_est_attack_alpha = clamp_f(CONF_SYNC_EST_ATTACK_ALPHA, SYNC_EST_ATTACK_MIN_ALPHA, 1.0f);
    g_sync_tension_fast_mm_s = clamp_f(CONF_SYNC_TENSION_FAST_MM_S, 1.0f, SYNC_TENSION_FAST_MAX_MM_S);
    g_sync_tension_probe_max_sps =
        clamp_i(CONF_SYNC_TENSION_PROBE_MAX_SPS, 0, mm_per_min_to_sps(TENSION_PROBE_MAX_MM_MIN));
    g_sync_tension_probe_up_sps_per_s = clamp_i(CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S, 0,
                                              mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    g_sync_tension_probe_down_sps_per_s = clamp_i(CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S, 0,
                                                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    g_sync_tension_probe_neutral_sps_per_s =
        clamp_i(CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S, 0,
                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));

    g_sync_compression_bias_frac =
        clamp_f(CONF_SYNC_COMPRESSION_BIAS_FRAC, 0.0f, COMPRESSION_BIAS_MAX_FRAC);
    flow_schedule_reset_runtime();

    g_buf_stab_sps = clamp_i(CONF_BUF_STAB_SPS, BUF_STAB_MIN_SPS, BUF_STAB_MAX_SPS);
    g_join_sps = CONF_JOIN_SPS;
    g_press_sps = CONF_PRESS_SPS;
    g_compression_sps = CONF_COMPRESSION_SPS;
}

static void settings_defaults_motion(void) {
    g_feed_sps = CONF_FEED_SPS;
    g_rev_sps = CONF_REV_SPS;
    g_auto_sps = CONF_AUTO_SPS;

    g_global_max_sps = clamp_i(CONF_GLOBAL_MAX_SPS, mm_per_min_to_sps(GLOBAL_MAX_MIN_MM_MIN),
                             mm_per_min_to_sps(GLOBAL_MAX_MAX_MM_MIN));
    g_sync_max_sps = sync_clamp_max_sps(CONF_SYNC_MAX_SPS);
    g_sync_min_sps = CONF_SYNC_MIN_SPS;
    g_sync_ramp_up_sps = CONF_SYNC_RAMP_UP_SPS;
    g_sync_ramp_dn_sps = CONF_SYNC_RAMP_DN_SPS;
    g_pre_ramp_sps = CONF_PRE_RAMP_SPS;
    g_sync_auto_stop_ms = CONF_SYNC_AUTO_STOP_MS;
    g_autoload_max_mm = CONF_AUTOLOAD_MAX_MM;
    g_load_max_mm = CONF_LOAD_MAX_MM;
    g_unload_max_mm = CONF_UNLOAD_MAX_MM;
    g_unload_tension_block_ms = CONF_UNLOAD_TENSION_BLOCK_MS;
    g_reload_join_delay_ms = CONF_RELOAD_JOIN_DELAY_MS;
    g_reload_mode = CONF_RELOAD_MODE;
    g_auto_mode = 1;
    g_auto_preload = true;
    g_dist_in_out = CONF_DIST_IN_OUT;
    g_dist_out_y = CONF_DIST_OUT_Y;
    g_dist_y_buf = CONF_DIST_Y_BUF;
    g_buf_body_len = CONF_BUF_BODY_LEN;
    g_buf_max_travel_mm = clamp_i(CONF_BUF_MAX_TRAVEL_MM, BUF_TRAVEL_MIN_MM, BUF_TRAVEL_MAX_MM);
    g_buf_switch_span_half_mm =
        buf_switch_span_half_from_full(CONF_BUF_SWITCH_SPAN_MM, g_buf_max_travel_mm);
    g_zone_bias_base_sps = CONF_ZONE_BIAS_BASE_SPS;
    g_zone_bias_ramp_sps_s = CONF_ZONE_BIAS_RAMP_SPS_S;
    g_zone_bias_max_sps = CONF_ZONE_BIAS_MAX_SPS;
    g_baseline_target_sps = CONF_BASELINE_SPS;
    g_baseline_sps = CONF_BASELINE_SPS;
    g_auto_preload = true;
    g_autoload_retract_mm = CONF_AUTOLOAD_RETRACT_MM;
    g_enable_cutter = CONF_ENABLE_CUTTER;
    g_unload_cut = CONF_UNLOAD_CUT;
    g_ramp_step_sps = CONF_RAMP_STEP_SPS;
}

static void settings_defaults_servo_cutter(void) {
    g_servo_open_us = CONF_SERVO_OPEN_US;
    g_servo_close_us = CONF_SERVO_CLOSE_US;
    g_servo_block_us = CONF_SERVO_BLOCK_US;
    g_servo_settle_ms = CONF_SERVO_SETTLE_MS;
    g_cut_feed_sps = CONF_CUT_FEED_SPS;
    g_cut_feed_mm = CONF_CUT_FEED_MM;
    g_cut_length_mm = CONF_CUT_LENGTH_MM;
    g_cut_amount = CONF_CUT_AMOUNT;

    g_runout_cooldown_ms = CONF_RUNOUT_COOLDOWN_MS;
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

    s.feed_sps = g_feed_sps;
    s.rev_sps = g_rev_sps;
    s.auto_sps = g_auto_sps;

    s.sync_max_sps = g_sync_max_sps;
    s.global_max_sps = g_global_max_sps;
    s.sync_min_sps = g_sync_min_sps;
    s.sync_auto_stop_ms = g_sync_auto_stop_ms;
    s.autoload_max_mm = g_autoload_max_mm;
    s.load_max_mm = g_load_max_mm;
    s.unload_max_mm = g_unload_max_mm;
    s.unload_tension_block_ms = g_unload_tension_block_ms;
    s.reload_join_delay_ms = g_reload_join_delay_ms;
    s.auto_mode = g_auto_mode;
    s.auto_preload = g_auto_preload ? 1 : 0;
    s.buf_switch_span_mm = g_buf_switch_span_half_mm * FULL_SPAN_MULT_F;
    s.dist_in_out = g_dist_in_out;
    s.dist_out_y = g_dist_out_y;
    s.dist_y_buf = g_dist_y_buf;
    s.buf_body_len = g_buf_body_len;
    s.buf_max_travel_mm = g_buf_max_travel_mm;
    s.baseline_sps = g_baseline_target_sps;
    s.auto_preload = g_auto_preload;
    s.autoload_retract_mm = g_autoload_retract_mm;
    s.enable_cutter = g_enable_cutter;
    s.unload_cut = g_unload_cut;

    s.servo_open_us = g_servo_open_us;
    s.servo_close_us = g_servo_close_us;
    s.servo_block_us = g_servo_block_us;
    s.servo_settle_ms = g_servo_settle_ms;
    s.cut_feed_sps = g_cut_feed_sps;
    s.cut_feed_mm = g_cut_feed_mm;
    s.cut_length_mm = g_cut_length_mm;
    s.cut_amount = g_cut_amount;

    s.runout_cooldown_ms = g_runout_cooldown_ms;

    s.buf_sensor_type = g_buf_sensor_type;
    s.buf_psf_max_comp = g_buf_psf_max_comp;
    s.buf_psf_max_tens = g_buf_psf_max_tens;
    s.buf_psf_neutral = g_buf_psf_neutral;
    s.buf_psf_goal = g_buf_goal;
    s.sync_kp_sps = g_sync_kp_sps;
    s.sync_reserve_pct = g_sync_reserve_pct;
    s.join_sps = g_join_sps;
    s.press_sps = g_press_sps;
    s.compression_sps = g_compression_sps;

    s.reload_mode = (bool)g_reload_mode;
    for (int i = 0; i < NUM_LANES; i++) {
        s.follow_timeout_ms[i] = g_follow_timeout_ms[i];
    }

    s.relay_catchup_frac = g_relay_catchup_frac;
    s.relay_neutral_frac = g_relay_neutral_frac;

    s.sync_compression_bias_frac = g_sync_compression_bias_frac;

    for (int i = 0; i < NUM_LANES; i++) {
        s.tmc_rotation_distance[i] = g_tmc_rotation_distance[i];
        s.tmc_gear_ratio[i] = g_tmc_gear_ratio[i];
        s.tmc_full_steps[i] = g_tmc_full_steps[i];
        s.tmc_microsteps[i] = g_tmc_microsteps[i];
        s.tmc_tbl[i] = g_tmc_tbl[i];
        s.tmc_toff[i] = g_tmc_toff[i];
        s.tmc_hstrt[i] = g_tmc_hstrt[i];
        s.tmc_hend[i] = g_tmc_hend[i];
        s.tmc_interpolate[i] = g_tmc_interpolate[i];
        s.tmc_stealthchop_sps[i] = g_tmc_stealthchop_sps[i];
        s.tmc_run_current_ma[i] = g_tmc_run_current_ma[i];
        s.tmc_hold_current_ma[i] = g_tmc_hold_current_ma[i];
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

    g_mm_per_step[idx] =
        g_tmc_rotation_distance[idx] /
        ((float)g_tmc_full_steps[idx] * g_tmc_gear_ratio[idx] * (float)g_tmc_microsteps[idx]);

    tmc_setup_chopconf(tmc, g_tmc_microsteps[idx], g_tmc_toff[idx], g_tmc_tbl[idx], g_tmc_hstrt[idx],
                       g_tmc_hend[idx], g_tmc_interpolate[idx]);
    tmc_set_stealthchop_sps(tmc, g_tmc_stealthchop_sps[idx], g_tmc_microsteps[idx]);
    tmc_set_run_current_ma(tmc, g_tmc_run_current_ma[idx], g_tmc_hold_current_ma[idx]);

    // Synchronize shadow state for protocol reporting
    g_shadow_vsense[idx] = (g_tmc_run_current_ma[idx] <= TMC_VSENSE_THRESHOLD_MA);
    g_shadow_ihold_irun[idx] = build_ihold_irun_reg(g_tmc_run_current_ma[idx],
                                                    g_tmc_hold_current_ma[idx], g_shadow_vsense[idx]);
    g_shadow_ihold_irun_valid[idx] = true;
}

static void tmc_apply_all(void) {
    tmc_set_pwmconf(&g_tmc_l1);
    tmc_set_pwmconf(&g_tmc_l2);
    tmc_set_stealthchop_sps(&g_tmc_l1, g_tmc_stealthchop_sps[0], g_tmc_microsteps[0]);
    tmc_set_stealthchop_sps(&g_tmc_l2, g_tmc_stealthchop_sps[1], g_tmc_microsteps[1]);
    tmc_setup_chopconf(&g_tmc_l1, g_tmc_microsteps[0], g_tmc_toff[0], g_tmc_tbl[0], g_tmc_hstrt[0],
                       g_tmc_hend[0], g_tmc_interpolate[0]);
    tmc_setup_chopconf(&g_tmc_l2, g_tmc_microsteps[1], g_tmc_toff[1], g_tmc_tbl[1], g_tmc_hstrt[1],
                       g_tmc_hend[1], g_tmc_interpolate[1]);
    tmc_set_run_current_ma(&g_tmc_l1, g_tmc_run_current_ma[0], g_tmc_hold_current_ma[0]);
    tmc_set_run_current_ma(&g_tmc_l2, g_tmc_run_current_ma[1], g_tmc_hold_current_ma[1]);

    g_shadow_vsense[0] = (g_tmc_run_current_ma[0] <= TMC_VSENSE_THRESHOLD_MA);
    g_shadow_vsense[1] = (g_tmc_run_current_ma[1] <= TMC_VSENSE_THRESHOLD_MA);
    g_shadow_ihold_irun[0] =
        build_ihold_irun_reg(g_tmc_run_current_ma[0], g_tmc_hold_current_ma[0], g_shadow_vsense[0]);
    g_shadow_ihold_irun[1] =
        build_ihold_irun_reg(g_tmc_run_current_ma[1], g_tmc_hold_current_ma[1], g_shadow_vsense[1]);
    g_shadow_ihold_irun_valid[0] = true;
    g_shadow_ihold_irun_valid[1] = true;
}

static void settings_load_motion(const settings_t *s) {
    g_feed_sps = s->feed_sps;
    g_rev_sps = s->rev_sps;
    g_auto_sps = s->auto_sps;

    g_global_max_sps = clamp_i(s->global_max_sps, mm_per_min_to_sps(GLOBAL_MAX_MIN_MM_MIN),
                             mm_per_min_to_sps(GLOBAL_MAX_MAX_MM_MIN));
    g_sync_max_sps = sync_clamp_max_sps(s->sync_max_sps);
    g_sync_min_sps = s->sync_min_sps;
    g_sync_auto_stop_ms = s->sync_auto_stop_ms;
    g_autoload_max_mm = s->autoload_max_mm;
    g_load_max_mm = s->load_max_mm;
    g_unload_max_mm = s->unload_max_mm;
    g_unload_tension_block_ms = s->unload_tension_block_ms;
    g_reload_join_delay_ms = s->reload_join_delay_ms;
    g_auto_mode = s->auto_mode;
    g_auto_preload = (s->auto_preload != 0);
    g_buf_max_travel_mm = clamp_i(s->buf_max_travel_mm, BUF_TRAVEL_MIN_MM, BUF_TRAVEL_MAX_MM);
    g_buf_switch_span_half_mm =
        buf_switch_span_half_from_full(s->buf_switch_span_mm, g_buf_max_travel_mm);
    g_dist_in_out = s->dist_in_out;
    g_dist_out_y = s->dist_out_y;
    g_dist_y_buf = s->dist_y_buf;
    g_buf_body_len = s->buf_body_len;
    g_baseline_target_sps = motion_clamp_rate_sps(s->baseline_sps);
    g_baseline_sps = g_baseline_target_sps;
    g_auto_preload = s->auto_preload;
    g_autoload_retract_mm = s->autoload_retract_mm;
    g_enable_cutter = s->enable_cutter;
    g_unload_cut = s->unload_cut;
}

static int snap_microsteps(int x) {
    if (x <= 1) return 1;
    if (x >= 256) return 256;
    int lower = 1;
    while (lower * 2 <= x) {
        lower *= 2;
    }
    int upper = lower * 2;
    if ((x - lower) < (upper - x)) {
        return lower;
    } else {
        return upper;
    }
}

static void settings_load_tmc(const settings_t *s) {
    for (int i = 0; i < NUM_LANES; i++) {
        g_follow_timeout_ms[i] = s->follow_timeout_ms[i];
        g_tmc_rotation_distance[i] = clamp_f(s->tmc_rotation_distance[i], TMC_ROTATION_MIN_MM, TMC_ROTATION_MAX_MM);
        g_tmc_gear_ratio[i] = clamp_f(s->tmc_gear_ratio[i], TMC_GEAR_RATIO_MIN, TMC_GEAR_RATIO_MAX);
        g_tmc_full_steps[i] = (s->tmc_full_steps[i] == 400) ? 400 : 200;
        g_tmc_microsteps[i] = snap_microsteps(s->tmc_microsteps[i]);
        g_tmc_tbl[i] = s->tmc_tbl[i];
        g_tmc_toff[i] = s->tmc_toff[i];
        g_tmc_hstrt[i] = s->tmc_hstrt[i];
        g_tmc_hend[i] = s->tmc_hend[i];
        g_tmc_interpolate[i] = s->tmc_interpolate[i];
        g_tmc_stealthchop_sps[i] = s->tmc_stealthchop_sps[i];
        g_tmc_run_current_ma[i] = s->tmc_run_current_ma[i];
        g_tmc_hold_current_ma[i] = s->tmc_hold_current_ma[i];
        g_mm_per_step[i] = g_tmc_rotation_distance[i] /
                         ((float)g_tmc_full_steps[i] * g_tmc_gear_ratio[i] * (float)g_tmc_microsteps[i]);
    }
}

static void settings_load_servo_cutter(const settings_t *s) {
    g_servo_open_us = s->servo_open_us;
    g_servo_close_us = s->servo_close_us;
    g_servo_block_us = s->servo_block_us;
    g_servo_settle_ms = s->servo_settle_ms;
    g_cut_feed_sps = s->cut_feed_sps;
    g_cut_feed_mm = s->cut_feed_mm;
    g_cut_length_mm = s->cut_length_mm;
    g_cut_amount = s->cut_amount;

    g_runout_cooldown_ms = s->runout_cooldown_ms;
}

static void settings_load_sync_reload(const settings_t *s) {
    g_buf_sensor_type = s->buf_sensor_type;
    g_buf_psf_max_comp = s->buf_psf_max_comp;
    g_buf_psf_max_tens = s->buf_psf_max_tens;
    g_buf_psf_neutral = s->buf_psf_neutral;
    g_buf_goal = s->buf_psf_goal;
    g_sync_kp_sps = s->sync_kp_sps;
    g_sync_reserve_pct = clamp_i(s->sync_reserve_pct, 0, SYNC_RESERVE_MAX_PCT);
    g_relay_catchup_frac = clamp_f(s->relay_catchup_frac, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    g_relay_neutral_frac = clamp_f(s->relay_neutral_frac, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    g_sync_compression_drain_frac =
        clamp_f(CONF_SYNC_COMPRESSION_DRAIN_FRAC, 0.0f, COMPRESSION_DRAIN_MAX_FRAC);
    g_sync_compression_drain_budget_mm =
        clamp_f(CONF_SYNC_COMPRESSION_DRAIN_BUDGET_MM, 0.0f, COMPRESSION_DRAIN_BUDGET_MAX_MM);
    g_sync_est_attack_alpha = clamp_f(CONF_SYNC_EST_ATTACK_ALPHA, SYNC_EST_ATTACK_MIN_ALPHA, 1.0f);
    g_sync_tension_fast_mm_s = clamp_f(CONF_SYNC_TENSION_FAST_MM_S, 1.0f, SYNC_TENSION_FAST_MAX_MM_S);
    g_sync_tension_probe_max_sps =
        clamp_i(CONF_SYNC_TENSION_PROBE_MAX_SPS, 0, mm_per_min_to_sps(TENSION_PROBE_MAX_MM_MIN));
    g_sync_tension_probe_up_sps_per_s = clamp_i(CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S, 0,
                                              mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    g_sync_tension_probe_down_sps_per_s = clamp_i(CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S, 0,
                                                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    g_sync_tension_probe_neutral_sps_per_s =
        clamp_i(CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S, 0,
                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));

    g_sync_compression_bias_frac =
        clamp_f(s->sync_compression_bias_frac, 0.0f, COMPRESSION_BIAS_MAX_FRAC);
    flow_schedule_reset_runtime();

    g_reload_mode = s->reload_mode ? 1 : 0;
    g_join_sps = s->join_sps;
    g_press_sps = s->press_sps;
    g_compression_sps = s->compression_sps;
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
