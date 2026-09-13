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
#include "protocol.h"
#include "settings_store.h"
#include "sync.h"

#define SETTINGS_FLASH_OFFSET_A (PICO_FLASH_SIZE_BYTES - (2 * FLASH_SECTOR_SIZE))
#define SETTINGS_FLASH_OFFSET_B (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

int g_active_sector = 1;
uint32_t g_seq = 0;

// RP2040's onboard NOR flash is typically rated ~100k erase cycles per
// sector. Visibility only (ARCHITECTURE_BRIEF.md "no wear leveling, no
// warning" gap) -- not a retunable value, so no CONF_*/config.ini entry.
#define FLASH_WEAR_WARN_THRESHOLD 80000u

enum {
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
/* Shared by settings_defaults_sync() and settings_apply_clamps() below; the
   SET:/GET: handler in protocol.c mirrors these bounds with its own
   same-named #define, matching the existing RELAY_FRAC_MIN/MAX split
   between this file and protocol.c. Below 1.0 would mean "relief less than
   demand", which is not relief; the outer min(max_sps, ...) in
   sync_type_p_relief_bound_sps() is an independent ceiling regardless. */
static const float SYNC_RELIEF_MULT_MIN = 1.0f;
static const float SYNC_RELIEF_MULT_MAX = 3.0f;
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
    uint32_t seq;

    int feed_sps, rev_sps, auto_sps;
    int sync_max_sps, global_max_sps, sync_min_sps;
    int sync_auto_stop_ms;
    int load_max_mm;
    int tc_ts_retries;
    float tc_ts_retry_retract_mm;
    float tc_ts_park_mm;
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

    uint32_t flash_erase_count;

    uint32_t crc32;
} settings_v63_t;

_Static_assert(sizeof(settings_v63_t) <= SETTINGS_FLASH_BUFFER_BYTES,
               "settings_v63_t exceeds flash buffer");

