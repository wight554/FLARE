/// @file protocol.c
/// @brief USB-CDC serial protocol: line parsing and the motion/system command
///        handlers plus SET/GET tunable dispatch. Status dump lives in
///        protocol_status.c; advanced TMC commands in protocol_tmc.c.
/// @details Wire format CMD:params\n -> OK:.../ER:... replies, EV: events. Reply
///          semantics (one OK or ER per command) are a contract. See MANUAL.md;
///          spec project-architecture (serial protocol).

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"

#include "controller_shared.h"
#include "cutter.h"
#include "motion.h"
#include "protocol.h"
#include "protocol_internal.h"
#include "settings_store.h"
#include "sync.h"
#include "toolchange.h"

#define CMD_POLL_BYTE_BUDGET 128
#define CMD_POLL_COMMAND_BUDGET 4
#define CMD_EVENT_WINDOW_MS 100
#define CMD_EVENT_BUDGET 8
#define CMD_PARAM_MAX 64
#define CMD_PARAM_SCAN_WIDTH 63
#define CMD_PARAM_SCAN_FMT "%63[^:]"
#define CMD_VALUE_MAX 32
#define CMD_VALUE_SCAN_FMT "%31s"
#define CMD_OPTION_TOKEN_MAX 8
#define CMD_OPTION_TOKEN_SCAN_FMT "%7[^:]:%7s"
#define BUF_SWITCH_SPAN_MIN_MM 2.0f
#define BUF_SWITCH_SPAN_HALF_MIN_MM (BUF_SWITCH_SPAN_MIN_MM * HALF_F)
#define SECONDS_PER_MINUTE_F 60.0f
#define ROUND_TO_NEAREST_F 0.5f
#define MIN_RUN_RATE_SPS 200
#define MAX_RUN_RATE_SPS 50000
#define MIN_LOW_RATE_SPS 10
#define MAX_LOW_RATE_SPS 10000
#define GLOBAL_MAX_MIN_MM_MIN 1000.0f
#define GLOBAL_MAX_MAX_MM_MIN 12000.0f
#define AUTOLOAD_RETRACT_MAX_MM 50
#define LONG_TIMEOUT_MAX_MS 60000
#define RELOAD_JOIN_MAX_MS 10000
#define PATH_DIST_MIN_MM 10
#define PATH_DIST_MAX_MM 5000
#define BUF_TRAVEL_MIN_MM 10
#define BUF_TRAVEL_MAX_MM 1000
#define AUTOLOAD_MAX_MIN_MM 10
#define AUTOLOAD_MAX_MAX_MM 10000
#define LOAD_UNLOAD_MIN_MM 100
#define LOAD_UNLOAD_MAX_MM 10000
#define FOLLOW_TIMEOUT_MIN_MS 1000
#define FOLLOW_TIMEOUT_MAX_MS 60000
#define DRIVER_CURRENT_MAX_MA 2000
#define DRIVER_MICROSTEPS_MAX 256
#define DRIVER_FULL_STEPS_400 400
#define DRIVER_FULL_STEPS_200 200
#define DRIVER_TBL_MAX 3
#define DRIVER_TOFF_MAX 15
#define DRIVER_HSTRT_MAX 7
#define DRIVER_HEND_MIN (-3)
#define DRIVER_HEND_MAX 12
#define SYNC_PSF_SLEW_MAX 50000.0f
#define SYNC_PSF_FILTER_MIN_MM 0.1f
#define SYNC_PSF_FILTER_MAX_MM 500.0f
#define SYNC_PSF_DECAY_MAX_SPS_PER_S 200000.0f
#define SYNC_RESERVE_MAX_PCT 150
#define COMPRESSION_BIAS_MAX_FRAC 0.7f
#define SYNC_AUTO_STOP_MAX_MS 30000
#define PSF_KD_MAX 100.0f
#define RELAY_FRAC_MIN 0.5f
#define RELAY_FRAC_MAX 3.0f
#define RELAY_TRIM_STEP_MAX_SPS 5000
#define RELAY_TRIM_CLAMP_MAX_SPS 50000
#define COMPRESSION_DRAIN_MAX_FRAC 0.9f
#define COMPRESSION_DRAIN_BUDGET_MAX_MM 25.0f
#define SYNC_EST_ATTACK_MIN_ALPHA 0.65f
#define SYNC_TENSION_FAST_MAX_MM_S 200.0f
#define TENSION_PROBE_MAX_MM_MIN 6000.0f
#define TENSION_PROBE_RAMP_MAX_MM_MIN 12000.0f
#define SERVO_MIN_PULSE_US 400
#define SERVO_MAX_PULSE_US 2700
#define SERVO_SETTLE_MIN_MS 100
#define SERVO_SETTLE_MAX_MS 2000
#define CUT_FEED_MIN_MM_MIN 100
#define CUT_FEED_MAX_MM_MIN 30000
#define CUT_FEED_MAX_MM 200
#define CUT_LENGTH_MAX_MM 50
#define CUT_AMOUNT_MAX 5
#define BOOTSEL_REPLY_DELAY_MS 100

typedef struct {
    char buf[CMD_PARAM_MAX];
    int pos;
    bool overflow;
} cmd_parser_t;

static cmd_parser_t g_cmd = {0};
static uint32_t g_cmd_event_window_ms = 0;
static int g_cmd_event_count = 0;
bool g_live_tune_lock = false;

typedef enum {
    MANUAL_UNLOAD_IDLE,
    MANUAL_UNLOAD_WAIT_CUT,
    MANUAL_UNLOAD_WAIT_FIRST_CLEAR,
    MANUAL_UNLOAD_WAIT_IN_CLEAR
} manual_unload_state_t;

typedef struct {
    manual_unload_state_t state;
    lane_t *lane;
    bool finish_to_in;
    bool cut_pending;
} manual_unload_ctx_t;

static manual_unload_ctx_t g_manual_unload = {0};

// ============================================================================
// Command helpers — parsing, replies/events, activity gating, manual unload
// ============================================================================

bool manual_unload_active(void) {
    return g_manual_unload.state != MANUAL_UNLOAD_IDLE;
}

static bool parse_mv_option(const char *tok, bool *forward, bool *ignore_buffer) {
    if (tok[0] == '\0')
        return true;

    if (!strcmp(tok, "F") || !strcmp(tok, "f") || !strcmp(tok, "+")) {
        *forward = true;
    } else if (!strcmp(tok, "R") || !strcmp(tok, "r") || !strcmp(tok, "B") || !strcmp(tok, "b") ||
               !strcmp(tok, "-")) {
        *forward = false;
    } else if (!strcmp(tok, "I") || !strcmp(tok, "i")) {
        *ignore_buffer = true;
    } else {
        return false;
    }
    return true;
}

static void manual_unload_reset(void) {
    g_manual_unload.state = MANUAL_UNLOAD_IDLE;
    g_manual_unload.lane = NULL;
    g_manual_unload.finish_to_in = false;
    g_manual_unload.cut_pending = false;
}

/* Called on successful completion of an unload sequence (OUT cleared).
   If exactly one lane still has filament past its OUT sensor, select it as the
   active lane so the operator does not have to issue a T: command manually.
   This is the key recovery step for the double-load scenario: after the bad
   lane is unloaded, the good lane auto-selects and normal sync can resume. */
static void maybe_autoselect_lane(void) {
    bool lane1_present = lane_out_present(&g_lane_l1);
    bool lane2_present = lane_out_present(&g_lane_l2);
    if (lane1_present && !lane2_present && g_active_lane != 1)
        set_active_lane(1);
    else if (lane2_present && !lane1_present && g_active_lane != 2)
        set_active_lane(2);
}

static void manual_unload_complete(void) {
    maybe_autoselect_lane();
    manual_unload_reset();
}

static bool live_tune_locked_param(const char *param) {
    return !strcmp(param, "BASELINE_RATE") || !strcmp(param, "BASELINE_SPS") ||
           !strcmp(param, "COMPRESSION_BIAS_FRAC") || !strcmp(param, "NEUTRAL_CREEP_TIMEOUT_MS") ||
           !strcmp(param, "NEUTRAL_CREEP_RATE") || !strcmp(param, "NEUTRAL_CREEP_RATE_SPS_PER_S") ||
           !strcmp(param, "NEUTRAL_CREEP_CAP") || !strcmp(param, "NEUTRAL_CREEP_CAP_FRAC") ||
           !strcmp(param, "RELAY_CATCHUP_FRAC") || !strcmp(param, "RELAY_NEUTRAL_FRAC") ||
           !strcmp(param, "SYNC_RELAY_TRIM_STEP_SPS") ||
           !strcmp(param, "SYNC_RELAY_TRIM_CLAMP_SPS") ||
           !strcmp(param, "SYNC_COMPRESSION_DRAIN_FRAC") ||
           !strcmp(param, "SYNC_COMPRESSION_DRAIN_BUDGET_MM") ||
           !strcmp(param, "SYNC_EST_ATTACK_ALPHA") || !strcmp(param, "SYNC_TENSION_FAST_MM_S") ||
           !strcmp(param, "SYNC_TENSION_PROBE_MAX") || !strcmp(param, "SYNC_TENSION_PROBE_UP") ||
           !strcmp(param, "SYNC_TENSION_PROBE_DOWN") ||
           !strcmp(param, "SYNC_TENSION_PROBE_NEUTRAL") || !strcmp(param, "RELAY_MIN_FLIP_MM") ||
           !strcmp(param, "RELAY_COLLAPSE_DELAY_MS") ||
           !strcmp(param, "RELAY_COLLAPSE_RAMP_MULT") || !strcmp(param, "RELAY_COLLAPSE_CAP_MS") ||
           !strcmp(param, "VAR_BLEND_FRAC") || !strcmp(param, "BUF_VARIANCE_BLEND_FRAC") ||
           !strcmp(param, "VAR_BLEND_REF_MM") || !strcmp(param, "BUF_VARIANCE_BLEND_REF_MM");
}

static float buf_switch_span_half_from_full(float span_mm, int max_travel_mm) {
    float max_span_mm = (float)max_travel_mm;
    if (max_span_mm < BUF_SWITCH_SPAN_MIN_MM)
        max_span_mm = BUF_SWITCH_SPAN_MIN_MM;
    return clamp_f(span_mm, BUF_SWITCH_SPAN_MIN_MM, max_span_mm) * HALF_F;
}

static bool controller_activity_in_progress(void) {
    if (manual_unload_active())
        return true;
    /* TC_ERROR means the TC concluded (failed) — not running. Treat as idle
     * so BS/BL/SV/LD/RS are usable for error recovery without a board reset. */
    if ((g_tc_ctx.state != TC_IDLE && g_tc_ctx.state != TC_ERROR) || cutter_busy() ||
        g_boot_stabilizing)
        return true;
    if (g_lane_l1.task != TASK_IDLE || g_lane_l2.task != TASK_IDLE)
        return true;
    if (sync_buffer_lock_motor_moving())
        return true;
    return false;
}

static bool controller_hard_activity_in_progress(void) {
    if (manual_unload_active())
        return true;
    if (g_tc_ctx.state != TC_IDLE && g_tc_ctx.state != TC_ERROR)
        return true;
    if (cutter_busy())
        return true;
    return false;
}

static bool cmd_event_permitted(void) {
    if (!stdio_usb_connected())
        return false;

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now_ms - g_cmd_event_window_ms) >= CMD_EVENT_WINDOW_MS) {
        g_cmd_event_window_ms = now_ms;
        g_cmd_event_count = 0;
    }

    if (g_cmd_event_count >= CMD_EVENT_BUDGET)
        return false;
    g_cmd_event_count++;
    return true;
}

static void cmd_write_line(const char *prefix, const char *type, const char *data,
                           bool best_effort) {
    if (best_effort && !cmd_event_permitted())
        return;

    char line[CMD_LINE_MAX];
    int len;
    if (data && *data)
        len = snprintf(line, sizeof(line), "%s%s:%s\n", prefix, type, data);
    else
        len = snprintf(line, sizeof(line), "%s%s\n", prefix, type);
    if (len <= 0)
        return;
    if (len >= (int)sizeof(line))
        len = (int)sizeof(line) - 1;
    (void)stdio_put_string(line, len, false, false);
}

void cmd_reply(const char *status, const char *data) {
    cmd_write_line("", status, data, false);
}

void cmd_event(const char *type, const char *data) {
    cmd_write_line("EV:", type, data, true);
}

void cmd_event_critical(const char *type, const char *data) {
    if (!stdio_usb_connected())
        return;
    cmd_write_line("EV:", type, data, false);
}

static lane_t *get_active_lane_and_clear_error(void) {
    if (g_tc_ctx.state == TC_ERROR)
        tc_abort();
    lane_t *lane = lane_ptr(g_active_lane);
    if (!lane) {
        cmd_reply("ER", "NO_ACTIVE_LANE");
        return NULL;
    }
    return lane;
}

static bool parse_optional_lane_payload(const char *p, int *lane_out, bool *explicit_lane) {
    *explicit_lane = false;
    *lane_out = g_active_lane;

    if (!p || p[0] == '\0')
        return true;
    if ((p[0] == '1' || p[0] == '2') && p[1] == '\0') {
        *explicit_lane = true;
        *lane_out = p[0] - '0';
        return true;
    }
    return false;
}

static void start_manual_unload_lane(lane_t *lane, bool suppress_event, uint32_t now_ms) {
    lane->unload_to_in = false;
    lane->unload_buf_recover_done = false;
    lane_start(lane, TASK_UNLOAD, g_rev_sps, false, now_ms, (float)g_unload_max_mm);
    lane->suppress_unloaded_event = suppress_event;
}

static void start_manual_unload_to_in(lane_t *lane, uint32_t now_ms) {
    lane->unload_to_in = true;
    lane->unload_buf_recover_done = false;
    lane_start(lane, TASK_UNLOAD, g_rev_sps, false, now_ms, (float)g_unload_max_mm);
}

static void manual_unload_tick(uint32_t now_ms) {
    lane_t *lane = g_manual_unload.lane;
    if (!lane) {
        manual_unload_reset();
        return;
    }

    switch (g_manual_unload.state) {
    case MANUAL_UNLOAD_IDLE:
        return;

    case MANUAL_UNLOAD_WAIT_CUT:
        if (!cutter_busy()) {
            if (cutter_failed()) {
                cmd_event_critical("UNLOAD:FAULT", "CUT_FAILED");
                manual_unload_reset();
            } else {
                g_manual_unload.cut_pending = false;
                start_manual_unload_lane(lane, g_manual_unload.finish_to_in, now_ms);
                if (g_manual_unload.finish_to_in) {
                    g_manual_unload.state = MANUAL_UNLOAD_WAIT_FIRST_CLEAR;
                } else {
                    manual_unload_complete();
                }
            }
        }
        break;

    case MANUAL_UNLOAD_WAIT_FIRST_CLEAR:
        if (lane->task == TASK_IDLE) {
            if (lane_out_present(lane)) {
                cmd_event_critical("UNLOAD:FAULT", "OUT_BLOCKED");
                manual_unload_reset();
                return;
            }
            if (g_manual_unload.cut_pending) {
                if (on_al(&g_y_split)) {
                    g_manual_unload.cut_pending = false;
                    if (g_manual_unload.finish_to_in) {
                        start_manual_unload_to_in(lane, now_ms);
                        g_manual_unload.state = MANUAL_UNLOAD_WAIT_IN_CLEAR;
                    } else {
                        manual_unload_complete();
                    }
                } else {
                    cutter_start(lane, true, now_ms);
                    g_manual_unload.state = MANUAL_UNLOAD_WAIT_CUT;
                }
            } else if (g_manual_unload.finish_to_in) {
                start_manual_unload_to_in(lane, now_ms);
                g_manual_unload.state = MANUAL_UNLOAD_WAIT_IN_CLEAR;
            } else {
                manual_unload_complete();
            }
        }
        break;

    case MANUAL_UNLOAD_WAIT_IN_CLEAR:
        if (lane->task == TASK_IDLE) {
            manual_unload_complete();
        }
        break;
    }
}

char g_marker_tag[MARKER_TAG_LEN] = {0};
uint16_t g_marker_seq = 0;

// ============================================================================
// GET handlers — report runtime tunables (grouped by domain)
// ============================================================================

static bool cmd_get_motion_params(const char *param, int idx, char *out, size_t out_len) {
    if (!strcmp(param, "FEED_RATE"))
        snprintf(out, out_len, "FEED_RATE:%.1f", (double)sps_to_mm_per_min_idx(g_feed_sps, idx));
    else if (!strcmp(param, "REV_RATE"))
        snprintf(out, out_len, "REV_RATE:%.1f", (double)sps_to_mm_per_min_idx(g_rev_sps, idx));
    else if (!strcmp(param, "AUTO_RATE"))
        snprintf(out, out_len, "AUTO_RATE:%.1f", (double)sps_to_mm_per_min_idx(g_auto_sps, idx));
    else if (!strcmp(param, "GLOBAL_MAX_RATE"))
        snprintf(out, out_len, "GLOBAL_MAX_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(g_global_max_sps, idx));
    else if (!strcmp(param, "GLOBAL_MAX_ACCEL")) {
        float tick_s = (float)g_ramp_tick_ms / MS_PER_SECOND_F;
        float accel = (float)g_ramp_step_sps * g_mm_per_step[0] / tick_s;
        snprintf(out, out_len, "GLOBAL_MAX_ACCEL:%.0f", (double)accel);
    }
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "RAMP_TICK_MS"))
        snprintf(out, out_len, "RAMP_TICK_MS:%d", g_ramp_tick_ms);
    else if (!strcmp(param, "PRE_RAMP_RATE"))
        snprintf(out, out_len, "PRE_RAMP_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(g_pre_ramp_sps, idx));
    else if (!strcmp(param, "STARTUP_MS"))
        snprintf(out, out_len, "STARTUP_MS:%d", g_motion_startup_ms);
#endif
    else if (!strcmp(param, "AUTOLOAD_MAX"))
        snprintf(out, out_len, "AUTOLOAD_MAX:%d", g_autoload_max_mm);
    else if (!strcmp(param, "LOAD_MAX"))
        snprintf(out, out_len, "LOAD_MAX:%d", g_load_max_mm);
    else if (!strcmp(param, "UNLOAD_MAX"))
        snprintf(out, out_len, "UNLOAD_MAX:%d", g_unload_max_mm);
    else if (!strcmp(param, "TC_LOAD_MM"))
        snprintf(out, out_len, "TC_LOAD_MM:%d", g_load_max_mm);
    else if (!strcmp(param, "TC_UNLOAD_MM"))
        snprintf(out, out_len, "TC_UNLOAD_MM:%d", g_unload_max_mm);
    else if (!strcmp(param, "RETRACT_MM"))
        snprintf(out, out_len, "RETRACT_MM:%d", g_autoload_retract_mm);
    else
        return false;
    return true;
}

static bool cmd_get_tmc_params(const char *param, int idx, char *out, size_t out_len) {
    if (!strcmp(param, "MICROSTEPS"))
        snprintf(out, out_len, "MICROSTEPS:%d", g_tmc_microsteps[idx]);
    else if (!strcmp(param, "INTERPOLATE"))
        snprintf(out, out_len, "INTERPOLATE:%d", g_tmc_interpolate[idx] ? 1 : 0);
    else if (!strcmp(param, "STEALTHCHOP"))
        snprintf(out, out_len, "STEALTHCHOP:%.1f",
                 (double)sps_to_mm_per_min_idx(g_tmc_stealthchop_sps[idx], idx));
    else if (!strcmp(param, "DRIVER_TBL"))
        snprintf(out, out_len, "DRIVER_TBL:%d", g_tmc_tbl[idx]);
    else if (!strcmp(param, "DRIVER_TOFF"))
        snprintf(out, out_len, "DRIVER_TOFF:%d", g_tmc_toff[idx]);
    else if (!strcmp(param, "DRIVER_HSTRT"))
        snprintf(out, out_len, "DRIVER_HSTRT:%d", g_tmc_hstrt[idx]);
    else if (!strcmp(param, "DRIVER_HEND"))
        snprintf(out, out_len, "DRIVER_HEND:%d", g_tmc_hend[idx]);
    else if (!strcmp(param, "ROTATION_DIST"))
        snprintf(out, out_len, "ROTATION_DIST:%.3f", (double)g_tmc_rotation_distance[idx]);
    else if (!strcmp(param, "GEAR_RATIO"))
        snprintf(out, out_len, "GEAR_RATIO:%.3f", (double)g_tmc_gear_ratio[idx]);
    else if (!strcmp(param, "FULL_STEPS"))
        snprintf(out, out_len, "FULL_STEPS:%d", g_tmc_full_steps[idx]);
    else if (!strcmp(param, "RUN_CURRENT_MA"))
        snprintf(out, out_len, "RUN_CURRENT_MA:%d", g_tmc_run_current_ma[idx]);
    else if (!strcmp(param, "HOLD_CURRENT_MA"))
        snprintf(out, out_len, "HOLD_CURRENT_MA:%d", g_tmc_hold_current_ma[idx]);
    else
        return false;
    return true;
}

static bool cmd_get_buffer_geometry_params(const char *param, int idx, char *out, size_t out_len) {
    if (!strcmp(param, "SYNC_MAX_RATE"))
        snprintf(out, out_len, "SYNC_MAX_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(g_sync_max_sps, idx));
    else if (!strcmp(param, "SYNC_MIN_RATE"))
        snprintf(out, out_len, "SYNC_MIN_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(g_sync_min_sps, idx));
    else if (!strcmp(param, "SYNC_RAMP_ACCEL")) {
        float tick_s = (float)g_sync_tick_ms / MS_PER_SECOND_F;
        float accel = (float)g_sync_ramp_up_sps * g_mm_per_step[0] / tick_s;
        snprintf(out, out_len, "SYNC_RAMP_ACCEL:%.0f", (double)accel);
    } else if (!strcmp(param, "SYNC_RAMP_DECEL")) {
        float tick_s = (float)g_sync_tick_ms / MS_PER_SECOND_F;
        float accel = (float)g_sync_ramp_dn_sps * g_mm_per_step[0] / tick_s;
        snprintf(out, out_len, "SYNC_RAMP_DECEL:%.0f", (double)accel);
    }
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "SYNC_TICK_MS"))
        snprintf(out, out_len, "SYNC_TICK_MS:%d", g_sync_tick_ms);
    else if (!strcmp(param, "BUF_HYST"))
        snprintf(out, out_len, "BUF_HYST:%d", g_buf_hyst_ms);
    else if (!strcmp(param, "BUF_PREDICT_THR_MS"))
        snprintf(out, out_len, "BUF_PREDICT_THR_MS:%d", g_buf_predict_thr_ms);
#endif
    else if (!strcmp(param, "BUF_SWITCH_SPAN"))
        snprintf(out, out_len, "BUF_SWITCH_SPAN:%.3f",
                 (double)(g_buf_switch_span_half_mm * FULL_SPAN_MULT_F));
    else if (!strcmp(param, "BL"))
        snprintf(out, out_len, "BL:%s", sync_buffer_lock_arm_str());
    else if (!strcmp(param, "SYNC_STATE"))
        snprintf(out, out_len, "SYNC_STATE:%d", (int)g_sync_state);
    else if (!strcmp(param, "BASELINE_RATE"))
        snprintf(out, out_len, "BASELINE_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(g_baseline_target_sps, idx));
    else if (!strcmp(param, "BASELINE_SPS"))
        snprintf(out, out_len, "BASELINE_SPS:%d", g_baseline_target_sps);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "BASELINE_ALPHA"))
        snprintf(out, out_len, "BASELINE_ALPHA:%.3f", (double)g_baseline_alpha);
#endif
    else if (!strcmp(param, "BUF_SENSOR"))
        snprintf(out, out_len, "BUF_SENSOR:%d", g_buf_sensor_type);
    else if (!strcmp(param, "BUF_PSF_MAX_COMP"))
        snprintf(out, out_len, "BUF_PSF_MAX_COMP:%.3f", (double)g_buf_psf_max_comp);
    else if (!strcmp(param, "BUF_PSF_MAX_TENS"))
        snprintf(out, out_len, "BUF_PSF_MAX_TENS:%.3f", (double)g_buf_psf_max_tens);
    else if (!strcmp(param, "BUF_PSF_NEUTRAL"))
        snprintf(out, out_len, "BUF_PSF_NEUTRAL:%.3f", (double)g_buf_psf_neutral);
    else if (!strcmp(param, "BUF_GOAL"))
        snprintf(out, out_len, "BUF_GOAL:%.3f", (double)g_buf_goal);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "BUF_ALPHA"))
        snprintf(out, out_len, "BUF_ALPHA:%.3f", (double)g_buf_analog_alpha);