typedef enum {
    SECTOR_INVALID = 0,
    SECTOR_V63 = 63,
    SECTOR_V64 = 64,
} sector_version_t;

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
    g_sync_tension_fast_mm_s =
        clamp_f(CONF_SYNC_TENSION_FAST_MM_S, 1.0f, SYNC_TENSION_FAST_MAX_MM_S);
    g_sync_tension_probe_max_sps =
        clamp_i(CONF_SYNC_TENSION_PROBE_MAX_SPS, 0, mm_per_min_to_sps(TENSION_PROBE_MAX_MM_MIN));
    g_sync_tension_probe_up_sps_per_s = clamp_i(CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S, 0,
                                                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    g_sync_tension_probe_down_sps_per_s = clamp_i(CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S, 0,
                                                  mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    g_sync_tension_probe_neutral_sps_per_s =
        clamp_i(CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S, 0,
                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    g_sync_psf_relief_mult =
        clamp_f(CONF_SYNC_PSF_RELIEF_MULT, SYNC_RELIEF_MULT_MIN, SYNC_RELIEF_MULT_MAX);

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
    g_tc_ts_retries = CONF_TC_TS_RETRIES;
    g_tc_ts_retry_retract_mm = CONF_TC_TS_RETRY_RETRACT_MM;
    g_tc_ts_park_mm = CONF_TC_TS_PARK_MM;
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
    g_flash_erase_count = 0;
    g_seq = 0;
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

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t offset;
} tlv_writer_t;

static bool tlv_emit(tlv_writer_t *w, uint8_t tag, uint8_t len, const void *val) {
    if (!w || w->offset + 2 + len > w->cap) {
        return false;
    }
    w->buf[w->offset++] = tag;
    w->buf[w->offset++] = len;
    if (len > 0 && val) {
        memcpy(&w->buf[w->offset], val, len);
        w->offset += len;
    }
    return true;
}

static bool tlv_emit_i32(tlv_writer_t *w, uint8_t tag, int32_t val) {
    return tlv_emit(w, tag, sizeof(val), &val);
}

static bool tlv_emit_u32(tlv_writer_t *w, uint8_t tag, uint32_t val) {
    return tlv_emit(w, tag, sizeof(val), &val);
}

static bool tlv_emit_f32(tlv_writer_t *w, uint8_t tag, float val) {
    return tlv_emit(w, tag, sizeof(val), &val);
}

static bool tlv_emit_bool(tlv_writer_t *w, uint8_t tag, bool val) {
    uint8_t b = val ? 1 : 0;
    return tlv_emit(w, tag, sizeof(b), &b);
}

static sector_version_t settings_validate_sector(const uint8_t *sector_base, uint32_t *out_seq) {
    if (!sector_base)
        return SECTOR_INVALID;

    const settings_header_t *hdr = (const settings_header_t *)sector_base;
    if (hdr->magic != SETTINGS_MAGIC) {
        return SECTOR_INVALID;
    }

    if (hdr->version == SETTINGS_VERSION) { // 64
        if (hdr->payload_len + sizeof(settings_header_t) + sizeof(uint32_t) >
            SETTINGS_FLASH_BUFFER_BYTES) {
            return SECTOR_INVALID;
        }
        size_t total_payload = sizeof(settings_header_t) + hdr->payload_len;
        uint32_t stored_crc;
        memcpy(&stored_crc, sector_base + total_payload, sizeof(uint32_t));
        uint32_t computed_crc = crc32_buf(sector_base, total_payload);
        if (computed_crc != stored_crc) {
            return SECTOR_INVALID;
        }
        if (out_seq)
            *out_seq = hdr->seq;
        return SECTOR_V64;
    }

    if (hdr->version == SETTINGS_VERSION_V63) { // 63
        const settings_v63_t *s63 = (const settings_v63_t *)sector_base;
        uint32_t computed_crc = crc32_buf(sector_base, offsetof(settings_v63_t, crc32));
        if (computed_crc != s63->crc32) {
            return SECTOR_INVALID;
        }
        if (out_seq)
            *out_seq = s63->seq;
        return SECTOR_V63;
    }

    return SECTOR_INVALID;
}

void settings_save(void) {
    int target = 1 - g_active_sector;
    uint32_t target_offset = (target == 0) ? SETTINGS_FLASH_OFFSET_A : SETTINGS_FLASH_OFFSET_B;

    uint8_t buffer[SETTINGS_FLASH_BUFFER_BYTES] = {0};
    settings_header_t *hdr = (settings_header_t *)buffer;
    hdr->magic = SETTINGS_MAGIC;
    hdr->version = SETTINGS_VERSION;

    g_seq++;
    hdr->seq = g_seq;

    g_flash_erase_count++;
    if (g_flash_erase_count == FLASH_WEAR_WARN_THRESHOLD) {
        cmd_event("FLASH", "WEAR_WARNING");
    }

    tlv_writer_t w = {
        .buf = buffer + sizeof(settings_header_t),
        .cap = SETTINGS_FLASH_BUFFER_BYTES - sizeof(settings_header_t) - sizeof(uint32_t),
        .offset = 0,
    };

    tlv_emit_i32(&w, TAG_FEED_SPS, g_feed_sps);
    tlv_emit_i32(&w, TAG_REV_SPS, g_rev_sps);
    tlv_emit_i32(&w, TAG_AUTO_SPS, g_auto_sps);
    tlv_emit_i32(&w, TAG_SYNC_MAX_SPS, g_sync_max_sps);
    tlv_emit_i32(&w, TAG_GLOBAL_MAX_SPS, g_global_max_sps);
    tlv_emit_i32(&w, TAG_SYNC_MIN_SPS, g_sync_min_sps);
    tlv_emit_i32(&w, TAG_SYNC_AUTO_STOP_MS, g_sync_auto_stop_ms);
    tlv_emit_i32(&w, TAG_LOAD_MAX_MM, g_load_max_mm);
    tlv_emit_i32(&w, TAG_TC_TS_RETRIES, g_tc_ts_retries);
    tlv_emit_f32(&w, TAG_TC_TS_RETRY_RETRACT_MM, g_tc_ts_retry_retract_mm);
    tlv_emit_f32(&w, TAG_TC_TS_PARK_MM, g_tc_ts_park_mm);
    tlv_emit_i32(&w, TAG_UNLOAD_MAX_MM, g_unload_max_mm);
    tlv_emit_i32(&w, TAG_UNLOAD_TENSION_BLOCK_MS, g_unload_tension_block_ms);
    tlv_emit_i32(&w, TAG_RELOAD_JOIN_DELAY_MS, g_reload_join_delay_ms);
    tlv_emit_i32(&w, TAG_AUTOLOAD_MAX_MM, g_autoload_max_mm);
    tlv_emit_i32(&w, TAG_AUTO_MODE, g_auto_mode);
    tlv_emit_i32(&w, TAG_DIST_IN_OUT, g_dist_in_out);
    tlv_emit_i32(&w, TAG_DIST_OUT_Y, g_dist_out_y);
    tlv_emit_i32(&w, TAG_DIST_Y_BUF, g_dist_y_buf);
    tlv_emit_i32(&w, TAG_BUF_BODY_LEN, g_buf_body_len);
    tlv_emit_i32(&w, TAG_BUF_MAX_TRAVEL_MM, g_buf_max_travel_mm);
    float buf_switch_span_mm = g_buf_switch_span_half_mm * FULL_SPAN_MULT_F;
    tlv_emit_f32(&w, TAG_BUF_SWITCH_SPAN_MM, buf_switch_span_mm);
    tlv_emit_i32(&w, TAG_BASELINE_SPS, g_baseline_target_sps);
    tlv_emit_i32(&w, TAG_AUTOLOAD_RETRACT_MM, g_autoload_retract_mm);

    tlv_emit_i32(&w, TAG_SERVO_OPEN_US, g_servo_open_us);
    tlv_emit_i32(&w, TAG_SERVO_CLOSE_US, g_servo_close_us);
    tlv_emit_i32(&w, TAG_SERVO_BLOCK_US, g_servo_block_us);
    tlv_emit_i32(&w, TAG_SERVO_SETTLE_MS, g_servo_settle_ms);
    tlv_emit_i32(&w, TAG_CUT_FEED_SPS, g_cut_feed_sps);
    tlv_emit_i32(&w, TAG_CUT_FEED_MM, g_cut_feed_mm);
    tlv_emit_i32(&w, TAG_CUT_LENGTH_MM, g_cut_length_mm);
    tlv_emit_i32(&w, TAG_CUT_AMOUNT, g_cut_amount);

    tlv_emit_i32(&w, TAG_RUNOUT_COOLDOWN_MS, g_runout_cooldown_ms);

    tlv_emit_i32(&w, TAG_BUF_SENSOR_TYPE, g_buf_sensor_type);
    tlv_emit_f32(&w, TAG_BUF_PSF_MAX_COMP, g_buf_psf_max_comp);
    tlv_emit_f32(&w, TAG_BUF_PSF_MAX_TENS, g_buf_psf_max_tens);
    tlv_emit_f32(&w, TAG_BUF_PSF_NEUTRAL, g_buf_psf_neutral);
    tlv_emit_f32(&w, TAG_BUF_PSF_GOAL, g_buf_goal);
    tlv_emit_i32(&w, TAG_SYNC_KP_SPS, g_sync_kp_sps);
    tlv_emit_i32(&w, TAG_SYNC_RESERVE_PCT, g_sync_reserve_pct);

    tlv_emit_i32(&w, TAG_JOIN_SPS, g_join_sps);
    tlv_emit_i32(&w, TAG_PRESS_SPS, g_press_sps);
    tlv_emit_i32(&w, TAG_COMPRESSION_SPS, g_compression_sps);
    tlv_emit(&w, TAG_FOLLOW_TIMEOUT_MS, sizeof(g_follow_timeout_ms), g_follow_timeout_ms);

    tlv_emit_bool(&w, TAG_AUTO_PRELOAD, g_auto_preload);
    tlv_emit_bool(&w, TAG_ENABLE_CUTTER, g_enable_cutter);
    tlv_emit_bool(&w, TAG_UNLOAD_CUT, g_unload_cut);
    tlv_emit_i32(&w, TAG_RELOAD_MODE, g_reload_mode ? 1 : 0);

    tlv_emit(&w, TAG_TMC_ROTATION_DISTANCE, sizeof(g_tmc_rotation_distance),
             g_tmc_rotation_distance);
    tlv_emit(&w, TAG_TMC_GEAR_RATIO, sizeof(g_tmc_gear_ratio), g_tmc_gear_ratio);
    tlv_emit(&w, TAG_TMC_FULL_STEPS, sizeof(g_tmc_full_steps), g_tmc_full_steps);
    tlv_emit(&w, TAG_TMC_MICROSTEPS, sizeof(g_tmc_microsteps), g_tmc_microsteps);
    tlv_emit(&w, TAG_TMC_TBL, sizeof(g_tmc_tbl), g_tmc_tbl);
    tlv_emit(&w, TAG_TMC_TOFF, sizeof(g_tmc_toff), g_tmc_toff);
    tlv_emit(&w, TAG_TMC_HSTRT, sizeof(g_tmc_hstrt), g_tmc_hstrt);
    tlv_emit(&w, TAG_TMC_HEND, sizeof(g_tmc_hend), g_tmc_hend);
    tlv_emit(&w, TAG_TMC_INTERPOLATE, sizeof(g_tmc_interpolate), g_tmc_interpolate);
    tlv_emit(&w, TAG_TMC_STEALTHCHOP_SPS, sizeof(g_tmc_stealthchop_sps), g_tmc_stealthchop_sps);
    tlv_emit(&w, TAG_TMC_RUN_CURRENT_MA, sizeof(g_tmc_run_current_ma), g_tmc_run_current_ma);
    tlv_emit(&w, TAG_TMC_HOLD_CURRENT_MA, sizeof(g_tmc_hold_current_ma), g_tmc_hold_current_ma);

    tlv_emit_f32(&w, TAG_RELAY_CATCHUP_FRAC, g_relay_catchup_frac);
    tlv_emit_f32(&w, TAG_RELAY_NEUTRAL_FRAC, g_relay_neutral_frac);
    tlv_emit_f32(&w, TAG_SYNC_COMPRESSION_BIAS_FRAC, g_sync_compression_bias_frac);
    tlv_emit_f32(&w, TAG_SYNC_PSF_RELIEF_MULT, g_sync_psf_relief_mult);

    tlv_emit_u32(&w, TAG_FLASH_ERASE_COUNT, g_flash_erase_count);

    hdr->payload_len = (uint16_t)w.offset;
    hdr->reserved = 0;

    size_t total_payload = sizeof(settings_header_t) + hdr->payload_len;
    uint32_t crc = crc32_buf(buffer, total_payload);
    memcpy(buffer + total_payload, &crc, sizeof(crc));

    // Writing flash on the RP2040 stalls code execution from flash (XIP). Stop all
    // motion first (no steps will be generated during the write), then disable
    // interrupts so no ISR tries to run from flash mid-erase. Erase the sector,
    // then program the buffer. Caller must guarantee we are not mid-motion.
    stop_all();

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(target_offset, FLASH_SECTOR_SIZE);
    flash_range_program(target_offset, buffer, SETTINGS_FLASH_BUFFER_BYTES);
    restore_interrupts(ints);

    // Verify readback before flipping active sector pointer
    uint32_t readback_seq = 0;
    const uint8_t *target_base = (const uint8_t *)(XIP_BASE + target_offset);
    if (settings_validate_sector(target_base, &readback_seq) == SECTOR_V64 &&
        readback_seq == hdr->seq) {
        g_active_sector = target;
    }
}

void sync_tmc_settings(int lane) {
    int idx = lane_to_idx(lane);
    tmc_t *tmc = (lane == 1) ? &g_tmc_l1 : &g_tmc_l2;

    g_mm_per_step[idx] =
        g_tmc_rotation_distance[idx] /
        ((float)g_tmc_full_steps[idx] * g_tmc_gear_ratio[idx] * (float)g_tmc_microsteps[idx]);

    tmc_set_pwmconf(tmc);
    tmc_setup_chopconf(tmc, g_tmc_microsteps[idx], g_tmc_toff[idx], g_tmc_tbl[idx],
                       g_tmc_hstrt[idx], g_tmc_hend[idx], g_tmc_interpolate[idx]);
    tmc_set_stealthchop_sps(tmc, g_tmc_stealthchop_sps[idx], g_tmc_microsteps[idx]);
    tmc_set_run_current_ma(tmc, g_tmc_run_current_ma[idx], g_tmc_hold_current_ma[idx]);

    // Synchronize shadow state for protocol reporting
    g_shadow_vsense[idx] = (g_tmc_run_current_ma[idx] <= TMC_VSENSE_THRESHOLD_MA);
    g_shadow_ihold_irun[idx] = build_ihold_irun_reg(
        g_tmc_run_current_ma[idx], g_tmc_hold_current_ma[idx], g_shadow_vsense[idx]);
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

static int snap_microsteps(int x) {
    if (x <= 1)
        return 1;
    if (x >= 256)
        return 256;
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

static void settings_apply_clamps(float buf_switch_span_mm) {
    g_global_max_sps = clamp_i(g_global_max_sps, mm_per_min_to_sps(GLOBAL_MAX_MIN_MM_MIN),
                               mm_per_min_to_sps(GLOBAL_MAX_MAX_MM_MIN));
    g_sync_max_sps = sync_clamp_max_sps(g_sync_max_sps);
    g_tc_ts_retries = clamp_i(g_tc_ts_retries, TC_TS_RETRIES_MIN, TC_TS_RETRIES_MAX);
    g_tc_ts_retry_retract_mm =
        clamp_f(g_tc_ts_retry_retract_mm, TC_TS_RETRY_RETRACT_MIN_MM, TC_TS_RETRY_RETRACT_MAX_MM);
    g_tc_ts_park_mm = clamp_f(g_tc_ts_park_mm, TC_TS_PARK_MIN_MM, TC_TS_PARK_MAX_MM);
    g_buf_max_travel_mm = clamp_i(g_buf_max_travel_mm, BUF_TRAVEL_MIN_MM, BUF_TRAVEL_MAX_MM);
    g_buf_switch_span_half_mm =
        buf_switch_span_half_from_full(buf_switch_span_mm, g_buf_max_travel_mm);
    g_baseline_target_sps = motion_clamp_rate_sps(g_baseline_sps);
    g_baseline_sps = g_baseline_target_sps;

    for (int i = 0; i < NUM_LANES; i++) {
        g_tmc_rotation_distance[i] =
            clamp_f(g_tmc_rotation_distance[i], TMC_ROTATION_MIN_MM, TMC_ROTATION_MAX_MM);
        g_tmc_gear_ratio[i] = clamp_f(g_tmc_gear_ratio[i], TMC_GEAR_RATIO_MIN, TMC_GEAR_RATIO_MAX);
        g_tmc_full_steps[i] = (g_tmc_full_steps[i] == 400) ? 400 : 200;
        g_tmc_microsteps[i] = snap_microsteps(g_tmc_microsteps[i]);
        g_mm_per_step[i] =
            g_tmc_rotation_distance[i] /
            ((float)g_tmc_full_steps[i] * g_tmc_gear_ratio[i] * (float)g_tmc_microsteps[i]);
    }

    g_sync_reserve_pct = clamp_i(g_sync_reserve_pct, 0, SYNC_RESERVE_MAX_PCT);
    g_relay_catchup_frac = clamp_f(g_relay_catchup_frac, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    g_relay_neutral_frac = clamp_f(g_relay_neutral_frac, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    g_sync_compression_drain_frac =
        clamp_f(CONF_SYNC_COMPRESSION_DRAIN_FRAC, 0.0f, COMPRESSION_DRAIN_MAX_FRAC);
    g_sync_compression_drain_budget_mm =
        clamp_f(CONF_SYNC_COMPRESSION_DRAIN_BUDGET_MM, 0.0f, COMPRESSION_DRAIN_BUDGET_MAX_MM);
    g_sync_est_attack_alpha = clamp_f(CONF_SYNC_EST_ATTACK_ALPHA, SYNC_EST_ATTACK_MIN_ALPHA, 1.0f);
    g_sync_tension_fast_mm_s =
        clamp_f(CONF_SYNC_TENSION_FAST_MM_S, 1.0f, SYNC_TENSION_FAST_MAX_MM_S);
    g_sync_tension_probe_max_sps =
        clamp_i(CONF_SYNC_TENSION_PROBE_MAX_SPS, 0, mm_per_min_to_sps(TENSION_PROBE_MAX_MM_MIN));
    g_sync_tension_probe_up_sps_per_s = clamp_i(CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S, 0,
                                                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    g_sync_tension_probe_down_sps_per_s = clamp_i(CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S, 0,
                                                  mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    g_sync_tension_probe_neutral_sps_per_s =
        clamp_i(CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S, 0,
                mm_per_min_to_sps(TENSION_PROBE_RAMP_MAX_MM_MIN));
    /* REVIEW-05: the TLV `case` below is a bare memcpy by project convention,
       so a corrupt or foreign flash sector can land an unconstrained float in
       the control loop. Re-clamp here (settings_load_tlv() calls this as its
       final statement, and the v63 path calls it too) rather than adding a
       range check inside the TLV case, matching the g_relay_catchup_frac
       treatment above. */
    g_sync_psf_relief_mult =
        clamp_f(g_sync_psf_relief_mult, SYNC_RELIEF_MULT_MIN, SYNC_RELIEF_MULT_MAX);

    g_sync_compression_bias_frac =
        clamp_f(g_sync_compression_bias_frac, 0.0f, COMPRESSION_BIAS_MAX_FRAC);
    flow_schedule_reset_runtime();

    motion_limit_runtime_rates(false);
}

static void settings_load_tlv_tag(uint8_t tag, uint8_t len, const uint8_t *val,
                                  float *buf_switch_span_mm) {
    switch ((settings_tag_t)tag) {
    case TAG_FEED_SPS:
        if (len == sizeof(int))
            memcpy(&g_feed_sps, val, sizeof(int));
        break;
    case TAG_REV_SPS:
        if (len == sizeof(int))
            memcpy(&g_rev_sps, val, sizeof(int));
        break;
    case TAG_AUTO_SPS:
        if (len == sizeof(int))
            memcpy(&g_auto_sps, val, sizeof(int));
        break;
    case TAG_SYNC_MAX_SPS:
        if (len == sizeof(int))
            memcpy(&g_sync_max_sps, val, sizeof(int));
        break;
    case TAG_GLOBAL_MAX_SPS:
        if (len == sizeof(int))
            memcpy(&g_global_max_sps, val, sizeof(int));
        break;
    case TAG_SYNC_MIN_SPS:
        if (len == sizeof(int))
            memcpy(&g_sync_min_sps, val, sizeof(int));
        break;
    case TAG_SYNC_AUTO_STOP_MS:
        if (len == sizeof(int))
            memcpy(&g_sync_auto_stop_ms, val, sizeof(int));
        break;
    case TAG_LOAD_MAX_MM:
        if (len == sizeof(int))
            memcpy(&g_load_max_mm, val, sizeof(int));
        break;
    case TAG_TC_TS_RETRIES:
        if (len == sizeof(int))
            memcpy(&g_tc_ts_retries, val, sizeof(int));
        break;
    case TAG_TC_TS_RETRY_RETRACT_MM:
        if (len == sizeof(float))
            memcpy(&g_tc_ts_retry_retract_mm, val, sizeof(float));
        break;
    case TAG_TC_TS_PARK_MM:
        if (len == sizeof(float))
            memcpy(&g_tc_ts_park_mm, val, sizeof(float));
        break;
    case TAG_UNLOAD_MAX_MM:
        if (len == sizeof(int))
            memcpy(&g_unload_max_mm, val, sizeof(int));
        break;
    case TAG_UNLOAD_TENSION_BLOCK_MS:
        if (len == sizeof(int))
            memcpy(&g_unload_tension_block_ms, val, sizeof(int));
        break;
    case TAG_RELOAD_JOIN_DELAY_MS:
        if (len == sizeof(int))
            memcpy(&g_reload_join_delay_ms, val, sizeof(int));
        break;
    case TAG_AUTOLOAD_MAX_MM:
        if (len == sizeof(int))
            memcpy(&g_autoload_max_mm, val, sizeof(int));
        break;
    case TAG_AUTO_MODE:
        if (len == sizeof(int))
            memcpy(&g_auto_mode, val, sizeof(int));
        break;
    case TAG_DIST_IN_OUT:
        if (len == sizeof(int))
            memcpy(&g_dist_in_out, val, sizeof(int));
        break;
    case TAG_DIST_OUT_Y:
        if (len == sizeof(int))
            memcpy(&g_dist_out_y, val, sizeof(int));
        break;
    case TAG_DIST_Y_BUF:
        if (len == sizeof(int))
            memcpy(&g_dist_y_buf, val, sizeof(int));
        break;
    case TAG_BUF_BODY_LEN:
        if (len == sizeof(int))
            memcpy(&g_buf_body_len, val, sizeof(int));
        break;
    case TAG_BUF_MAX_TRAVEL_MM:
        if (len == sizeof(int))
            memcpy(&g_buf_max_travel_mm, val, sizeof(int));
        break;
    case TAG_BUF_SWITCH_SPAN_MM:
        if (len == sizeof(float))
            memcpy(buf_switch_span_mm, val, sizeof(float));
        break;
    case TAG_BASELINE_SPS:
        if (len == sizeof(int))
            memcpy(&g_baseline_sps, val, sizeof(int));
        break;
    case TAG_AUTOLOAD_RETRACT_MM:
        if (len == sizeof(int))
            memcpy(&g_autoload_retract_mm, val, sizeof(int));
        break;
    case TAG_SERVO_OPEN_US:
        if (len == sizeof(int))
            memcpy(&g_servo_open_us, val, sizeof(int));
        break;
    case TAG_SERVO_CLOSE_US:
        if (len == sizeof(int))
            memcpy(&g_servo_close_us, val, sizeof(int));
        break;
    case TAG_SERVO_BLOCK_US:
        if (len == sizeof(int))
            memcpy(&g_servo_block_us, val, sizeof(int));
        break;
    case TAG_SERVO_SETTLE_MS:
        if (len == sizeof(int))
            memcpy(&g_servo_settle_ms, val, sizeof(int));
        break;
    case TAG_CUT_FEED_SPS:
        if (len == sizeof(int))
            memcpy(&g_cut_feed_sps, val, sizeof(int));
        break;
    case TAG_CUT_FEED_MM:
        if (len == sizeof(int))
            memcpy(&g_cut_feed_mm, val, sizeof(int));
        break;
    case TAG_CUT_LENGTH_MM:
        if (len == sizeof(int))
            memcpy(&g_cut_length_mm, val, sizeof(int));
        break;
    case TAG_CUT_AMOUNT:
        if (len == sizeof(int))
            memcpy(&g_cut_amount, val, sizeof(int));
        break;
    case TAG_RUNOUT_COOLDOWN_MS:
        if (len == sizeof(int))
            memcpy(&g_runout_cooldown_ms, val, sizeof(int));
        break;
    case TAG_BUF_SENSOR_TYPE:
        if (len == sizeof(int))
            memcpy(&g_buf_sensor_type, val, sizeof(int));
        break;
    case TAG_BUF_PSF_MAX_COMP:
        if (len == sizeof(float))
            memcpy(&g_buf_psf_max_comp, val, sizeof(float));
        break;
    case TAG_BUF_PSF_MAX_TENS:
        if (len == sizeof(float))
            memcpy(&g_buf_psf_max_tens, val, sizeof(float));
        break;
    case TAG_BUF_PSF_NEUTRAL:
        if (len == sizeof(float))
            memcpy(&g_buf_psf_neutral, val, sizeof(float));
        break;
    case TAG_BUF_PSF_GOAL:
        if (len == sizeof(float))
            memcpy(&g_buf_goal, val, sizeof(float));
        break;
    case TAG_SYNC_KP_SPS:
        if (len == sizeof(int))
            memcpy(&g_sync_kp_sps, val, sizeof(int));
        break;
    case TAG_SYNC_RESERVE_PCT:
        if (len == sizeof(int))
            memcpy(&g_sync_reserve_pct, val, sizeof(int));
        break;
    case TAG_JOIN_SPS:
        if (len == sizeof(int))
            memcpy(&g_join_sps, val, sizeof(int));
        break;
    case TAG_PRESS_SPS:
        if (len == sizeof(int))
            memcpy(&g_press_sps, val, sizeof(int));
        break;
    case TAG_COMPRESSION_SPS:
        if (len == sizeof(int))
            memcpy(&g_compression_sps, val, sizeof(int));
        break;
    case TAG_FOLLOW_TIMEOUT_MS:
        if (len == sizeof(g_follow_timeout_ms))
            memcpy(g_follow_timeout_ms, val, sizeof(g_follow_timeout_ms));
        break;
    case TAG_AUTO_PRELOAD:
        if (len == 1)
            g_auto_preload = (val[0] != 0);
        else if (len == sizeof(int)) {
            int v;
            memcpy(&v, val, sizeof(int));
            g_auto_preload = (v != 0);
        }
        break;
    case TAG_ENABLE_CUTTER:
        if (len == 1)
            g_enable_cutter = (val[0] != 0);
        else if (len == sizeof(int)) {
            int v;
            memcpy(&v, val, sizeof(int));
            g_enable_cutter = (v != 0);
        }
        break;
    case TAG_UNLOAD_CUT:
        if (len == 1)
            g_unload_cut = (val[0] != 0);
        else if (len == sizeof(int)) {
            int v;
            memcpy(&v, val, sizeof(int));
            g_unload_cut = (v != 0);
        }
        break;
    case TAG_RELOAD_MODE:
        if (len == sizeof(int))
            memcpy(&g_reload_mode, val, sizeof(int));
        else if (len == 1)
            g_reload_mode = (val[0] != 0);
        break;
    case TAG_TMC_ROTATION_DISTANCE:
        if (len == sizeof(g_tmc_rotation_distance))
            memcpy(g_tmc_rotation_distance, val, sizeof(g_tmc_rotation_distance));
        break;
    case TAG_TMC_GEAR_RATIO:
        if (len == sizeof(g_tmc_gear_ratio))
            memcpy(g_tmc_gear_ratio, val, sizeof(g_tmc_gear_ratio));
        break;
    case TAG_TMC_FULL_STEPS:
        if (len == sizeof(g_tmc_full_steps))
            memcpy(g_tmc_full_steps, val, sizeof(g_tmc_full_steps));
        break;
    case TAG_TMC_MICROSTEPS:
        if (len == sizeof(g_tmc_microsteps))
            memcpy(g_tmc_microsteps, val, sizeof(g_tmc_microsteps));
        break;
    case TAG_TMC_TBL:
        if (len == sizeof(g_tmc_tbl))
            memcpy(g_tmc_tbl, val, sizeof(g_tmc_tbl));
        break;
    case TAG_TMC_TOFF:
        if (len == sizeof(g_tmc_toff))
            memcpy(g_tmc_toff, val, sizeof(g_tmc_toff));
        break;
    case TAG_TMC_HSTRT:
        if (len == sizeof(g_tmc_hstrt))
            memcpy(g_tmc_hstrt, val, sizeof(g_tmc_hstrt));
        break;
    case TAG_TMC_HEND:
        if (len == sizeof(g_tmc_hend))
            memcpy(g_tmc_hend, val, sizeof(g_tmc_hend));
        break;
    case TAG_TMC_INTERPOLATE:
        if (len == sizeof(g_tmc_interpolate))
            memcpy(g_tmc_interpolate, val, sizeof(g_tmc_interpolate));
        break;
    case TAG_TMC_STEALTHCHOP_SPS:
        if (len == sizeof(g_tmc_stealthchop_sps))
            memcpy(g_tmc_stealthchop_sps, val, sizeof(g_tmc_stealthchop_sps));
        break;
    case TAG_TMC_RUN_CURRENT_MA:
        if (len == sizeof(g_tmc_run_current_ma))
            memcpy(g_tmc_run_current_ma, val, sizeof(g_tmc_run_current_ma));
        break;
    case TAG_TMC_HOLD_CURRENT_MA:
        if (len == sizeof(g_tmc_hold_current_ma))
            memcpy(g_tmc_hold_current_ma, val, sizeof(g_tmc_hold_current_ma));
        break;
    case TAG_RELAY_CATCHUP_FRAC:
        if (len == sizeof(float))
            memcpy(&g_relay_catchup_frac, val, sizeof(float));
        break;
    case TAG_RELAY_NEUTRAL_FRAC:
        if (len == sizeof(float))
            memcpy(&g_relay_neutral_frac, val, sizeof(float));
        break;
    case TAG_SYNC_COMPRESSION_BIAS_FRAC:
        if (len == sizeof(float))
            memcpy(&g_sync_compression_bias_frac, val, sizeof(float));
        break;
    case TAG_SYNC_PSF_RELIEF_MULT:
        if (len == sizeof(float))
            memcpy(&g_sync_psf_relief_mult, val, sizeof(float));
        break;
    case TAG_FLASH_ERASE_COUNT:
        if (len == sizeof(uint32_t))
            memcpy(&g_flash_erase_count, val, sizeof(uint32_t));
        break;
    default:
        // Unknown tag: skip cleanly
        break;
    }
}

static void settings_load_tlv(const uint8_t *payload, size_t payload_len) {
    float buf_switch_span_mm = g_buf_switch_span_half_mm * FULL_SPAN_MULT_F;
    size_t offset = 0;
    while (offset + 2 <= payload_len) {
        uint8_t tag = payload[offset];
        uint8_t len = payload[offset + 1];
        if (offset + 2 + len > payload_len) {
            // Malformed record exceeding payload bounds
            break;
        }
        settings_load_tlv_tag(tag, len, payload + offset + 2, &buf_switch_span_mm);
        offset += 2 + len;
    }
    settings_apply_clamps(buf_switch_span_mm);
}

static void settings_load_v63(const settings_v63_t *s) {
    g_feed_sps = s->feed_sps;
    g_rev_sps = s->rev_sps;
    g_auto_sps = s->auto_sps;
    g_sync_max_sps = s->sync_max_sps;
    g_global_max_sps = s->global_max_sps;
    g_sync_min_sps = s->sync_min_sps;
    g_sync_auto_stop_ms = s->sync_auto_stop_ms;
    g_load_max_mm = s->load_max_mm;
    g_tc_ts_retries = s->tc_ts_retries;
    g_tc_ts_retry_retract_mm = s->tc_ts_retry_retract_mm;
    g_tc_ts_park_mm = s->tc_ts_park_mm;
    g_unload_max_mm = s->unload_max_mm;
    g_unload_tension_block_ms = s->unload_tension_block_ms;
    g_reload_join_delay_ms = s->reload_join_delay_ms;
    g_autoload_max_mm = s->autoload_max_mm;
    g_auto_mode = s->auto_mode;
    g_dist_in_out = s->dist_in_out;
    g_dist_out_y = s->dist_out_y;
    g_dist_y_buf = s->dist_y_buf;
    g_buf_body_len = s->buf_body_len;
    g_buf_max_travel_mm = s->buf_max_travel_mm;
    g_baseline_sps = s->baseline_sps;
    g_auto_preload = s->auto_preload;
    g_autoload_retract_mm = s->autoload_retract_mm;
    g_enable_cutter = s->enable_cutter;
    g_unload_cut = s->unload_cut;
    g_servo_open_us = s->servo_open_us;
    g_servo_close_us = s->servo_close_us;
    g_servo_block_us = s->servo_block_us;
    g_servo_settle_ms = s->servo_settle_ms;
    g_cut_feed_sps = s->cut_feed_sps;
    g_cut_feed_mm = s->cut_feed_mm;
    g_cut_length_mm = s->cut_length_mm;
    g_cut_amount = s->cut_amount;
    g_runout_cooldown_ms = s->runout_cooldown_ms;
    g_buf_sensor_type = s->buf_sensor_type;
    g_buf_psf_max_comp = s->buf_psf_max_comp;
    g_buf_psf_max_tens = s->buf_psf_max_tens;
    g_buf_psf_neutral = s->buf_psf_neutral;
    g_buf_goal = s->buf_psf_goal;
    g_sync_kp_sps = s->sync_kp_sps;
    g_sync_reserve_pct = s->sync_reserve_pct;
    g_join_sps = s->join_sps;
    g_press_sps = s->press_sps;
    g_compression_sps = s->compression_sps;
    for (int i = 0; i < NUM_LANES; i++) {
        g_follow_timeout_ms[i] = s->follow_timeout_ms[i];
    }
    g_reload_mode = s->reload_mode ? 1 : 0;
    for (int i = 0; i < NUM_LANES; i++) {
        g_tmc_rotation_distance[i] = s->tmc_rotation_distance[i];
        g_tmc_gear_ratio[i] = s->tmc_gear_ratio[i];
        g_tmc_full_steps[i] = s->tmc_full_steps[i];
        g_tmc_microsteps[i] = snap_microsteps(s->tmc_microsteps[i]);
        g_tmc_tbl[i] = s->tmc_tbl[i];
        g_tmc_toff[i] = s->tmc_toff[i];
        g_tmc_hstrt[i] = s->tmc_hstrt[i];
        g_tmc_hend[i] = s->tmc_hend[i];
        g_tmc_interpolate[i] = s->tmc_interpolate[i];
        g_tmc_stealthchop_sps[i] = s->tmc_stealthchop_sps[i];
        g_tmc_run_current_ma[i] = s->tmc_run_current_ma[i];
        g_tmc_hold_current_ma[i] = s->tmc_hold_current_ma[i];
    }
    g_relay_catchup_frac = s->relay_catchup_frac;
    g_relay_neutral_frac = s->relay_neutral_frac;
    g_sync_compression_bias_frac = s->sync_compression_bias_frac;
    g_flash_erase_count = s->flash_erase_count;

    settings_apply_clamps(s->buf_switch_span_mm);
}

void settings_load(void) {
    // Read settings straight from memory-mapped flash (XIP). Dual ping-pong
    // sectors (A and B) provide atomic persistence resilient against power
    // loss mid-erase or mid-program. Three guards must pass per sector: magic,
    // version, and CRC. If both valid, select newer sequence; if one valid,
    // select valid; if neither valid, fall back to compiled defaults.
    const uint8_t *sector_a = (const uint8_t *)(XIP_BASE + SETTINGS_FLASH_OFFSET_A);
    const uint8_t *sector_b = (const uint8_t *)(XIP_BASE + SETTINGS_FLASH_OFFSET_B);

    uint32_t seq_a = 0;
    uint32_t seq_b = 0;
    sector_version_t ver_a = settings_validate_sector(sector_a, &seq_a);
    sector_version_t ver_b = settings_validate_sector(sector_b, &seq_b);

    const uint8_t *chosen = NULL;
    int chosen_sector = -1;
    sector_version_t chosen_ver = SECTOR_INVALID;
    uint32_t chosen_seq = 0;

    if (ver_a != SECTOR_INVALID && ver_b != SECTOR_INVALID) {
        if ((int32_t)(seq_a - seq_b) > 0) {
            chosen = sector_a;
            chosen_sector = 0;
            chosen_ver = ver_a;
            chosen_seq = seq_a;
        } else {
            chosen = sector_b;
            chosen_sector = 1;
            chosen_ver = ver_b;
            chosen_seq = seq_b;
        }
    } else if (ver_a != SECTOR_INVALID) {
        chosen = sector_a;
        chosen_sector = 0;
        chosen_ver = ver_a;
        chosen_seq = seq_a;
    } else if (ver_b != SECTOR_INVALID) {
        chosen = sector_b;
        chosen_sector = 1;
        chosen_ver = ver_b;
        chosen_seq = seq_b;
    }

    if (!chosen) {
        g_active_sector = 1;
        g_seq = 0;
        settings_defaults();
        tmc_apply_all();
        return;
    }

    // Seed defaults first so newly introduced tags retain compiled defaults
    settings_defaults();

    g_active_sector = chosen_sector;
    g_seq = chosen_seq;

    if (chosen_ver == SECTOR_V64) {
        const settings_header_t *hdr = (const settings_header_t *)chosen;
        settings_load_tlv(chosen + sizeof(settings_header_t), hdr->payload_len);
    } else if (chosen_ver == SECTOR_V63) {
        const settings_v63_t *s63 = (const settings_v63_t *)chosen;
        settings_load_v63(s63);
    }

    tmc_apply_all();
}