#endif
    else
        return false;
    return true;
}

static bool cmd_get_sync_control_params(const char *param, int idx, char *out, size_t out_len) {
    if (!strcmp(param, "SYNC_REFILL_MM"))
        snprintf(out, out_len, "SYNC_REFILL_MM:%d", (int)g_sync_refill_effort_mm);
    else if (!strcmp(param, "SYNC_RELIEVE_MM"))
        snprintf(out, out_len, "SYNC_RELIEVE_MM:%d", (int)g_sync_relieve_effort_mm);
    else if (!strcmp(param, "SYNC_KP_RATE"))
        snprintf(out, out_len, "SYNC_KP_RATE:%.1f", (double)sps_to_mm_per_min(g_sync_kp_sps));
    else if (!strcmp(param, "KD_PSF"))
        snprintf(out, out_len, "KD_PSF:%.3f", (double)g_kd_psf);
    else if (!strcmp(param, "SYNC_PSF_SLEW_PER_MM"))
        snprintf(out, out_len, "SYNC_PSF_SLEW_PER_MM:%.1f", (double)g_sync_psf_slew_per_mm);
    else if (!strcmp(param, "SYNC_PSF_FILTER_MM"))
        snprintf(out, out_len, "SYNC_PSF_FILTER_MM:%.2f", (double)g_sync_psf_filter_mm);
    else if (!strcmp(param, "SYNC_PSF_DECAY_SPS_PER_S"))
        snprintf(out, out_len, "SYNC_PSF_DECAY_SPS_PER_S:%.1f", (double)g_sync_psf_decay_sps_per_s);
    else if (!strcmp(param, "PSF_STAB_STAGNANT_MS"))
        snprintf(out, out_len, "PSF_STAB_STAGNANT_MS:%d", g_psf_stab_stagnant_ms);
    else if (!strcmp(param, "PSF_STAB_STAGNANT_NORM"))
        snprintf(out, out_len, "PSF_STAB_STAGNANT_NORM:%.3f", (double)g_psf_stab_stagnant_norm);
    else if (!strcmp(param, "PSF_STAB_RAIL_BREAK_MS"))
        snprintf(out, out_len, "PSF_STAB_RAIL_BREAK_MS:%d", g_psf_stab_rail_break_ms);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "SYNC_OVERSHOOT_PCT"))
        snprintf(out, out_len, "SYNC_OVERSHOOT_PCT:%d", g_sync_overshoot_pct);
#endif
    else if (!strcmp(param, "SYNC_RESERVE_PCT"))
        snprintf(out, out_len, "SYNC_RESERVE_PCT:%d", g_sync_reserve_pct);
    else if (!strcmp(param, "COMPRESSION_BIAS_FRAC"))
        snprintf(out, out_len, "COMPRESSION_BIAS_FRAC:%.3f", (double)g_sync_compression_bias_frac);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "NEUTRAL_CREEP_TIMEOUT_MS"))
        snprintf(out, out_len, "NEUTRAL_CREEP_TIMEOUT_MS:%d", g_neutral_creep_timeout_ms);
    else if (!strcmp(param, "NEUTRAL_CREEP_RATE") || !strcmp(param, "NEUTRAL_CREEP_RATE_SPS_PER_S"))
        snprintf(out, out_len, "%s:%d", param, g_neutral_creep_rate_sps_per_s);
    else if (!strcmp(param, "NEUTRAL_CREEP_CAP") || !strcmp(param, "NEUTRAL_CREEP_CAP_FRAC"))
        snprintf(out, out_len, "%s:%d", param, g_neutral_creep_cap_frac);
    else if (!strcmp(param, "開設_blend_frac") || !strcmp(param, "BUF_VARIANCE_BLEND_FRAC"))
        snprintf(out, out_len, "%s:%.3f", param, (double)g_buf_variance_blend_frac);
    else if (!strcmp(param, "開設_blend_ref_mm") || !strcmp(param, "BUF_VARIANCE_BLEND_REF_MM"))
        snprintf(out, out_len, "%s:%.3f", param, (double)g_buf_variance_blend_ref_mm);
#endif
    else if (!strcmp(param, "SYNC_AUTO_STOP"))
        snprintf(out, out_len, "SYNC_AUTO_STOP:%d", g_sync_auto_stop_ms);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "SYNC_TENSION_STOP_MS"))
        snprintf(out, out_len, "SYNC_TENSION_STOP_MS:%d", g_sync_tension_dwell_stop_ms);
    else if (!strcmp(param, "SYNC_TENSION_RAMP_MS"))
        snprintf(out, out_len, "SYNC_TENSION_RAMP_MS:%d", g_sync_tension_ramp_delay_ms);
    else if (!strcmp(param, "SYNC_OVERSHOOT_NEUTRAL_EXT"))
        snprintf(out, out_len, "SYNC_OVERSHOOT_NEUTRAL_EXT:%d", g_sync_overshoot_neutral_extend);
    else if (!strcmp(param, "SYNC_INT_GAIN"))
        snprintf(out, out_len, "SYNC_INT_GAIN:%.4f", (double)g_sync_reserve_integral_gain);
    else if (!strcmp(param, "SYNC_INT_CLAMP"))
        snprintf(out, out_len, "SYNC_INT_CLAMP:%.3f", (double)g_sync_reserve_integral_clamp_mm);
    else if (!strcmp(param, "SYNC_INT_DECAY_MS"))
        snprintf(out, out_len, "SYNC_INT_DECAY_MS:%d", g_sync_reserve_integral_decay_ms);
    else if (!strcmp(param, "EST_SIGMA_CAP"))
        snprintf(out, out_len, "EST_SIGMA_CAP:%.3f", (double)g_est_sigma_hard_cap_mm);
    else if (!strcmp(param, "EST_LOW_CF_THR"))
        snprintf(out, out_len, "EST_LOW_CF_THR:%.3f", (double)g_est_low_cf_warn_threshold);
    else if (!strcmp(param, "EST_FALLBACK_THR"))
        snprintf(out, out_len, "EST_FALLBACK_THR:%.3f", (double)g_est_fallback_cf_threshold);
#endif
    else
        return false;
    return true;
}

static bool cmd_get_sync_relay_probe_params(const char *param, int idx, char *out, size_t out_len) {
    if (!strcmp(param, "RELAY_CATCHUP_FRAC"))
        snprintf(out, out_len, "RELAY_CATCHUP_FRAC:%.3f", (double)g_relay_catchup_frac);
    else if (!strcmp(param, "RELAY_NEUTRAL_FRAC"))
        snprintf(out, out_len, "RELAY_NEUTRAL_FRAC:%.3f", (double)g_relay_neutral_frac);
    else if (!strcmp(param, "SYNC_RELAY_TRIM_STEP_SPS"))
        snprintf(out, out_len, "SYNC_RELAY_TRIM_STEP_SPS:%d", g_sync_relay_trim_step_sps);
    else if (!strcmp(param, "SYNC_RELAY_TRIM_CLAMP_SPS"))
        snprintf(out, out_len, "SYNC_RELAY_TRIM_CLAMP_SPS:%d", g_sync_relay_trim_clamp_sps);
    else if (!strcmp(param, "SYNC_COMPRESSION_DRAIN_FRAC"))
        snprintf(out, out_len, "SYNC_COMPRESSION_DRAIN_FRAC:%.3f",
                 (double)g_sync_compression_drain_frac);
    else if (!strcmp(param, "SYNC_COMPRESSION_DRAIN_BUDGET_MM"))
        snprintf(out, out_len, "SYNC_COMPRESSION_DRAIN_BUDGET_MM:%.3f",
                 (double)g_sync_compression_drain_budget_mm);
    else if (!strcmp(param, "SYNC_EST_ATTACK_ALPHA"))
        snprintf(out, out_len, "SYNC_EST_ATTACK_ALPHA:%.3f", (double)g_sync_est_attack_alpha);
    else if (!strcmp(param, "SYNC_TENSION_FAST_MM_S"))
        snprintf(out, out_len, "SYNC_TENSION_FAST_MM_S:%.3f", (double)g_sync_tension_fast_mm_s);
    else if (!strcmp(param, "SYNC_TENSION_PROBE_MAX"))
        snprintf(out, out_len, "SYNC_TENSION_PROBE_MAX:%.1f",
                 (double)sps_to_mm_per_min_idx(g_sync_tension_probe_max_sps, idx));
    else if (!strcmp(param, "SYNC_TENSION_PROBE_UP"))
        snprintf(out, out_len, "SYNC_TENSION_PROBE_UP:%.1f",
                 (double)sps_to_mm_per_min_idx(g_sync_tension_probe_up_sps_per_s, idx));
    else if (!strcmp(param, "SYNC_TENSION_PROBE_DOWN"))
        snprintf(out, out_len, "SYNC_TENSION_PROBE_DOWN:%.1f",
                 (double)sps_to_mm_per_min_idx(g_sync_tension_probe_down_sps_per_s, idx));
    else if (!strcmp(param, "SYNC_TENSION_PROBE_NEUTRAL"))
        snprintf(out, out_len, "SYNC_TENSION_PROBE_NEUTRAL:%.1f",
                 (double)sps_to_mm_per_min_idx(g_sync_tension_probe_neutral_sps_per_s, idx));
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "RELAY_MIN_FLIP_MM"))
        snprintf(out, out_len, "RELAY_MIN_FLIP_MM:%.3f", (double)g_relay_min_flip_mm);
    else if (!strcmp(param, "RELAY_COLLAPSE_DELAY_MS"))
        snprintf(out, out_len, "RELAY_COLLAPSE_DELAY_MS:%d", g_relay_collapse_delay_ms);
    else if (!strcmp(param, "RELAY_COLLAPSE_RAMP_MULT"))
        snprintf(out, out_len, "RELAY_COLLAPSE_RAMP_MULT:%d", g_relay_collapse_ramp_mult);
    else if (!strcmp(param, "RELAY_COLLAPSE_CAP_MS"))
        snprintf(out, out_len, "RELAY_COLLAPSE_CAP_MS:%d", g_relay_collapse_cap_ms);
    else if (!strcmp(param, "BUF_DRIFT_TAU_MS"))
        snprintf(out, out_len, "BUF_DRIFT_TAU_MS:%d", g_buf_drift_ewma_tau_ms);
    else if (!strcmp(param, "BUF_DRIFT_MIN_SMP"))
        snprintf(out, out_len, "BUF_DRIFT_MIN_SMP:%d", g_buf_drift_min_samples);
    else if (!strcmp(param, "BUF_DRIFT_THR_MM"))
        snprintf(out, out_len, "BUF_DRIFT_THR_MM:%.3f", (double)g_buf_drift_apply_thr_mm);
    else if (!strcmp(param, "BUF_DRIFT_CLAMP"))
        snprintf(out, out_len, "BUF_DRIFT_CLAMP:%.3f", (double)g_buf_drift_clamp_mm);
    else if (!strcmp(param, "BUF_DRIFT_MIN_CF"))
        snprintf(out, out_len, "BUF_DRIFT_MIN_CF:%.3f", (double)g_buf_drift_apply_min_cf);
    else if (!strcmp(param, "TENSION_RISK_WINDOW"))
        snprintf(out, out_len, "TENSION_RISK_WINDOW:%d", g_tension_risk_window_ms);
    else if (!strcmp(param, "TENSION_RISK_THR"))
        snprintf(out, out_len, "TENSION_RISK_THR:%d", g_tension_risk_threshold);
    else if (!strcmp(param, "TS_BUF_MS"))
        snprintf(out, out_len, "TS_BUF_MS:%d", g_ts_buf_fallback_ms);
    else if (!strcmp(param, "EST_ALPHA_MIN"))
        snprintf(out, out_len, "EST_ALPHA_MIN:%.3f", (double)g_est_alpha_min);
    else if (!strcmp(param, "EST_ALPHA_MAX"))
        snprintf(out, out_len, "EST_ALPHA_MAX:%.3f", (double)g_est_alpha_max);
    else if (!strcmp(param, "ZONE_BIAS_BASE"))
        snprintf(out, out_len, "ZONE_BIAS_BASE:%.1f",
                 (double)sps_to_mm_per_min(g_zone_bias_base_sps));
    else if (!strcmp(param, "ZONE_BIAS_RAMP"))
        snprintf(out, out_len, "ZONE_BIAS_RAMP:%.1f",
                 (double)sps_to_mm_per_min(g_zone_bias_ramp_sps_s));
    else if (!strcmp(param, "ZONE_BIAS_MAX"))
        snprintf(out, out_len, "ZONE_BIAS_MAX:%.1f", (double)sps_to_mm_per_min(g_zone_bias_max_sps));
#endif
    else
        return false;
    return true;
}

static bool cmd_get_buffer_params(const char *param, int idx, char *out, size_t out_len) {
    return cmd_get_buffer_geometry_params(param, idx, out, out_len) ||
           cmd_get_sync_control_params(param, idx, out, out_len) ||
           cmd_get_sync_relay_probe_params(param, idx, out, out_len);
}

static bool cmd_get_reload_cutter_params(const char *param, int idx, char *out, size_t out_len) {
    if (!strcmp(param, "LIVE_TUNE_LOCK"))
        snprintf(out, out_len, "LIVE_TUNE_LOCK:%d", g_live_tune_lock ? 1 : 0);
    else if (!strcmp(param, "AUTO_PRELOAD"))
        snprintf(out, out_len, "AUTO_PRELOAD:%d", g_auto_preload ? 1 : 0);
    else if (!strcmp(param, "CUTTER"))
        snprintf(out, out_len, "CUTTER:%d", g_enable_cutter ? 1 : 0);
    else if (!strcmp(param, "AUTO_MODE"))
        snprintf(out, out_len, "AUTO_MODE:%d", g_auto_mode);
    else if (!strcmp(param, "RELOAD_MODE"))
        snprintf(out, out_len, "RELOAD_MODE:%d", g_reload_mode);
    else if (!strcmp(param, "RUNOUT_COOLDOWN_MS"))
        snprintf(out, out_len, "RUNOUT_COOLDOWN_MS:%d", g_runout_cooldown_ms);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "POST_PRINT_STAB_MS"))
        snprintf(out, out_len, "POST_PRINT_STAB_MS:%d", g_post_print_stab_delay_ms);
    else if (!strcmp(param, "RELOAD_Y_MS"))
        snprintf(out, out_len, "RELOAD_Y_MS:%d", g_reload_y_timeout_ms);
#endif
    else if (!strcmp(param, "RELOAD_JOIN_MS"))
        snprintf(out, out_len, "RELOAD_JOIN_MS:%d", g_reload_join_delay_ms);
    else if (!strcmp(param, "DIST_IN_OUT"))
        snprintf(out, out_len, "DIST_IN_OUT:%d", g_dist_in_out);
    else if (!strcmp(param, "DIST_OUT_Y"))
        snprintf(out, out_len, "DIST_OUT_Y:%d", g_dist_out_y);
    else if (!strcmp(param, "DIST_Y_BUF"))
        snprintf(out, out_len, "DIST_Y_BUF:%d", g_dist_y_buf);
    else if (!strcmp(param, "BUF_BODY_LEN"))
        snprintf(out, out_len, "BUF_BODY_LEN:%d", g_buf_body_len);
    else if (!strcmp(param, "BUF_MAX_TRAVEL"))
        snprintf(out, out_len, "BUF_MAX_TRAVEL:%d", g_buf_max_travel_mm);
    else if (!strcmp(param, "JOIN_RATE"))
        snprintf(out, out_len, "JOIN_RATE:%.1f", (double)sps_to_mm_per_min_idx(g_join_sps, idx));
    else if (!strcmp(param, "PRESS_RATE"))
        snprintf(out, out_len, "PRESS_RATE:%.1f", (double)sps_to_mm_per_min_idx(g_press_sps, idx));
    else if (!strcmp(param, "COMPRESSION_RATE"))
        snprintf(out, out_len, "COMPRESSION_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(g_compression_sps, idx));
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "BUF_STAB_RATE"))
        snprintf(out, out_len, "BUF_STAB_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(g_buf_stab_sps, idx));
#endif
    else if (!strcmp(param, "FOLLOW_MS"))
        snprintf(out, out_len, "FOLLOW_MS:%d", g_follow_timeout_ms[idx]);
    else if (!strcmp(param, "UNLOAD_TENSION_BLOCK_MS"))
        snprintf(out, out_len, "UNLOAD_TENSION_BLOCK_MS:%d", g_unload_tension_block_ms);
    else if (!strcmp(param, "SERVO_OPEN"))
        snprintf(out, out_len, "SERVO_OPEN:%d", g_servo_open_us);
    else if (!strcmp(param, "SERVO_CLOSE"))
        snprintf(out, out_len, "SERVO_CLOSE:%d", g_servo_close_us);
    else if (!strcmp(param, "SERVO_BLOCK"))
        snprintf(out, out_len, "SERVO_BLOCK:%d", g_servo_block_us);
    else if (!strcmp(param, "SERVO_SETTLE") || !strcmp(param, "SERVO_SETTLE_MS"))
        snprintf(out, out_len, "%s:%d", param, g_servo_settle_ms);
    else if (!strcmp(param, "UNLOAD_CUT"))
        snprintf(out, out_len, "UNLOAD_CUT:%d", g_unload_cut ? 1 : 0);
    else if (!strcmp(param, "CUT_FEED_RATE"))
        snprintf(out, out_len, "CUT_FEED_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(g_cut_feed_sps, idx));
    else if (!strcmp(param, "CUT_FEED"))
        snprintf(out, out_len, "CUT_FEED:%d", g_cut_feed_mm);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "CUT_FEED_MS"))
        snprintf(out, out_len, "CUT_FEED_MS:%d", g_cut_timeout_feed_ms);
    else if (!strcmp(param, "CUT_SETTLE_MS"))
        snprintf(out, out_len, "CUT_SETTLE_MS:%d", g_cut_timeout_settle_ms);
#endif
    else if (!strcmp(param, "CUT_LEN"))
        snprintf(out, out_len, "CUT_LEN:%d", g_cut_length_mm);
    else if (!strcmp(param, "CUT_AMT"))
        snprintf(out, out_len, "CUT_AMT:%d", g_cut_amount);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "TC_CUT_MS"))
        snprintf(out, out_len, "TC_CUT_MS:%d", g_tc_timeout_cut_ms);
    else if (!strcmp(param, "TC_TH_MS"))
        snprintf(out, out_len, "TC_TH_MS:%d", g_tc_timeout_th_ms);
    else if (!strcmp(param, "TC_Y_MS"))
        snprintf(out, out_len, "TC_Y_MS:%d", g_tc_timeout_y_ms);
    else if (!strcmp(param, "RELOAD_LEAN"))
        snprintf(out, out_len, "RELOAD_LEAN:%.2f", (double)g_reload_lean_factor);
#endif
    else
        return false;
    return true;
}

static void cmd_handle_get(const char *p, uint32_t now_ms) {
    char out[CMD_PARAM_MAX];
    char param[CMD_PARAM_MAX];
    int lane_mask = 1;
    strncpy(param, p, sizeof(param));
    param[sizeof(param) - 1] = '\0';
    size_t len = strlen(param);
    if (len > 3 && !strcmp(param + len - 3, "_L1")) {
        lane_mask = 1;
        param[len - 3] = '\0';
    } else if (len > 3 && !strcmp(param + len - 3, "_L2")) {
        lane_mask = 2;
        param[len - 3] = '\0';
    }
    int idx = (lane_mask == 2) ? 1 : 0;

    bool handled = cmd_get_motion_params(param, idx, out, sizeof(out)) ||
                   cmd_get_tmc_params(param, idx, out, sizeof(out)) ||
                   cmd_get_buffer_params(param, idx, out, sizeof(out)) ||
                   cmd_get_reload_cutter_params(param, idx, out, sizeof(out));

    if (handled)
        cmd_reply("OK", out);
    else
        cmd_reply("ER", "GET:UNKNOWN_PARAM");
}

typedef enum { CMD_SET_UNHANDLED, CMD_SET_HANDLED, CMD_SET_REPLIED } cmd_set_result_t;

// ============================================================================
// SET handlers — apply/persist runtime tunables (grouped by domain)
// ============================================================================

static bool cmd_set_motion_params(const char *base_param, int iv, float fv) {
    if (!strcmp(base_param, "FEED_RATE"))
        g_feed_sps = motion_clamp_rate_sps(
            clamp_i(mm_per_min_to_sps(fv), MIN_RUN_RATE_SPS, MAX_RUN_RATE_SPS));
    else if (!strcmp(base_param, "REV_RATE"))
        g_rev_sps = motion_clamp_rate_sps(
            clamp_i(mm_per_min_to_sps(fv), MIN_RUN_RATE_SPS, MAX_RUN_RATE_SPS));
    else if (!strcmp(base_param, "AUTO_RATE"))
        g_auto_sps = motion_clamp_rate_sps(
            clamp_i(mm_per_min_to_sps(fv), MIN_RUN_RATE_SPS, MAX_RUN_RATE_SPS));
    else if (!strcmp(base_param, "SYNC_MAX_RATE"))
        g_sync_max_sps =
            sync_clamp_max_sps(clamp_i(mm_per_min_to_sps(fv), MIN_RUN_RATE_SPS, MAX_RUN_RATE_SPS));
    else if (!strcmp(base_param, "GLOBAL_MAX_RATE")) {
        g_global_max_sps = clamp_i(mm_per_min_to_sps(fv), mm_per_min_to_sps(GLOBAL_MAX_MIN_MM_MIN),
                                 mm_per_min_to_sps(GLOBAL_MAX_MAX_MM_MIN));
    } else if (!strcmp(base_param, "SYNC_MIN_RATE"))
        g_sync_min_sps = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 0, MAX_RUN_RATE_SPS));
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "SYNC_RAMP_ACCEL")) {
        float tick_s = (float)g_sync_tick_ms / MS_PER_SECOND_F;
        int sps = (int)(fv * tick_s / g_mm_per_step[0] + HALF_F);
        g_sync_ramp_up_sps = motion_clamp_rate_sps(clamp_i(sps, 1, MAX_RUN_RATE_SPS));
    } else if (!strcmp(base_param, "SYNC_RAMP_DECEL")) {
        float tick_s = (float)g_sync_tick_ms / MS_PER_SECOND_F;
        int sps = (int)(fv * tick_s / g_mm_per_step[0] + HALF_F);
        g_sync_ramp_dn_sps = motion_clamp_rate_sps(clamp_i(sps, 1, MAX_RUN_RATE_SPS));
    } else if (!strcmp(base_param, "SYNC_TICK_MS"))
        g_sync_tick_ms = clamp_i(iv, 1, 1000);
    else if (!strcmp(base_param, "GLOBAL_MAX_ACCEL")) {
        float tick_s = (float)g_ramp_tick_ms / MS_PER_SECOND_F;
        int sps = (int)(fv * tick_s / g_mm_per_step[0] + HALF_F);
        g_ramp_step_sps = motion_clamp_rate_sps(clamp_i(sps, 1, 30000));
    } else if (!strcmp(base_param, "RAMP_TICK_MS"))
        g_ramp_tick_ms = clamp_i(iv, 1, 1000);
    else if (!strcmp(base_param, "PRE_RAMP_RATE"))
        g_pre_ramp_sps = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 0, MAX_RUN_RATE_SPS));
#endif
    else if (!strcmp(base_param, "BUF_SWITCH_SPAN")) {
        g_buf_switch_span_half_mm = buf_switch_span_half_from_full(fv, g_buf_max_travel_mm);
    }
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "BUF_HYST"))
        g_buf_hyst_ms = clamp_i(iv, 5, 500);
    else if (!strcmp(base_param, "BUF_PREDICT_THR_MS"))
        g_buf_predict_thr_ms = clamp_i(iv, 0, RELOAD_JOIN_MAX_MS);
#endif
    else
        return false;
    return true;
}

static bool cmd_set_reload_motion_params(const char *base_param, int iv, float fv) {
    if (!strcmp(base_param, "AUTO_PRELOAD"))
        g_auto_preload = (iv != 0);
    else if (!strcmp(base_param, "RETRACT_MM"))
        g_autoload_retract_mm = clamp_i(iv, 0, AUTOLOAD_RETRACT_MAX_MM);
    else if (!strcmp(base_param, "CUTTER"))
        g_enable_cutter = (iv != 0);
    else if (!strcmp(base_param, "AUTO_MODE"))
        g_auto_mode = clamp_i(iv, 0, 1);
    else if (!strcmp(base_param, "RELOAD_MODE"))
        g_reload_mode = (iv != 0) ? 1 : 0;
    else if (!strcmp(base_param, "RUNOUT_COOLDOWN_MS"))
        g_runout_cooldown_ms = clamp_i(iv, 0, LONG_TIMEOUT_MAX_MS);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "POST_PRINT_STAB_MS"))
        g_post_print_stab_delay_ms = clamp_i(iv, 0, 300000);
    else if (!strcmp(base_param, "RELOAD_Y_MS"))
        g_reload_y_timeout_ms = (iv == 0) ? 0 : clamp_i(iv, 100, 30000);
#endif
    else if (!strcmp(base_param, "RELOAD_JOIN_MS"))
        g_reload_join_delay_ms = clamp_i(iv, 0, RELOAD_JOIN_MAX_MS);
    else if (!strcmp(base_param, "DIST_IN_OUT"))
        g_dist_in_out = clamp_i(iv, PATH_DIST_MIN_MM, PATH_DIST_MAX_MM);
    else if (!strcmp(base_param, "DIST_OUT_Y"))
        g_dist_out_y = clamp_i(iv, 0, PATH_DIST_MAX_MM);
    else if (!strcmp(base_param, "DIST_Y_BUF"))
        g_dist_y_buf = clamp_i(iv, 0, PATH_DIST_MAX_MM);
    else if (!strcmp(base_param, "BUF_BODY_LEN"))
        g_buf_body_len = clamp_i(iv, 0, PATH_DIST_MAX_MM);
    else if (!strcmp(base_param, "BUF_MAX_TRAVEL")) {
        g_buf_max_travel_mm = clamp_i(iv, BUF_TRAVEL_MIN_MM, BUF_TRAVEL_MAX_MM);
        float max_half = (float)g_buf_max_travel_mm * HALF_F;
        if (g_buf_switch_span_half_mm > max_half)
            g_buf_switch_span_half_mm = max_half;
        if (g_buf_switch_span_half_mm < BUF_SWITCH_SPAN_HALF_MIN_MM)
            g_buf_switch_span_half_mm = BUF_SWITCH_SPAN_HALF_MIN_MM;
    } else if (!strcmp(base_param, "JOIN_RATE"))
        g_join_sps = motion_clamp_rate_sps(
            clamp_i(mm_per_min_to_sps(fv), MIN_RUN_RATE_SPS, MAX_RUN_RATE_SPS));
    else if (!strcmp(base_param, "PRESS_RATE"))
        g_press_sps = motion_clamp_rate_sps(
            clamp_i(mm_per_min_to_sps(fv), MIN_RUN_RATE_SPS, MAX_RUN_RATE_SPS));
    else if (!strcmp(base_param, "COMPRESSION_RATE"))
        g_compression_sps = motion_clamp_rate_sps(
            clamp_i(mm_per_min_to_sps(fv), MIN_LOW_RATE_SPS, MAX_LOW_RATE_SPS));
    else if (!strcmp(base_param, "BUF_STAB_RATE"))
        g_buf_stab_sps = motion_clamp_rate_sps(
            clamp_i(mm_per_min_to_sps(fv), MIN_LOW_RATE_SPS, MAX_LOW_RATE_SPS));
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "BASELINE_ALPHA"))
        g_baseline_alpha = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "EST_ALPHA_MIN"))
        g_est_alpha_min = clamp_f(fv, 0.01f, 1.0f);
    else if (!strcmp(base_param, "EST_ALPHA_MAX"))
        g_est_alpha_max = clamp_f(fv, 0.01f, 1.0f);
    else if (!strcmp(base_param, "ZONE_BIAS_BASE"))
        g_zone_bias_base_sps = clamp_i(mm_per_min_to_sps(fv), 0, 5000);
    else if (!strcmp(base_param, "ZONE_BIAS_RAMP"))
        g_zone_bias_ramp_sps_s = clamp_i(mm_per_min_to_sps(fv), 0, 5000);
    else if (!strcmp(base_param, "ZONE_BIAS_MAX"))
        g_zone_bias_max_sps = clamp_i(mm_per_min_to_sps(fv), 0, 5000);
    else if (!strcmp(base_param, "RELOAD_LEAN"))
        g_reload_lean_factor = clamp_f(fv, 0.0f, 5.0f);
#endif
    else
        return false;
    return true;
}

static bool cmd_set_lane_params(const char *base_param, int lane_mask, int iv, float fv) {
#define SET_LANE(BLOCK)                                                                            \
    for (int l = 1; l <= NUM_LANES; l++)                                                           \
        if (lane_mask & (1 << (l - 1))) {                                                          \
            int idx = l - 1;                                                                       \
            BLOCK;                                                                                 \
            sync_tmc_settings(l);                                                                  \
        }

    if (!strcmp(base_param, "RUN_CURRENT_MA")) {
        SET_LANE({ g_tmc_run_current_ma[idx] = clamp_i(iv, 0, DRIVER_CURRENT_MAX_MA); });
    } else if (!strcmp(base_param, "HOLD_CURRENT_MA")) {
        SET_LANE({ g_tmc_hold_current_ma[idx] = clamp_i(iv, 0, DRIVER_CURRENT_MAX_MA); });
    } else if (!strcmp(base_param, "MICROSTEPS")) {
        if (iv < 1 || iv > DRIVER_MICROSTEPS_MAX || (iv & (iv - 1)) != 0) {
            return false;
        }
        SET_LANE({ g_tmc_microsteps[idx] = iv; });
    } else if (!strcmp(base_param, "ROTATION_DIST")) {
        SET_LANE({
            g_tmc_rotation_distance[idx] = clamp_f(fv, TMC_ROTATION_MIN_MM, TMC_ROTATION_MAX_MM);
        });
    } else if (!strcmp(base_param, "GEAR_RATIO")) {
        SET_LANE({ g_tmc_gear_ratio[idx] = clamp_f(fv, TMC_GEAR_RATIO_MIN, TMC_GEAR_RATIO_MAX); });
    } else if (!strcmp(base_param, "FULL_STEPS")) {
        SET_LANE({
            g_tmc_full_steps[idx] =
                (iv == DRIVER_FULL_STEPS_400 ? DRIVER_FULL_STEPS_400 : DRIVER_FULL_STEPS_200);
        });
    } else if (!strcmp(base_param, "INTERPOLATE")) {
        SET_LANE({ g_tmc_interpolate[idx] = (iv != 0); });
    } else if (!strcmp(base_param, "STEALTHCHOP")) {
        SET_LANE({ g_tmc_stealthchop_sps[idx] = (iv == 0) ? 0 : mm_per_min_to_sps_idx(fv, idx); });
    } else if (!strcmp(base_param, "DRIVER_TBL")) {
        SET_LANE({ g_tmc_tbl[idx] = clamp_i(iv, 0, DRIVER_TBL_MAX); });
    } else if (!strcmp(base_param, "DRIVER_TOFF")) {
        SET_LANE({ g_tmc_toff[idx] = clamp_i(iv, 0, DRIVER_TOFF_MAX); });
    } else if (!strcmp(base_param, "DRIVER_HSTRT")) {
        SET_LANE({ g_tmc_hstrt[idx] = clamp_i(iv, 0, DRIVER_HSTRT_MAX); });
    } else if (!strcmp(base_param, "DRIVER_HEND")) {
        SET_LANE({ g_tmc_hend[idx] = clamp_i(iv, DRIVER_HEND_MIN, DRIVER_HEND_MAX); });
    } else if (!strcmp(base_param, "FOLLOW_MS")) {
        SET_LANE({
            g_follow_timeout_ms[idx] = clamp_i(iv, FOLLOW_TIMEOUT_MIN_MS, FOLLOW_TIMEOUT_MAX_MS);
        });
    } else {
#undef SET_LANE
        return false;
    }

#undef SET_LANE
    return true;
}

static cmd_set_result_t cmd_set_buffer_params(const char *base_param, int iv, float fv) {
    if (!strcmp(base_param, "BASELINE_RATE")) {
        int baseline_sps = motion_clamp_rate_sps(
            clamp_i(mm_per_min_to_sps(fv), MIN_RUN_RATE_SPS, MAX_RUN_RATE_SPS));
        g_baseline_target_sps = baseline_sps;
        g_baseline_sps = baseline_sps;
        flow_schedule_refresh_scalar();
    } else if (!strcmp(base_param, "BASELINE_SPS")) {
        int baseline_sps = motion_clamp_rate_sps(clamp_i(iv, MIN_RUN_RATE_SPS, MAX_RUN_RATE_SPS));
        g_baseline_target_sps = baseline_sps;
        g_baseline_sps = baseline_sps;
        flow_schedule_refresh_scalar();
    } else if (!strcmp(base_param, "BUF_SENSOR")) {
        if (sync_enabled || tc_state() != TC_IDLE || g_lane_l1.task != TASK_IDLE ||
            g_lane_l2.task != TASK_IDLE) {
            cmd_reply("ER", "BUSY:LANE");
            return CMD_SET_REPLIED;
        }
        g_buf_sensor_type = clamp_i(iv, 0, 1);
        sync_disable(false);
    } else if (!strcmp(base_param, "BUF_PSF_MAX_COMP"))
        g_buf_psf_max_comp = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "BUF_PSF_MAX_TENS"))
        g_buf_psf_max_tens = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "BUF_PSF_NEUTRAL"))
        g_buf_psf_neutral = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "BUF_GOAL"))
        g_buf_goal = clamp_f(fv, 0.0f, 1.0f);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "BUF_ALPHA"))
        g_buf_analog_alpha = clamp_f(fv, 0.01f, 1.0f);
#endif
    else if (!strcmp(base_param, "AUTOLOAD_MAX"))
        g_autoload_max_mm = clamp_i(iv, AUTOLOAD_MAX_MIN_MM, AUTOLOAD_MAX_MAX_MM);
    else if (!strcmp(base_param, "LOAD_MAX"))
        g_load_max_mm = clamp_i(iv, LOAD_UNLOAD_MIN_MM, LOAD_UNLOAD_MAX_MM);
    else if (!strcmp(base_param, "UNLOAD_MAX"))
        g_unload_max_mm = clamp_i(iv, LOAD_UNLOAD_MIN_MM, LOAD_UNLOAD_MAX_MM);
    else if (!strcmp(base_param, "UNLOAD_TENSION_BLOCK_MS"))
        g_unload_tension_block_ms = clamp_i(iv, 0, LONG_TIMEOUT_MAX_MS);
    else if (!strcmp(base_param, "SYNC_KP_RATE"))
        g_sync_kp_sps = clamp_i(mm_per_min_to_sps(fv), 0, MAX_RUN_RATE_SPS);
    else if (!strcmp(base_param, "KD_PSF"))
        g_kd_psf = clamp_f(fv, 0.0f, PSF_KD_MAX);
    else if (!strcmp(base_param, "SYNC_PSF_SLEW_PER_MM"))
        g_sync_psf_slew_per_mm = clamp_f(fv, 1.0f, SYNC_PSF_SLEW_MAX);
    else if (!strcmp(base_param, "SYNC_PSF_FILTER_MM"))
        g_sync_psf_filter_mm = clamp_f(fv, SYNC_PSF_FILTER_MIN_MM, SYNC_PSF_FILTER_MAX_MM);
    else if (!strcmp(base_param, "SYNC_PSF_DECAY_SPS_PER_S"))
        g_sync_psf_decay_sps_per_s = clamp_f(fv, 0.0f, SYNC_PSF_DECAY_MAX_SPS_PER_S);
    else if (!strcmp(base_param, "PSF_STAB_STAGNANT_MS"))
        g_psf_stab_stagnant_ms = (iv < 0) ? 0 : iv;
    else if (!strcmp(base_param, "PSF_STAB_STAGNANT_NORM"))
        g_psf_stab_stagnant_norm = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "PSF_STAB_RAIL_BREAK_MS"))
        g_psf_stab_rail_break_ms = (iv < 0) ? 0 : iv;
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "SYNC_OVERSHOOT_PCT"))
        g_sync_overshoot_pct = clamp_i(iv, 0, 200);
#endif
    else if (!strcmp(base_param, "SYNC_RESERVE_PCT"))
        g_sync_reserve_pct = clamp_i(iv, 0, SYNC_RESERVE_MAX_PCT);
    else if (!strcmp(base_param, "COMPRESSION_BIAS_FRAC")) {
        g_sync_compression_bias_frac = clamp_f(fv, 0.0f, COMPRESSION_BIAS_MAX_FRAC);
        flow_schedule_refresh_scalar();
    } else if (!strcmp(base_param, "SYNC_AUTO_STOP"))
        g_sync_auto_stop_ms = clamp_i(iv, 0, SYNC_AUTO_STOP_MAX_MS);
    else
        return CMD_SET_UNHANDLED;
    return CMD_SET_HANDLED;
}

static bool cmd_set_sync_advanced_params(const char *base_param, int iv, float fv) {
#ifdef FLARE_DEV_TUNING
    if (!strcmp(base_param, "NEUTRAL_CREEP_TIMEOUT_MS"))
        g_neutral_creep_timeout_ms = clamp_i(iv, 0, LONG_TIMEOUT_MAX_MS);
    else if (!strcmp(base_param, "NEUTRAL_CREEP_RATE") ||
             !strcmp(base_param, "NEUTRAL_CREEP_RATE_SPS_PER_S"))
        g_neutral_creep_rate_sps_per_s = clamp_i(iv, 0, 1000);
    else if (!strcmp(base_param, "NEUTRAL_CREEP_CAP") ||
             !strcmp(base_param, "NEUTRAL_CREEP_CAP_FRAC"))
        g_neutral_creep_cap_frac = clamp_i(iv, 0, 100);
    else if (!strcmp(base_param, "VAR_BLEND_FRAC") ||
             !strcmp(base_param, "BUF_VARIANCE_BLEND_FRAC"))
        g_buf_variance_blend_frac = clamp_f(fv, 0.0f, 0.9f);
    else if (!strcmp(base_param, "VAR_BLEND_REF_MM") ||
             !strcmp(base_param, "BUF_VARIANCE_BLEND_REF_MM"))
        g_buf_variance_blend_ref_mm = clamp_f(fv, HALF_F, 5.0f);
    else if (!strcmp(base_param, "SYNC_TENSION_STOP_MS"))
        g_sync_tension_dwell_stop_ms = clamp_i(iv, 0, 30000);
    else if (!strcmp(base_param, "SYNC_TENSION_RAMP_MS"))
        g_sync_tension_ramp_delay_ms = clamp_i(iv, 0, LONG_TIMEOUT_MAX_MS);
    else if (!strcmp(base_param, "SYNC_OVERSHOOT_NEUTRAL_EXT"))
        g_sync_overshoot_neutral_extend = clamp_i(iv, 0, 1);
    else if (!strcmp(base_param, "SYNC_INT_GAIN"))
        g_sync_reserve_integral_gain = clamp_f(fv, 0.0f, 0.05f);
    else if (!strcmp(base_param, "SYNC_INT_CLAMP"))
        g_sync_reserve_integral_clamp_mm = clamp_f(fv, 0.0f, BUF_SWITCH_SPAN_MIN_MM);
    else if (!strcmp(base_param, "SYNC_INT_DECAY_MS"))
        g_sync_reserve_integral_decay_ms = clamp_i(iv, 0, LONG_TIMEOUT_MAX_MS);
    else if (!strcmp(base_param, "EST_SIGMA_CAP"))
        g_est_sigma_hard_cap_mm = clamp_f(fv, HALF_F, 5.0f);
    else if (!strcmp(base_param, "EST_LOW_CF_THR"))
        g_est_low_cf_warn_threshold = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "EST_FALLBACK_THR"))
        g_est_fallback_cf_threshold = clamp_f(fv, 0.0f, HALF_F);
    else
#endif
        return false;
    return true;
}

static bool cmd_set_sync_relay_probe_params(const char *base_param, int iv, float fv) {
    if (!strcmp(base_param, "RELAY_CATCHUP_FRAC"))
        g_relay_catchup_frac = clamp_f(fv, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    else if (!strcmp(base_param, "RELAY_NEUTRAL_FRAC"))
        g_relay_neutral_frac = clamp_f(fv, RELAY_FRAC_MIN, RELAY_FRAC_MAX);
    else if (!strcmp(base_param, "SYNC_RELAY_TRIM_STEP_SPS"))
        g_sync_relay_trim_step_sps = clamp_i(iv, 0, RELAY_TRIM_STEP_MAX_SPS);
    else if (!strcmp(base_param, "SYNC_RELAY_TRIM_CLAMP_SPS"))
        g_sync_relay_trim_clamp_sps = clamp_i(iv, 0, RELAY_TRIM_CLAMP_MAX_SPS);
    else if (!strcmp(base_param, "SYNC_COMPRESSION_DRAIN_FRAC"))
        g_sync_compression_drain_frac = clamp_f(fv, 0.0f, COMPRESSION_DRAIN_MAX_FRAC);
    else if (!strcmp(base_param, "SYNC_COMPRESSION_DRAIN_BUDGET_MM"))
        g_sync_compression_drain_budget_mm = clamp_f(fv, 0.0f, COMPRESSION_DRAIN_BUDGET_MAX_MM);
    else if (!strcmp(base_param, "SYNC_EST_ATTACK_ALPHA"))
        g_sync_est_attack_alpha = clamp_f(fv, SYNC_EST_ATTACK_MIN_ALPHA, 1.0f);
    else if (!strcmp(base_param, "SYNC_TENSION_FAST_MM_S"))
        g_sync_tension_fast_mm_s = clamp_f(fv, 1.0f, SYNC_TENSION_FAST_MAX_MM_S);
    else if (!strcmp(base_param, "SYNC_TENSION_PROBE_MAX"))
        g_sync_tension_probe_max_sps =
            clamp_i(mm_per_min_to_sps(fv), 0, mm_per_min_to_sps(TENSION_PROBE_MAX_MM_MIN));
    else if (!strcmp(base_param, "SYNC_TENSION_PROBE_UP"))
        g_sync_tension_probe_up_sps_per_s =
            clamp_i(mm_per_min_to_sps(fv), 0, mm_per_min_to_sps(GLOBAL_MAX_MAX_MM_MIN));
    else if (!strcmp(base_param, "SYNC_TENSION_PROBE_DOWN"))
        g_sync_tension_probe_down_sps_per_s =
            clamp_i(mm_per_min_to_sps(fv), 0, mm_per_min_to_sps(GLOBAL_MAX_MAX_MM_MIN));
    else if (!strcmp(base_param, "SYNC_TENSION_PROBE_NEUTRAL"))
        g_sync_tension_probe_neutral_sps_per_s =
            clamp_i(mm_per_min_to_sps(fv), 0, mm_per_min_to_sps(GLOBAL_MAX_MAX_MM_MIN));
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "RELAY_MIN_FLIP_MM"))
        g_relay_min_flip_mm = clamp_f(fv, 0.0f, 100.0f);
    else if (!strcmp(base_param, "RELAY_COLLAPSE_DELAY_MS"))
        g_relay_collapse_delay_ms = clamp_i(iv, 0, LONG_TIMEOUT_MAX_MS);
    else if (!strcmp(base_param, "RELAY_COLLAPSE_RAMP_MULT"))
        g_relay_collapse_ramp_mult = clamp_i(iv, 1, 16);
    else if (!strcmp(base_param, "RELAY_COLLAPSE_CAP_MS"))
        g_relay_collapse_cap_ms = clamp_i(iv, 0, LONG_TIMEOUT_MAX_MS);
    else if (!strcmp(base_param, "BUF_DRIFT_TAU_MS"))
        g_buf_drift_ewma_tau_ms = clamp_i(iv, 5000, 600000);
    else if (!strcmp(base_param, "BUF_DRIFT_MIN_SMP"))
        g_buf_drift_min_samples = clamp_i(iv, 1, 32);
    else if (!strcmp(base_param, "BUF_DRIFT_THR_MM"))
        g_buf_drift_apply_thr_mm = clamp_f(fv, 0.0f, 5.0f);
    else if (!strcmp(base_param, "BUF_DRIFT_CLAMP"))
        g_buf_drift_clamp_mm = clamp_f(fv, 0.0f, BUF_DRIFT_CLAMP_LIMIT_MM);
    else if (!strcmp(base_param, "BUF_DRIFT_MIN_CF"))
        g_buf_drift_apply_min_cf = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "TENSION_RISK_WINDOW"))
        g_tension_risk_window_ms = clamp_i(iv, 5000, 300000);
    else if (!strcmp(base_param, "TENSION_RISK_THR"))
        g_tension_risk_threshold = clamp_i(iv, 0, 1000);
    else if (!strcmp(base_param, "TS_BUF_MS"))
        g_ts_buf_fallback_ms = clamp_i(iv, 0, 30000);
    else if (!strcmp(base_param, "STARTUP_MS"))
        g_motion_startup_ms = clamp_i(iv, 0, 30000);
#endif
    else
        return false;
    return true;
}

static bool cmd_set_cutter_params(const char *base_param, int iv, float fv) {
    if (!strcmp(base_param, "SERVO_OPEN"))
        g_servo_open_us = clamp_i(iv, SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
    else if (!strcmp(base_param, "SERVO_CLOSE"))
        g_servo_close_us = clamp_i(iv, SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
    else if (!strcmp(base_param, "SERVO_BLOCK"))
        g_servo_block_us = clamp_i(iv, SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
    else if (!strcmp(base_param, "SERVO_SETTLE") || !strcmp(base_param, "SERVO_SETTLE_MS"))
        g_servo_settle_ms = clamp_i(iv, SERVO_SETTLE_MIN_MS, SERVO_SETTLE_MAX_MS);
    else if (!strcmp(base_param, "UNLOAD_CUT"))
        g_unload_cut = (iv == 1);
    else if (!strcmp(base_param, "CUT_FEED_RATE"))
        g_cut_feed_sps = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv),
                                                     mm_per_min_to_sps(CUT_FEED_MIN_MM_MIN),
                                                     mm_per_min_to_sps(CUT_FEED_MAX_MM_MIN)));
    else if (!strcmp(base_param, "CUT_FEED"))
        g_cut_feed_mm = clamp_i(iv, 1, CUT_FEED_MAX_MM);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "CUT_FEED_MS"))
        g_cut_timeout_feed_ms = clamp_i(iv, 1000, 120000);
    else if (!strcmp(base_param, "CUT_SETTLE_MS"))
        g_cut_timeout_settle_ms = clamp_i(iv, 500, 10000);
#endif
    else if (!strcmp(base_param, "CUT_LEN"))
        g_cut_length_mm = clamp_i(iv, 1, CUT_LENGTH_MAX_MM);
    else if (!strcmp(base_param, "CUT_AMT"))
        g_cut_amount = clamp_i(iv, 1, CUT_AMOUNT_MAX);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "TC_CUT_MS"))
        g_tc_timeout_cut_ms = clamp_i(iv, 1000, 30000);
    else if (!strcmp(base_param, "TC_TH_MS"))
        g_tc_timeout_th_ms = clamp_i(iv, 0, RELOAD_JOIN_MAX_MS);
    else if (!strcmp(base_param, "TC_Y_MS"))
        g_tc_timeout_y_ms = clamp_i(iv, 0, 30000);
#endif
    else
        return false;
    return true;
}

static cmd_set_result_t cmd_apply_set_param(const char *base_param, int lane_mask, int iv,
                                            float fv) {
    if (cmd_set_motion_params(base_param, iv, fv) ||
        cmd_set_reload_motion_params(base_param, iv, fv) ||
        cmd_set_lane_params(base_param, lane_mask, iv, fv)) {
        return CMD_SET_HANDLED;
    }

    cmd_set_result_t buffer_result = cmd_set_buffer_params(base_param, iv, fv);
    if (buffer_result != CMD_SET_UNHANDLED) {
        return buffer_result;
    }

    if (cmd_set_sync_advanced_params(base_param, iv, fv) ||
        cmd_set_sync_relay_probe_params(base_param, iv, fv) ||
        cmd_set_cutter_params(base_param, iv, fv)) {
        return CMD_SET_HANDLED;
    }
    return CMD_SET_UNHANDLED;
}

static void cmd_handle_set(const char *p, uint32_t now_ms) {
    char param[CMD_PARAM_MAX];
    char val_str[CMD_VALUE_MAX];
    if (sscanf(p, CMD_PARAM_SCAN_FMT ":" CMD_VALUE_SCAN_FMT, param, val_str) != 2) {
        cmd_reply("ER", "SET:ARG");
        return;
    }
    int iv = atoi(val_str);
    float fv = (float)atof(val_str);
    int lane_mask = 3;
    char base_param[CMD_PARAM_MAX];
    strncpy(base_param, param, sizeof(base_param));
    base_param[sizeof(base_param) - 1] = '\0';
    size_t len = strlen(param);
    if (len > 3 && !strcmp(param + len - 3, "_L1")) {
        lane_mask = 1;
        base_param[len - 3] = '\0';
    } else if (len > 3 && !strcmp(param + len - 3, "_L2")) {
        lane_mask = 2;
        base_param[len - 3] = '\0';
    }

    if (!strcmp(base_param, "LIVE_TUNE_LOCK")) {
        g_live_tune_lock = (iv != 0);
        cmd_reply("OK", "LIVE_TUNE_LOCK");
        return;
    }
    if (g_live_tune_lock && live_tune_locked_param(base_param)) {
        cmd_reply("ER", "LIVE_TUNE_LOCKED");
        return;
    }

    cmd_set_result_t result = cmd_apply_set_param(base_param, lane_mask, iv, fv);
    if (result == CMD_SET_REPLIED) {
        return;
    }
    if (result == CMD_SET_HANDLED) {
        motion_limit_runtime_rates(true);
        cmd_reply("OK", NULL);
    } else
        cmd_reply("ER", "SET:UNKNOWN_PARAM");
}

// ============================================================================
// Command handlers & dispatch — cutter, unload, load, motion, sensor, system
// ============================================================================

static bool cmd_handle_cutter(const char *cmd, const char *p, uint32_t now_ms) {
    if (!strcmp(cmd, "CU")) {
        if (!g_enable_cutter) {
            cmd_reply("ER", "CUTTER_DISABLED");
            return true;
        }
        lane_t *lane = get_active_lane_and_clear_error();
        if (!lane)
            return true;
        lane_t *other = (lane == &g_lane_l1) ? &g_lane_l2 : &g_lane_l1;
        if (!lane_in_present(lane)) {
            cmd_reply("ER", "NO_FILAMENT");
            return true;
        }
        if (lane_out_present(other)) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return true;
        }
        if (lane->task != TASK_IDLE || g_tc_ctx.state != TC_IDLE) {
            cmd_reply("ER", "BUSY:CUTTER");
            return true;
        }
        sync_retract_assist_set(false);
        cutter_start(lane, true, now_ms);
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "CX")) {
        if (!g_enable_cutter) {
            cmd_reply("ER", "CUTTER_DISABLED");
            return true;
        }
        sync_retract_assist_set(false);
        cutter_start(NULL, false, now_ms);
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "CP")) {
        if (!g_enable_cutter) {
            cmd_reply("ER", "CUTTER_DISABLED");
            return true;
        }
        int us = atoi(p);
        if (us < SERVO_MIN_PULSE_US || us > SERVO_MAX_PULSE_US) {
            cmd_reply("ER", "ARG");
            return true;
        }
        sync_retract_assist_set(false);
        if (!cutter_test_us(us)) {
            cmd_reply("ER", "BUSY:CUTTER");
        } else {
            cmd_reply("OK", NULL);
        }
        return true;
    }
    return false;
}

static bool cmd_handle_unload(const char *cmd, const char *p, uint32_t now_ms) {
    if (!strcmp(cmd, "UL")) {
        if (tc_busy()) {
            cmd_reply("ER", "BUSY:UNLOAD");
            return true;
        }
        lane_t *lane = get_active_lane_and_clear_error();
        if (!lane)
            return true;
        if (!lane_out_present(lane)) {
            cmd_reply("ER", "NOT_LOADED");
            return true;
        }
        sync_retract_assist_set(false);
        sync_bl_clear_autostart_suppress();
        sync_set_state(SYNC_OFF);
        sync_disable(false);
        set_toolhead_filament(false);
        if (g_enable_cutter && g_unload_cut) {
            g_manual_unload.lane = lane;
            g_manual_unload.finish_to_in = false;
            g_manual_unload.cut_pending = true;
            g_manual_unload.state = MANUAL_UNLOAD_WAIT_FIRST_CLEAR;
            start_manual_unload_lane(lane, true, now_ms);
        } else {
            start_manual_unload_lane(lane, false, now_ms);
        }
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "UM")) {
        int target_lane = 0;
        bool explicit_lane = false;
        if (!parse_optional_lane_payload(p, &target_lane, &explicit_lane)) {
            cmd_reply("ER", "ARG");
            return true;
        }

        bool active_target = !explicit_lane || target_lane == g_active_lane;
        if (active_target && tc_busy()) {
            cmd_reply("ER", "BUSY:UNLOAD");
            return true;
        }
        lane_t *lane = active_target ? get_active_lane_and_clear_error() : lane_ptr(target_lane);
        if (!lane) {
            cmd_reply("ER", "ARG");
            return true;
        }

        if (active_target) {
            if (!lane_in_present(lane)) {
                cmd_reply("ER", "NOT_LOADED");
                return true;
            }
            sync_retract_assist_set(false);
            sync_bl_clear_autostart_suppress();
            sync_set_state(SYNC_OFF);
            sync_disable(false);
            set_toolhead_filament(false);
            bool out_present_at_entry = lane_out_present(lane);
            if (out_present_at_entry || on_al(&g_y_split)) {
                if (g_enable_cutter && g_unload_cut && out_present_at_entry) {
                    g_manual_unload.lane = lane;
                    g_manual_unload.finish_to_in = true;
                    g_manual_unload.cut_pending = true;
                    g_manual_unload.state = MANUAL_UNLOAD_WAIT_FIRST_CLEAR;
                    start_manual_unload_lane(lane, true, now_ms);
                } else {
                    start_manual_unload_lane(lane, true, now_ms);
                    g_manual_unload.lane = lane;
                    g_manual_unload.finish_to_in = true;
                    g_manual_unload.cut_pending = false;
                    g_manual_unload.state = MANUAL_UNLOAD_WAIT_FIRST_CLEAR;
                }
            } else {
                start_manual_unload_to_in(lane, now_ms);
            }
        } else {
            if (g_tc_ctx.state != TC_IDLE || lane->task != TASK_IDLE) {
                cmd_reply("ER", "BUSY:UNLOAD");
                return true;
            }
            if (!lane_in_present(lane)) {
                cmd_reply("ER", "NOT_LOADED");
                return true;
            }
            if (lane_out_present(lane)) {
                cmd_reply("ER", "NOT_PRELOADED");
                return true;
            }
            start_manual_unload_to_in(lane, now_ms);
        }
        cmd_reply("OK", NULL);
        return true;
    }
    return false;
}

static bool cmd_handle_mv_command(const char *p, uint32_t now_ms) {
    lane_t *lane = get_active_lane_and_clear_error();
    if (!lane)
        return true;
    float mm = 0.0f;
    float feed_mm_min = 0.0f;
    char dir_tok[CMD_OPTION_TOKEN_MAX] = {0};
    char ignore_tok[CMD_OPTION_TOKEN_MAX] = {0};
    int n = sscanf(p, "%f:%f:" CMD_OPTION_TOKEN_SCAN_FMT, &mm, &feed_mm_min, dir_tok, ignore_tok);
    if ((n < 2 || n > 4) || feed_mm_min <= 0.0f) {
        cmd_reply("ER", "ARG");
        return true;
    }
    int idx = lane_to_idx(g_active_lane);
    int sps = (int)(feed_mm_min / SECONDS_PER_MINUTE_F / g_mm_per_step[idx] + HALF_F);
    if (sps < MIN_RUN_RATE_SPS)
        sps = MIN_RUN_RATE_SPS;
    sps = motion_clamp_rate_sps(sps);

    bool forward = (mm >= 0.0f);
    bool ignore_buffer = false;

    if ((n >= 3 && !parse_mv_option(dir_tok, &forward, &ignore_buffer)) ||
        (n == 4 && !parse_mv_option(ignore_tok, &forward, &ignore_buffer))) {
        cmd_reply("ER", "ARG");
        return true;
    }

    float limit = mm < 0.0f ? -mm : mm;
    if (limit <= 0.0f) {
        cmd_reply("ER", "ARG");
        return true;
    }

    buffer_stabilize_cancel();
    sync_retract_assist_set(false);
    sync_set_state(SYNC_OFF);
    sync_disable(false);
    lane_start(lane, TASK_MOVE, sps, forward, now_ms, limit);
    lane->move_ignore_buffer = ignore_buffer;
    cmd_reply("OK", NULL);
    return true;
}

static bool cmd_handle_load_commands(const char *cmd, const char *p, uint32_t now_ms) {
    if (!strcmp(cmd, "LO")) {
        if (tc_busy()) {
            cmd_reply("ER", "BUSY:LANE");
            return true;
        }
        lane_t *lane = get_active_lane_and_clear_error();
        if (!lane)
            return true;
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        lane_start(lane, TASK_AUTOLOAD, g_auto_sps, true, now_ms, (float)g_autoload_max_mm);
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "FL")) {
        if (tc_busy()) {
            cmd_reply("ER", "BUSY:LANE");
            return true;
        }
        lane_t *lane = get_active_lane_and_clear_error();
        if (!lane)
            return true;
        if (!lane_in_present(lane)) {
            cmd_reply("ER", "NO_FILAMENT");
            return true;
        }
        if (on_al(&g_y_split) && !lane_out_present(lane)) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return true;
        }
        lane_t *other = lane_ptr(other_lane(g_active_lane));
        if (other && lane_out_present(other) && other->task == TASK_IDLE) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return true;
        }
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        set_toolhead_filament(false);
        lane_start(lane, TASK_LOAD_FULL, g_feed_sps, true, now_ms, (float)g_load_max_mm);
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "RL")) {
        if (tc_busy()) {
            cmd_reply("ER", "BUSY:LANE");
            return true;
        }
        lane_t *lane = get_active_lane_and_clear_error();
        if (!lane)
            return true;
        if (!lane_in_present(lane)) {
            cmd_reply("ER", "NO_FILAMENT");
            return true;
        }
        if (on_al(&g_y_split) && !lane_out_present(lane)) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return true;
        }
        lane_t *other = lane_ptr(other_lane(g_active_lane));
        if (other && lane_out_present(other) && other->task == TASK_IDLE) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return true;
        }
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        tc_manual_reload(now_ms);
        cmd_reply("OK", NULL);
        return true;
    }
    return false;
}

static bool cmd_handle_motion(const char *cmd, const char *p, uint32_t now_ms) {
    if (!strcmp(cmd, "UL") || !strcmp(cmd, "UM")) {
        return cmd_handle_unload(cmd, p, now_ms);
    }
    if (!strcmp(cmd, "CU") || !strcmp(cmd, "CX") || !strcmp(cmd, "CP")) {
        return cmd_handle_cutter(cmd, p, now_ms);
    }
    if (!strcmp(cmd, "LO") || !strcmp(cmd, "FL") || !strcmp(cmd, "RL")) {
        return cmd_handle_load_commands(cmd, p, now_ms);
    }
    if (!strcmp(cmd, "MV")) {
        return cmd_handle_mv_command(p, now_ms);
    }

    if (!strcmp(cmd, "TC")) {
        if (tc_busy()) {
            cmd_reply("ER", "BUSY:TC");
            return true;
        }
        int ln = atoi(p);
        if (ln == 1 || ln == 2) {
            if (g_active_lane != 1 && g_active_lane != 2) {
                cmd_reply("ER", "NO_ACTIVE_LANE");
                return true;
            }
            /* Double-load guard: if both OUT sensors are triggered the hub is
               stuck with two filaments.  Reject TC before touching anything so
               the caller can use T: to select a lane and UL: to clear it. */
            if (lane_out_present(&g_lane_l1) && lane_out_present(&g_lane_l2)) {
                cmd_reply("ER", "DOUBLE_LOAD");
                return true;
            }
            sync_retract_assist_set(false);
            sync_set_state(SYNC_OFF);
            tc_start(ln, now_ms);
            cmd_reply("OK", NULL);
        } else {
            cmd_reply("ER", "ARG");
        }
        return true;
    } else if (!strcmp(cmd, "T")) {
        int ln = atoi(p);
        if (ln == 1 || ln == 2) {
            /* Double-load recovery: both OUT sensors active means the hub is
               stuck with two filaments.  Abort any in-progress TC state machine
               and stop all motion, then do a bare lane select so the operator
               can follow up with UL: to clear the faulty lane manually. */
            if (lane_out_present(&g_lane_l1) && lane_out_present(&g_lane_l2)) {
                tc_abort();
                /* Stop any in-flight feed on the current lane before switching.
                   Do not use RETRACT_ASSIST: UL: calls sync_disable anyway and
                   the RA state is superfluous here. */
                {
                    lane_t *old_lane = lane_ptr(g_active_lane);
                    if (old_lane && old_lane->task == TASK_FEED)
                        lane_stop(old_lane);
                }
                sync_disable(false);
                set_active_lane(ln);
                cmd_reply("OK", NULL);
            } else {
                if (tc_busy()) {
                    cmd_reply("ER", "BUSY:LANE");
                    return true;
                }
                sync_retract_assist_set(false);
                set_active_lane(ln);
                cmd_reply("OK", NULL);
            }
        } else {
            cmd_reply("ER", "ARG");
        }
        return true;
    } else if (!strcmp(cmd, "FD")) {
        if (tc_busy()) {
            cmd_reply("ER", "BUSY:LANE");
            return true;
        }
        lane_t *lane = get_active_lane_and_clear_error();
        if (!lane)
            return true;
        /* Double-load guard: do not feed the active lane into a hub already
           occupied by the other lane. MV: stays unguarded for raw recovery. */
        if (on_al(&g_y_split) && !lane_out_present(lane)) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return true;
        }
        lane_t *other = lane_ptr(other_lane(g_active_lane));
        if (other && lane_out_present(other) && other->task == TASK_IDLE) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return true;
        }
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        lane_start(lane, TASK_FEED, g_feed_sps, true, now_ms, 0);
        cmd_reply("OK", NULL);
        return true;
    }
    return false;
}

static bool cmd_handle_bl_command(const char *p, uint32_t now_ms) {
    /* Buffer-lock arm command.
     *   BL or BL:T → arm TENSION (no follow-on)
     *   BL:C → arm COMPRESSION (no follow-on)
     *   BL:T:<follow_mm>:<follow_rate_mmpm> → arm TENSION + follow-on
     *   BL:C:<follow_mm>:<follow_rate_mmpm> → arm COMPRESSION + follow-on
     * Follow-on fires concurrent MMU motion (prime direction) on the
     * first raw transition off the armed extreme — i.e. the moment
     * the extruder starts filling the buffer. Mass-balances long
     * extruder retracts that exceed the buffer's mechanical headroom.
     * BL is allowed to take over from SYNC_ACTIVE and active BS. Reject
     * only if a non-sync lane task or hard activity is running. */
    char dir_tok = 'T';
    float follow_mm = 0.0f;
    float follow_rate = 0.0f;
    int n = sscanf(p, "%c:%f:%f", &dir_tok, &follow_mm, &follow_rate);
    if (n < 1)
        dir_tok = 'T';
    if (dir_tok != 'T' && dir_tok != 'C') {
        cmd_reply("ER", "ARG");
        return true;
    }
    if (n == 2 || (n == 3 && (follow_mm <= 0.0f || follow_rate <= 0.0f))) {
        cmd_reply("ER", "ARG");
        return true;
    }
    if (controller_hard_activity_in_progress()) {
        cmd_reply("ER", "BUSY:BL");
        return true;
    }
    buffer_stabilize_cancel();
    if (sync_enabled) {
        sync_disable(false);
        lane_t *lane = lane_ptr(g_active_lane);
        if (lane && lane->task == TASK_FEED)
            lane_stop(lane);
    } else if (g_sync_state != SYNC_OFF && g_sync_state != SYNC_RETRACT_ASSIST) {
        sync_disable(false);
    }
    if (controller_activity_in_progress()) {
        cmd_reply("ER", "BUSY:BL");
    } else {
        buf_state_t target = (dir_tok == 'C') ? BUF_COMPRESSION : BUF_TENSION;
        sync_buffer_lock_arm(target, follow_mm, follow_rate, now_ms);
        cmd_reply("OK", NULL);
    }
    return true;
}

static bool cmd_handle_sensor_status(const char *cmd, const char *p, uint32_t now_ms) {
    if (!strcmp(cmd, "ST")) {
        tc_abort();
        cutter_abort();
        manual_unload_reset();
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        sync_disable(false);
        stop_all();
        set_toolhead_filament(false);
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "BS")) {
        if (controller_hard_activity_in_progress()) {
            cmd_reply("ER", "BUSY:BL");
            return true;
        }
        /* BS is the buffer-service preemptor. Stop compatible firmware-owned
         * services first so macros can replace sync/BL/old-BS/simple lane
         * commands without sleeping on racy BUSY windows. */
        if (g_sync_state == SYNC_RETRACT_ASSIST) {
            sync_retract_assist_set(false);
        } else if (g_sync_state != SYNC_OFF) {
            sync_disable(false);
        }
        buffer_stabilize_cancel();
        if (g_lane_l1.task != TASK_IDLE || g_lane_l2.task != TASK_IDLE) {
            stop_all();
        }
        /* Always force a full stabilize. If buffer-lock was active, release
         * it first AND clear the BL auto-start suppression so the stabilize
         * can drive the buffer back to NEUTRAL even when raw is still
         * pinned at the armed extreme (e.g. filament parked at TENSION
         * after a tip-form unload move with no external force to push it
         * off the switch). Without clearing suppression the buffer can
         * stay stuck and sync never re-engages. */
        if (controller_activity_in_progress()) {
            cmd_reply("ER", "BUSY:BL");
            return true;
        }
        /* Always clear BL auto-start suppression on BS, not just when BL is
         * active at call time. Suppression can leak in from other paths
         * (e.g. TS:1 entering sync_retract_assist_set(false) while sync is
         * already in SYNC_RETRACT_ASSIST from a TC reload) and otherwise
         * sticks until the buffer physically departs TENSION — which never
         * happens if filament is parked at TENSION with no extruder demand.
         * BS is the explicit "operator wants the buffer normalized" call,
         * so always clear here. */
        sync_bl_clear_autostart_suppress();
        if (!buffer_stabilize_request(now_ms)) {
            cmd_reply("ER", "BUF_STAB_UNAVAILABLE");
            return true;
        }
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "TS")) {
        int v = atoi(p);
        if (v == 0 || v == 1) {
            if (v == 1) {
                sync_retract_assist_set(false);
                sync_disable(false);
                /* Explicit "toolhead loaded" signal — clear any leftover BL
                 * auto-start suppression so the buffer-tension auto-engage
                 * gate can fire on the next BUF_TENSION. Without this the
                 * load path (TC → TS:1, no BS) can leave suppression set
                 * from a prior BL release elsewhere in the session, and
                 * sync never engages even with filament loaded + buffer at
                 * TENSION + g_auto_mode on. */
                sync_bl_clear_autostart_suppress();
            }
            set_toolhead_filament(v == 1);
            cmd_reply("OK", NULL);
        } else {
            cmd_reply("ER", "ARG");
        }
        return true;
    } else if (!strcmp(cmd, "RA")) {
        /* Legacy RA command removed; BL replaces it (task 4.3). */
        cmd_reply("ER", "CMD");
        return true;
    } else if (!strcmp(cmd, "BL")) {
        return cmd_handle_bl_command(p, now_ms);
    } else if (!strcmp(cmd, "SM")) {
        int v = atoi(p);
        if (v == 0 || v == 1) {
            if (v == 1)
                sync_set_state(SYNC_ACTIVE);
            else
                sync_disable(false);
            g_sync_auto_started = false;
            g_sync_tail_assist_active = false;
            if (v == 0)
                g_sync_current_sps = 0;
            cmd_reply("OK", NULL);
        } else {
            cmd_reply("ER", "ARG");
        }
        return true;
    }
    return false;
}

static bool cmd_handle_system(const char *cmd, const char *p, uint32_t now_ms) {
    if (!strcmp(cmd, "CAL")) {
        if (controller_activity_in_progress()) {
            cmd_reply("ER", "PERSIST_BUSY");
            return true;
        }
        if (!strcmp(p, "PSF_COMP")) {
            buf_analog_update((uint32_t)g_sync_tick_ms);
            g_buf_psf_max_comp = clamp_f(g_buf_pos_raw_status, 0.0f, 1.0f);
            settings_save();
            cmd_reply("OK", NULL);
        } else if (!strcmp(p, "PSF_TENS")) {
            buf_analog_update((uint32_t)g_sync_tick_ms);
            g_buf_psf_max_tens = clamp_f(g_buf_pos_raw_status, 0.0f, 1.0f);
            settings_save();
            cmd_reply("OK", NULL);
        } else if (!strcmp(p, "PSF_NEUT")) {
            buf_analog_update((uint32_t)g_sync_tick_ms);
            g_buf_psf_neutral = clamp_f(g_buf_pos_raw_status, 0.0f, 1.0f);
            settings_save();
            cmd_reply("OK", NULL);
        } else {
            cmd_reply("ER", "ARG");
        }
        return true;
    } else if (!strcmp(cmd, "SV")) {
        if (controller_activity_in_progress()) {
            cmd_reply("ER", "PERSIST_BUSY");
            return true;
        }
        settings_save();
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "LD")) {
        if (controller_activity_in_progress()) {
            cmd_reply("ER", "PERSIST_BUSY");
            return true;
        }
        settings_load();
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "RS")) {
        if (controller_activity_in_progress()) {
            cmd_reply("ER", "PERSIST_BUSY");
            return true;
        }
        settings_defaults();
        settings_save();
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "VR")) {
        cmd_reply("OK", CONF_FW_VERSION);
        return true;
    } else if (!strcmp(cmd, "?")) {
        cmd_handle_status_dump();
        return true;
    } else if (!strcmp(cmd, "MARK")) {
        size_t n = strlen(p);
        if (n >= sizeof(g_marker_tag))
            n = sizeof(g_marker_tag) - 1;
        memcpy(g_marker_tag, p, n);
        g_marker_tag[n] = '\0';
        g_marker_seq++;
        cmd_reply("OK", "MARK");
        return true;
    } else if (!strcmp(cmd, "BOOT")) {
        cmd_reply("OK", "REBOOTING_TO_BOOTSEL");
        sleep_ms(BOOTSEL_REPLY_DELAY_MS);
        reset_usb_boot(0, 0);
        return true;
    }
    return false;
}

static void cmd_execute(const char *cmd, const char *p, uint32_t now_ms) {
    if (manual_unload_active() && strcmp(cmd, "ST") != 0 && strcmp(cmd, "?") != 0 &&
        strcmp(cmd, "GET") != 0) {
        cmd_reply("ER", "BUSY:UNLOAD");
        return;
    }

    if (cmd_handle_tmc_advanced(cmd, p, now_ms)) {
        return;
    }

    if (cmd_handle_motion(cmd, p, now_ms)) {
        return;
    }

    if (cmd_handle_sensor_status(cmd, p, now_ms)) {
        return;
    }

    if (cmd_handle_system(cmd, p, now_ms)) {
        return;
    }

    if (!strcmp(cmd, "SET")) {
        cmd_handle_set(p, now_ms);
    } else if (!strcmp(cmd, "GET")) {
        cmd_handle_get(p, now_ms);
    } else {
        cmd_reply("ER", "UNKNOWN");
    }
}

// ============================================================================
// Serial poll — accumulate a line from USB CDC and dispatch it
// ============================================================================

void cmd_poll(uint32_t now_ms) {
    manual_unload_tick(now_ms);

    int c;
    int bytes_processed = 0;
    int commands_processed = 0;

    while (bytes_processed < CMD_POLL_BYTE_BUDGET && commands_processed < CMD_POLL_COMMAND_BUDGET &&
           (c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        bytes_processed++;

        if (c == '\r')
            continue;

        if (c == '\n') {
            g_cmd.buf[g_cmd.pos] = 0;

            if (g_cmd.overflow) {
                cmd_reply("ER", "OVERFLOW");
            } else if (g_cmd.pos > 0) {
                char *colon = strchr(g_cmd.buf, ':');
                const char *payload = "";
                if (colon) {
                    *colon = 0;
                    payload = colon + 1;
                }
                cmd_execute(g_cmd.buf, payload, now_ms);
            }

            g_cmd.pos = 0;
            g_cmd.overflow = false;
            commands_processed++;
            continue;
        }

        if (g_cmd.pos >= (int)sizeof(g_cmd.buf) - 1) {
            g_cmd.overflow = true;
            continue;
        }

        g_cmd.buf[g_cmd.pos++] = (char)c;
    }
}
