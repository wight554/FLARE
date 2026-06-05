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

typedef struct {
    char buf[64];
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
    if (lane1_present && !lane2_present && active_lane != 1)
        set_active_lane(1);
    else if (lane2_present && !lane1_present && active_lane != 2)
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
    if (max_span_mm < 2.0f)
        max_span_mm = 2.0f;
    return clamp_f(span_mm, 2.0f, max_span_mm) * 0.5f;
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

    char line[768];
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

static lane_t *get_active_lane_and_clear_error(void) {
    if (g_tc_ctx.state == TC_ERROR)
        tc_abort();
    lane_t *lane = lane_ptr(active_lane);
    if (!lane) {
        cmd_reply("ER", "NO_ACTIVE_LANE");
        return NULL;
    }
    return lane;
}

static bool parse_optional_lane_payload(const char *p, int *lane_out, bool *explicit_lane) {
    *explicit_lane = false;
    *lane_out = active_lane;

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
    lane_start(lane, TASK_UNLOAD, REV_SPS, false, now_ms, (float)UNLOAD_MAX_MM);
    lane->suppress_unloaded_event = suppress_event;
}

static void start_manual_unload_to_in(lane_t *lane, uint32_t now_ms) {
    lane->unload_to_in = true;
    lane->unload_buf_recover_done = false;
    lane_start(lane, TASK_UNLOAD, REV_SPS, false, now_ms, (float)UNLOAD_MAX_MM);
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

static bool cmd_get_motion_params(const char *param, int idx, char *out, size_t out_len) {
    if (!strcmp(param, "FEED_RATE"))
        snprintf(out, out_len, "FEED_RATE:%.1f", (double)sps_to_mm_per_min_idx(FEED_SPS, idx));
    else if (!strcmp(param, "REV_RATE"))
        snprintf(out, out_len, "REV_RATE:%.1f", (double)sps_to_mm_per_min_idx(REV_SPS, idx));
    else if (!strcmp(param, "AUTO_RATE"))
        snprintf(out, out_len, "AUTO_RATE:%.1f", (double)sps_to_mm_per_min_idx(AUTO_SPS, idx));
    else if (!strcmp(param, "GLOBAL_MAX_RATE"))
        snprintf(out, out_len, "GLOBAL_MAX_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(GLOBAL_MAX_SPS, idx));
    else if (!strcmp(param, "GLOBAL_MAX_ACCEL")) {
        float tick_s = (float)RAMP_TICK_MS / 1000.0f;
        float accel = (float)RAMP_STEP_SPS * MM_PER_STEP[0] / tick_s;
        snprintf(out, out_len, "GLOBAL_MAX_ACCEL:%.0f", (double)accel);
    }
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "RAMP_TICK_MS"))
        snprintf(out, out_len, "RAMP_TICK_MS:%d", RAMP_TICK_MS);
    else if (!strcmp(param, "PRE_RAMP_RATE"))
        snprintf(out, out_len, "PRE_RAMP_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(PRE_RAMP_SPS, idx));
    else if (!strcmp(param, "STARTUP_MS"))
        snprintf(out, out_len, "STARTUP_MS:%d", MOTION_STARTUP_MS);
#endif
    else if (!strcmp(param, "AUTOLOAD_MAX"))
        snprintf(out, out_len, "AUTOLOAD_MAX:%d", AUTOLOAD_MAX_MM);
    else if (!strcmp(param, "LOAD_MAX"))
        snprintf(out, out_len, "LOAD_MAX:%d", LOAD_MAX_MM);
    else if (!strcmp(param, "UNLOAD_MAX"))
        snprintf(out, out_len, "UNLOAD_MAX:%d", UNLOAD_MAX_MM);
    else if (!strcmp(param, "TC_LOAD_MM"))
        snprintf(out, out_len, "TC_LOAD_MM:%d", LOAD_MAX_MM);
    else if (!strcmp(param, "TC_UNLOAD_MM"))
        snprintf(out, out_len, "TC_UNLOAD_MM:%d", UNLOAD_MAX_MM);
    else if (!strcmp(param, "RETRACT_MM"))
        snprintf(out, out_len, "RETRACT_MM:%d", AUTOLOAD_RETRACT_MM);
    else
        return false;
    return true;
}

static bool cmd_get_tmc_params(const char *param, int idx, char *out, size_t out_len) {
    if (!strcmp(param, "MICROSTEPS"))
        snprintf(out, out_len, "MICROSTEPS:%d", TMC_MICROSTEPS[idx]);
    else if (!strcmp(param, "INTERPOLATE"))
        snprintf(out, out_len, "INTERPOLATE:%d", TMC_INTERPOLATE[idx] ? 1 : 0);
    else if (!strcmp(param, "STEALTHCHOP"))
        snprintf(out, out_len, "STEALTHCHOP:%.1f",
                 (double)sps_to_mm_per_min_idx(TMC_STEALTHCHOP_SPS[idx], idx));
    else if (!strcmp(param, "DRIVER_TBL"))
        snprintf(out, out_len, "DRIVER_TBL:%d", TMC_TBL[idx]);
    else if (!strcmp(param, "DRIVER_TOFF"))
        snprintf(out, out_len, "DRIVER_TOFF:%d", TMC_TOFF[idx]);
    else if (!strcmp(param, "DRIVER_HSTRT"))
        snprintf(out, out_len, "DRIVER_HSTRT:%d", TMC_HSTRT[idx]);
    else if (!strcmp(param, "DRIVER_HEND"))
        snprintf(out, out_len, "DRIVER_HEND:%d", TMC_HEND[idx]);
    else if (!strcmp(param, "ROTATION_DIST"))
        snprintf(out, out_len, "ROTATION_DIST:%.3f", (double)TMC_ROTATION_DISTANCE[idx]);
    else if (!strcmp(param, "GEAR_RATIO"))
        snprintf(out, out_len, "GEAR_RATIO:%.3f", (double)TMC_GEAR_RATIO[idx]);
    else if (!strcmp(param, "FULL_STEPS"))
        snprintf(out, out_len, "FULL_STEPS:%d", TMC_FULL_STEPS[idx]);
    else if (!strcmp(param, "RUN_CURRENT_MA"))
        snprintf(out, out_len, "RUN_CURRENT_MA:%d", TMC_RUN_CURRENT_MA[idx]);
    else if (!strcmp(param, "HOLD_CURRENT_MA"))
        snprintf(out, out_len, "HOLD_CURRENT_MA:%d", TMC_HOLD_CURRENT_MA[idx]);
    else
        return false;
    return true;
}

static bool cmd_get_buffer_geometry_params(const char *param, int idx, char *out, size_t out_len) {
    if (!strcmp(param, "SYNC_MAX_RATE"))
        snprintf(out, out_len, "SYNC_MAX_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(SYNC_MAX_SPS, idx));
    else if (!strcmp(param, "SYNC_MIN_RATE"))
        snprintf(out, out_len, "SYNC_MIN_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(SYNC_MIN_SPS, idx));
    else if (!strcmp(param, "SYNC_RAMP_ACCEL")) {
        float tick_s = (float)SYNC_TICK_MS / 1000.0f;
        float accel = (float)SYNC_RAMP_UP_SPS * MM_PER_STEP[0] / tick_s;
        snprintf(out, out_len, "SYNC_RAMP_ACCEL:%.0f", (double)accel);
    } else if (!strcmp(param, "SYNC_RAMP_DECEL")) {
        float tick_s = (float)SYNC_TICK_MS / 1000.0f;
        float accel = (float)SYNC_RAMP_DN_SPS * MM_PER_STEP[0] / tick_s;
        snprintf(out, out_len, "SYNC_RAMP_DECEL:%.0f", (double)accel);
    }
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "SYNC_TICK_MS"))
        snprintf(out, out_len, "SYNC_TICK_MS:%d", SYNC_TICK_MS);
    else if (!strcmp(param, "BUF_HYST"))
        snprintf(out, out_len, "BUF_HYST:%d", BUF_HYST_MS);
    else if (!strcmp(param, "BUF_PREDICT_THR_MS"))
        snprintf(out, out_len, "BUF_PREDICT_THR_MS:%d", BUF_PREDICT_THR_MS);
#endif
    else if (!strcmp(param, "BUF_SWITCH_SPAN"))
        snprintf(out, out_len, "BUF_SWITCH_SPAN:%.3f", (double)(BUF_SWITCH_SPAN_HALF_MM * 2.0f));
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
        snprintf(out, out_len, "BUF_SENSOR:%d", BUF_SENSOR_TYPE);
    else if (!strcmp(param, "BUF_HOME_STATE"))
        snprintf(out, out_len, "BUF_HOME_STATE:%d", BUF_HOME_STATE);
    else if (!strcmp(param, "BUF_PSF_MAX_COMP"))
        snprintf(out, out_len, "BUF_PSF_MAX_COMP:%.3f", (double)BUF_PSF_MAX_COMP);
    else if (!strcmp(param, "BUF_PSF_MAX_TENS"))
        snprintf(out, out_len, "BUF_PSF_MAX_TENS:%.3f", (double)BUF_PSF_MAX_TENS);
    else if (!strcmp(param, "BUF_PSF_NEUTRAL"))
        snprintf(out, out_len, "BUF_PSF_NEUTRAL:%.3f", (double)BUF_PSF_NEUTRAL);
    else if (!strcmp(param, "BUF_GOAL"))
        snprintf(out, out_len, "BUF_GOAL:%.3f", (double)BUF_GOAL);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "BUF_ALPHA"))
        snprintf(out, out_len, "BUF_ALPHA:%.3f", (double)BUF_ANALOG_ALPHA);
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
        snprintf(out, out_len, "SYNC_KP_RATE:%.1f", (double)sps_to_mm_per_min(SYNC_KP_SPS));
    else if (!strcmp(param, "KD_PSF"))
        snprintf(out, out_len, "KD_PSF:%.3f", (double)KD_PSF);
    else if (!strcmp(param, "SYNC_PSF_SLEW_PER_MM"))
        snprintf(out, out_len, "SYNC_PSF_SLEW_PER_MM:%.1f", (double)SYNC_PSF_SLEW_PER_MM);
    else if (!strcmp(param, "SYNC_PSF_FILTER_MM"))
        snprintf(out, out_len, "SYNC_PSF_FILTER_MM:%.2f", (double)SYNC_PSF_FILTER_MM);
    else if (!strcmp(param, "SYNC_PSF_DECAY_SPS_PER_S"))
        snprintf(out, out_len, "SYNC_PSF_DECAY_SPS_PER_S:%.1f", (double)SYNC_PSF_DECAY_SPS_PER_S);
    else if (!strcmp(param, "PSF_STAB_STAGNANT_MS"))
        snprintf(out, out_len, "PSF_STAB_STAGNANT_MS:%d", PSF_STAB_STAGNANT_MS);
    else if (!strcmp(param, "PSF_STAB_STAGNANT_NORM"))
        snprintf(out, out_len, "PSF_STAB_STAGNANT_NORM:%.3f", (double)PSF_STAB_STAGNANT_NORM);
    else if (!strcmp(param, "PSF_STAB_RAIL_BREAK_MS"))
        snprintf(out, out_len, "PSF_STAB_RAIL_BREAK_MS:%d", PSF_STAB_RAIL_BREAK_MS);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "SYNC_OVERSHOOT_PCT"))
        snprintf(out, out_len, "SYNC_OVERSHOOT_PCT:%d", SYNC_OVERSHOOT_PCT);
#endif
    else if (!strcmp(param, "SYNC_RESERVE_PCT"))
        snprintf(out, out_len, "SYNC_RESERVE_PCT:%d", SYNC_RESERVE_PCT);
    else if (!strcmp(param, "COMPRESSION_BIAS_FRAC"))
        snprintf(out, out_len, "COMPRESSION_BIAS_FRAC:%.3f", (double)SYNC_COMPRESSION_BIAS_FRAC);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "NEUTRAL_CREEP_TIMEOUT_MS"))
        snprintf(out, out_len, "NEUTRAL_CREEP_TIMEOUT_MS:%d", NEUTRAL_CREEP_TIMEOUT_MS);
    else if (!strcmp(param, "NEUTRAL_CREEP_RATE") || !strcmp(param, "NEUTRAL_CREEP_RATE_SPS_PER_S"))
        snprintf(out, out_len, "%s:%d", param, NEUTRAL_CREEP_RATE_SPS_PER_S);
    else if (!strcmp(param, "NEUTRAL_CREEP_CAP") || !strcmp(param, "NEUTRAL_CREEP_CAP_FRAC"))
        snprintf(out, out_len, "%s:%d", param, NEUTRAL_CREEP_CAP_FRAC);
    else if (!strcmp(param, "開設_blend_frac") || !strcmp(param, "BUF_VARIANCE_BLEND_FRAC"))
        snprintf(out, out_len, "%s:%.3f", param, (double)BUF_VARIANCE_BLEND_FRAC);
    else if (!strcmp(param, "開設_blend_ref_mm") || !strcmp(param, "BUF_VARIANCE_BLEND_REF_MM"))
        snprintf(out, out_len, "%s:%.3f", param, (double)BUF_VARIANCE_BLEND_REF_MM);
#endif
    else if (!strcmp(param, "SYNC_AUTO_STOP"))
        snprintf(out, out_len, "SYNC_AUTO_STOP:%d", SYNC_AUTO_STOP_MS);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "SYNC_TENSION_STOP_MS"))
        snprintf(out, out_len, "SYNC_TENSION_STOP_MS:%d", SYNC_TENSION_DWELL_STOP_MS);
    else if (!strcmp(param, "SYNC_TENSION_RAMP_MS"))
        snprintf(out, out_len, "SYNC_TENSION_RAMP_MS:%d", SYNC_TENSION_RAMP_DELAY_MS);
    else if (!strcmp(param, "SYNC_OVERSHOOT_NEUTRAL_EXT"))
        snprintf(out, out_len, "SYNC_OVERSHOOT_NEUTRAL_EXT:%d", SYNC_OVERSHOOT_NEUTRAL_EXTEND);
    else if (!strcmp(param, "SYNC_INT_GAIN"))
        snprintf(out, out_len, "SYNC_INT_GAIN:%.4f", (double)SYNC_RESERVE_INTEGRAL_GAIN);
    else if (!strcmp(param, "SYNC_INT_CLAMP"))
        snprintf(out, out_len, "SYNC_INT_CLAMP:%.3f", (double)SYNC_RESERVE_INTEGRAL_CLAMP_MM);
    else if (!strcmp(param, "SYNC_INT_DECAY_MS"))
        snprintf(out, out_len, "SYNC_INT_DECAY_MS:%d", SYNC_RESERVE_INTEGRAL_DECAY_MS);
    else if (!strcmp(param, "EST_SIGMA_CAP"))
        snprintf(out, out_len, "EST_SIGMA_CAP:%.3f", (double)EST_SIGMA_HARD_CAP_MM);
    else if (!strcmp(param, "EST_LOW_CF_THR"))
        snprintf(out, out_len, "EST_LOW_CF_THR:%.3f", (double)EST_LOW_CF_WARN_THRESHOLD);
    else if (!strcmp(param, "EST_FALLBACK_THR"))
        snprintf(out, out_len, "EST_FALLBACK_THR:%.3f", (double)EST_FALLBACK_CF_THRESHOLD);
#endif
    else
        return false;
    return true;
}

static bool cmd_get_sync_relay_probe_params(const char *param, int idx, char *out, size_t out_len) {
    if (!strcmp(param, "RELAY_CATCHUP_FRAC"))
        snprintf(out, out_len, "RELAY_CATCHUP_FRAC:%.3f", (double)RELAY_CATCHUP_FRAC);
    else if (!strcmp(param, "RELAY_NEUTRAL_FRAC"))
        snprintf(out, out_len, "RELAY_NEUTRAL_FRAC:%.3f", (double)RELAY_NEUTRAL_FRAC);
    else if (!strcmp(param, "SYNC_RELAY_TRIM_STEP_SPS"))
        snprintf(out, out_len, "SYNC_RELAY_TRIM_STEP_SPS:%d", SYNC_RELAY_TRIM_STEP_SPS);
    else if (!strcmp(param, "SYNC_RELAY_TRIM_CLAMP_SPS"))
        snprintf(out, out_len, "SYNC_RELAY_TRIM_CLAMP_SPS:%d", SYNC_RELAY_TRIM_CLAMP_SPS);
    else if (!strcmp(param, "SYNC_COMPRESSION_DRAIN_FRAC"))
        snprintf(out, out_len, "SYNC_COMPRESSION_DRAIN_FRAC:%.3f",
                 (double)SYNC_COMPRESSION_DRAIN_FRAC);
    else if (!strcmp(param, "SYNC_COMPRESSION_DRAIN_BUDGET_MM"))
        snprintf(out, out_len, "SYNC_COMPRESSION_DRAIN_BUDGET_MM:%.3f",
                 (double)SYNC_COMPRESSION_DRAIN_BUDGET_MM);
    else if (!strcmp(param, "SYNC_EST_ATTACK_ALPHA"))
        snprintf(out, out_len, "SYNC_EST_ATTACK_ALPHA:%.3f", (double)SYNC_EST_ATTACK_ALPHA);
    else if (!strcmp(param, "SYNC_TENSION_FAST_MM_S"))
        snprintf(out, out_len, "SYNC_TENSION_FAST_MM_S:%.3f", (double)SYNC_TENSION_FAST_MM_S);
    else if (!strcmp(param, "SYNC_TENSION_PROBE_MAX"))
        snprintf(out, out_len, "SYNC_TENSION_PROBE_MAX:%.1f",
                 (double)sps_to_mm_per_min_idx(SYNC_TENSION_PROBE_MAX_SPS, idx));
    else if (!strcmp(param, "SYNC_TENSION_PROBE_UP"))
        snprintf(out, out_len, "SYNC_TENSION_PROBE_UP:%.1f",
                 (double)sps_to_mm_per_min_idx(SYNC_TENSION_PROBE_UP_SPS_PER_S, idx));
    else if (!strcmp(param, "SYNC_TENSION_PROBE_DOWN"))
        snprintf(out, out_len, "SYNC_TENSION_PROBE_DOWN:%.1f",
                 (double)sps_to_mm_per_min_idx(SYNC_TENSION_PROBE_DOWN_SPS_PER_S, idx));
    else if (!strcmp(param, "SYNC_TENSION_PROBE_NEUTRAL"))
        snprintf(out, out_len, "SYNC_TENSION_PROBE_NEUTRAL:%.1f",
                 (double)sps_to_mm_per_min_idx(SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S, idx));
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "RELAY_MIN_FLIP_MM"))
        snprintf(out, out_len, "RELAY_MIN_FLIP_MM:%.3f", (double)RELAY_MIN_FLIP_MM);
    else if (!strcmp(param, "RELAY_COLLAPSE_DELAY_MS"))
        snprintf(out, out_len, "RELAY_COLLAPSE_DELAY_MS:%d", RELAY_COLLAPSE_DELAY_MS);
    else if (!strcmp(param, "RELAY_COLLAPSE_RAMP_MULT"))
        snprintf(out, out_len, "RELAY_COLLAPSE_RAMP_MULT:%d", RELAY_COLLAPSE_RAMP_MULT);
    else if (!strcmp(param, "RELAY_COLLAPSE_CAP_MS"))
        snprintf(out, out_len, "RELAY_COLLAPSE_CAP_MS:%d", RELAY_COLLAPSE_CAP_MS);
    else if (!strcmp(param, "BUF_DRIFT_TAU_MS"))
        snprintf(out, out_len, "BUF_DRIFT_TAU_MS:%d", BUF_DRIFT_EWMA_TAU_MS);
    else if (!strcmp(param, "BUF_DRIFT_MIN_SMP"))
        snprintf(out, out_len, "BUF_DRIFT_MIN_SMP:%d", BUF_DRIFT_MIN_SAMPLES);
    else if (!strcmp(param, "BUF_DRIFT_THR_MM"))
        snprintf(out, out_len, "BUF_DRIFT_THR_MM:%.3f", (double)BUF_DRIFT_APPLY_THR_MM);
    else if (!strcmp(param, "BUF_DRIFT_CLAMP"))
        snprintf(out, out_len, "BUF_DRIFT_CLAMP:%.3f", (double)BUF_DRIFT_CLAMP_MM);
    else if (!strcmp(param, "BUF_DRIFT_MIN_CF"))
        snprintf(out, out_len, "BUF_DRIFT_MIN_CF:%.3f", (double)BUF_DRIFT_APPLY_MIN_CF);
    else if (!strcmp(param, "TENSION_RISK_WINDOW"))
        snprintf(out, out_len, "TENSION_RISK_WINDOW:%d", TENSION_RISK_WINDOW_MS);
    else if (!strcmp(param, "TENSION_RISK_THR"))
        snprintf(out, out_len, "TENSION_RISK_THR:%d", TENSION_RISK_THRESHOLD);
    else if (!strcmp(param, "TS_BUF_MS"))
        snprintf(out, out_len, "TS_BUF_MS:%d", TS_BUF_FALLBACK_MS);
    else if (!strcmp(param, "EST_ALPHA_MIN"))
        snprintf(out, out_len, "EST_ALPHA_MIN:%.3f", (double)EST_ALPHA_MIN);
    else if (!strcmp(param, "EST_ALPHA_MAX"))
        snprintf(out, out_len, "EST_ALPHA_MAX:%.3f", (double)EST_ALPHA_MAX);
    else if (!strcmp(param, "ZONE_BIAS_BASE"))
        snprintf(out, out_len, "ZONE_BIAS_BASE:%.1f",
                 (double)sps_to_mm_per_min(ZONE_BIAS_BASE_SPS));
    else if (!strcmp(param, "ZONE_BIAS_RAMP"))
        snprintf(out, out_len, "ZONE_BIAS_RAMP:%.1f",
                 (double)sps_to_mm_per_min(ZONE_BIAS_RAMP_SPS_S));
    else if (!strcmp(param, "ZONE_BIAS_MAX"))
        snprintf(out, out_len, "ZONE_BIAS_MAX:%.1f", (double)sps_to_mm_per_min(ZONE_BIAS_MAX_SPS));
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
        snprintf(out, out_len, "AUTO_PRELOAD:%d", AUTO_PRELOAD ? 1 : 0);
    else if (!strcmp(param, "CUTTER"))
        snprintf(out, out_len, "CUTTER:%d", ENABLE_CUTTER ? 1 : 0);
    else if (!strcmp(param, "AUTO_MODE"))
        snprintf(out, out_len, "AUTO_MODE:%d", AUTO_MODE);
    else if (!strcmp(param, "RELOAD_MODE"))
        snprintf(out, out_len, "RELOAD_MODE:%d", RELOAD_MODE);
    else if (!strcmp(param, "RUNOUT_COOLDOWN_MS"))
        snprintf(out, out_len, "RUNOUT_COOLDOWN_MS:%d", RUNOUT_COOLDOWN_MS);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "POST_PRINT_STAB_MS"))
        snprintf(out, out_len, "POST_PRINT_STAB_MS:%d", POST_PRINT_STAB_DELAY_MS);
    else if (!strcmp(param, "RELOAD_Y_MS"))
        snprintf(out, out_len, "RELOAD_Y_MS:%d", RELOAD_Y_TIMEOUT_MS);
#endif
    else if (!strcmp(param, "RELOAD_JOIN_MS"))
        snprintf(out, out_len, "RELOAD_JOIN_MS:%d", RELOAD_JOIN_DELAY_MS);
    else if (!strcmp(param, "DIST_IN_OUT"))
        snprintf(out, out_len, "DIST_IN_OUT:%d", DIST_IN_OUT);
    else if (!strcmp(param, "DIST_OUT_Y"))
        snprintf(out, out_len, "DIST_OUT_Y:%d", DIST_OUT_Y);
    else if (!strcmp(param, "DIST_Y_BUF"))
        snprintf(out, out_len, "DIST_Y_BUF:%d", DIST_Y_BUF);
    else if (!strcmp(param, "BUF_BODY_LEN"))
        snprintf(out, out_len, "BUF_BODY_LEN:%d", BUF_BODY_LEN);
    else if (!strcmp(param, "BUF_MAX_TRAVEL"))
        snprintf(out, out_len, "BUF_MAX_TRAVEL:%d", BUF_MAX_TRAVEL_MM);
    else if (!strcmp(param, "JOIN_RATE"))
        snprintf(out, out_len, "JOIN_RATE:%.1f", (double)sps_to_mm_per_min_idx(JOIN_SPS, idx));
    else if (!strcmp(param, "PRESS_RATE"))
        snprintf(out, out_len, "PRESS_RATE:%.1f", (double)sps_to_mm_per_min_idx(PRESS_SPS, idx));
    else if (!strcmp(param, "COMPRESSION_RATE"))
        snprintf(out, out_len, "COMPRESSION_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(COMPRESSION_SPS, idx));
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "BUF_STAB_RATE"))
        snprintf(out, out_len, "BUF_STAB_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(BUF_STAB_SPS, idx));
#endif
    else if (!strcmp(param, "FOLLOW_MS"))
        snprintf(out, out_len, "FOLLOW_MS:%d", FOLLOW_TIMEOUT_MS[idx]);
    else if (!strcmp(param, "UNLOAD_TENSION_BLOCK_MS"))
        snprintf(out, out_len, "UNLOAD_TENSION_BLOCK_MS:%d", UNLOAD_TENSION_BLOCK_MS);
    else if (!strcmp(param, "SERVO_OPEN"))
        snprintf(out, out_len, "SERVO_OPEN:%d", SERVO_OPEN_US);
    else if (!strcmp(param, "SERVO_CLOSE"))
        snprintf(out, out_len, "SERVO_CLOSE:%d", SERVO_CLOSE_US);
    else if (!strcmp(param, "SERVO_BLOCK"))
        snprintf(out, out_len, "SERVO_BLOCK:%d", SERVO_BLOCK_US);
    else if (!strcmp(param, "SERVO_SETTLE") || !strcmp(param, "SERVO_SETTLE_MS"))
        snprintf(out, out_len, "%s:%d", param, SERVO_SETTLE_MS);
    else if (!strcmp(param, "UNLOAD_CUT"))
        snprintf(out, out_len, "UNLOAD_CUT:%d", UNLOAD_CUT ? 1 : 0);
    else if (!strcmp(param, "CUT_FEED_RATE"))
        snprintf(out, out_len, "CUT_FEED_RATE:%.1f",
                 (double)sps_to_mm_per_min_idx(CUT_FEED_SPS, idx));
    else if (!strcmp(param, "CUT_FEED"))
        snprintf(out, out_len, "CUT_FEED:%d", CUT_FEED_MM);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "CUT_FEED_MS"))
        snprintf(out, out_len, "CUT_FEED_MS:%d", CUT_TIMEOUT_FEED_MS);
    else if (!strcmp(param, "CUT_SETTLE_MS"))
        snprintf(out, out_len, "CUT_SETTLE_MS:%d", CUT_TIMEOUT_SETTLE_MS);
#endif
    else if (!strcmp(param, "CUT_LEN"))
        snprintf(out, out_len, "CUT_LEN:%d", CUT_LENGTH_MM);
    else if (!strcmp(param, "CUT_AMT"))
        snprintf(out, out_len, "CUT_AMT:%d", CUT_AMOUNT);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(param, "TC_CUT_MS"))
        snprintf(out, out_len, "TC_CUT_MS:%d", TC_TIMEOUT_CUT_MS);
    else if (!strcmp(param, "TC_TH_MS"))
        snprintf(out, out_len, "TC_TH_MS:%d", TC_TIMEOUT_TH_MS);
    else if (!strcmp(param, "TC_Y_MS"))
        snprintf(out, out_len, "TC_Y_MS:%d", TC_TIMEOUT_Y_MS);
    else if (!strcmp(param, "RELOAD_LEAN"))
        snprintf(out, out_len, "RELOAD_LEAN:%.2f", (double)RELOAD_LEAN_FACTOR);
#endif
    else
        return false;
    return true;
}

static void cmd_handle_get(const char *p, uint32_t now_ms) {
    char out[64];
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

static bool cmd_set_motion_params(const char *base_param, int iv, float fv) {
    if (!strcmp(base_param, "FEED_RATE"))
        FEED_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
    else if (!strcmp(base_param, "REV_RATE"))
        REV_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
    else if (!strcmp(base_param, "AUTO_RATE"))
        AUTO_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
    else if (!strcmp(base_param, "SYNC_MAX_RATE"))
        SYNC_MAX_SPS = sync_clamp_max_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
    else if (!strcmp(base_param, "GLOBAL_MAX_RATE")) {
        GLOBAL_MAX_SPS =
            clamp_i(mm_per_min_to_sps(fv), mm_per_min_to_sps(1000.0f), mm_per_min_to_sps(12000.0f));
    } else if (!strcmp(base_param, "SYNC_MIN_RATE"))
        SYNC_MIN_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 0, 50000));
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "SYNC_RAMP_ACCEL")) {
        float tick_s = (float)SYNC_TICK_MS / 1000.0f;
        int sps = (int)(fv * tick_s / MM_PER_STEP[0] + 0.5f);
        SYNC_RAMP_UP_SPS = motion_clamp_rate_sps(clamp_i(sps, 1, 50000));
    } else if (!strcmp(base_param, "SYNC_RAMP_DECEL")) {
        float tick_s = (float)SYNC_TICK_MS / 1000.0f;
        int sps = (int)(fv * tick_s / MM_PER_STEP[0] + 0.5f);
        SYNC_RAMP_DN_SPS = motion_clamp_rate_sps(clamp_i(sps, 1, 50000));
    } else if (!strcmp(base_param, "SYNC_TICK_MS"))
        SYNC_TICK_MS = clamp_i(iv, 1, 1000);
    else if (!strcmp(base_param, "GLOBAL_MAX_ACCEL")) {
        float tick_s = (float)RAMP_TICK_MS / 1000.0f;
        int sps = (int)(fv * tick_s / MM_PER_STEP[0] + 0.5f);
        RAMP_STEP_SPS = motion_clamp_rate_sps(clamp_i(sps, 1, 30000));
    } else if (!strcmp(base_param, "RAMP_TICK_MS"))
        RAMP_TICK_MS = clamp_i(iv, 1, 1000);
    else if (!strcmp(base_param, "PRE_RAMP_RATE"))
        PRE_RAMP_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 0, 50000));
#endif
    else if (!strcmp(base_param, "BUF_SWITCH_SPAN")) {
        BUF_SWITCH_SPAN_HALF_MM = buf_switch_span_half_from_full(fv, BUF_MAX_TRAVEL_MM);
    }
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "BUF_HYST"))
        BUF_HYST_MS = clamp_i(iv, 5, 500);
    else if (!strcmp(base_param, "BUF_PREDICT_THR_MS"))
        BUF_PREDICT_THR_MS = clamp_i(iv, 0, 10000);
#endif
    else
        return false;
    return true;
}

static bool cmd_set_reload_motion_params(const char *base_param, int iv, float fv) {
    if (!strcmp(base_param, "AUTO_PRELOAD"))
        AUTO_PRELOAD = (iv != 0);
    else if (!strcmp(base_param, "RETRACT_MM"))
        AUTOLOAD_RETRACT_MM = clamp_i(iv, 0, 50);
    else if (!strcmp(base_param, "CUTTER"))
        ENABLE_CUTTER = (iv != 0);
    else if (!strcmp(base_param, "AUTO_MODE"))
        AUTO_MODE = clamp_i(iv, 0, 1);
    else if (!strcmp(base_param, "RELOAD_MODE"))
        RELOAD_MODE = (iv != 0) ? 1 : 0;
    else if (!strcmp(base_param, "RUNOUT_COOLDOWN_MS"))
        RUNOUT_COOLDOWN_MS = clamp_i(iv, 0, 60000);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "POST_PRINT_STAB_MS"))
        POST_PRINT_STAB_DELAY_MS = clamp_i(iv, 0, 300000);
    else if (!strcmp(base_param, "RELOAD_Y_MS"))
        RELOAD_Y_TIMEOUT_MS = clamp_i(iv, 100, 30000);
#endif
    else if (!strcmp(base_param, "RELOAD_JOIN_MS"))
        RELOAD_JOIN_DELAY_MS = clamp_i(iv, 0, 10000);
    else if (!strcmp(base_param, "DIST_IN_OUT"))
        DIST_IN_OUT = clamp_i(iv, 10, 5000);
    else if (!strcmp(base_param, "DIST_OUT_Y"))
        DIST_OUT_Y = clamp_i(iv, 0, 5000);
    else if (!strcmp(base_param, "DIST_Y_BUF"))
        DIST_Y_BUF = clamp_i(iv, 0, 5000);
    else if (!strcmp(base_param, "BUF_BODY_LEN"))
        BUF_BODY_LEN = clamp_i(iv, 0, 5000);
    else if (!strcmp(base_param, "BUF_MAX_TRAVEL")) {
        BUF_MAX_TRAVEL_MM = clamp_i(iv, 10, 1000);
        float max_half = (float)BUF_MAX_TRAVEL_MM * 0.5f;
        if (BUF_SWITCH_SPAN_HALF_MM > max_half)
            BUF_SWITCH_SPAN_HALF_MM = max_half;
        if (BUF_SWITCH_SPAN_HALF_MM < 1.0f)
            BUF_SWITCH_SPAN_HALF_MM = 1.0f;
    } else if (!strcmp(base_param, "JOIN_RATE"))
        JOIN_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
    else if (!strcmp(base_param, "PRESS_RATE"))
        PRESS_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
    else if (!strcmp(base_param, "COMPRESSION_RATE"))
        COMPRESSION_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 10, 10000));
    else if (!strcmp(base_param, "BUF_STAB_RATE"))
        BUF_STAB_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 10, 10000));
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "BASELINE_ALPHA"))
        g_baseline_alpha = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "EST_ALPHA_MIN"))
        EST_ALPHA_MIN = clamp_f(fv, 0.01f, 1.0f);
    else if (!strcmp(base_param, "EST_ALPHA_MAX"))
        EST_ALPHA_MAX = clamp_f(fv, 0.01f, 1.0f);
    else if (!strcmp(base_param, "ZONE_BIAS_BASE"))
        ZONE_BIAS_BASE_SPS = clamp_i(mm_per_min_to_sps(fv), 0, 5000);
    else if (!strcmp(base_param, "ZONE_BIAS_RAMP"))
        ZONE_BIAS_RAMP_SPS_S = clamp_i(mm_per_min_to_sps(fv), 0, 5000);
    else if (!strcmp(base_param, "ZONE_BIAS_MAX"))
        ZONE_BIAS_MAX_SPS = clamp_i(mm_per_min_to_sps(fv), 0, 5000);
    else if (!strcmp(base_param, "RELOAD_LEAN"))
        RELOAD_LEAN_FACTOR = clamp_f(fv, 0.0f, 5.0f);
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
        SET_LANE({ TMC_RUN_CURRENT_MA[idx] = clamp_i(iv, 0, 2000); });
    } else if (!strcmp(base_param, "HOLD_CURRENT_MA")) {
        SET_LANE({ TMC_HOLD_CURRENT_MA[idx] = clamp_i(iv, 0, 2000); });
    } else if (!strcmp(base_param, "MICROSTEPS")) {
        SET_LANE({ TMC_MICROSTEPS[idx] = clamp_i(iv, 1, 256); });
    } else if (!strcmp(base_param, "ROTATION_DIST")) {
        SET_LANE({ TMC_ROTATION_DISTANCE[idx] = clamp_f(fv, 0.1f, 1000.0f); });
    } else if (!strcmp(base_param, "GEAR_RATIO")) {
        SET_LANE({ TMC_GEAR_RATIO[idx] = clamp_f(fv, 0.001f, 1000.0f); });
    } else if (!strcmp(base_param, "FULL_STEPS")) {
        SET_LANE({ TMC_FULL_STEPS[idx] = (iv == 400 ? 400 : 200); });
    } else if (!strcmp(base_param, "INTERPOLATE")) {
        SET_LANE({ TMC_INTERPOLATE[idx] = (iv != 0); });
    } else if (!strcmp(base_param, "STEALTHCHOP")) {
        SET_LANE({ TMC_STEALTHCHOP_SPS[idx] = (iv == 0) ? 0 : mm_per_min_to_sps_idx(fv, idx); });
    } else if (!strcmp(base_param, "DRIVER_TBL")) {
        SET_LANE({ TMC_TBL[idx] = clamp_i(iv, 0, 3); });
    } else if (!strcmp(base_param, "DRIVER_TOFF")) {
        SET_LANE({ TMC_TOFF[idx] = clamp_i(iv, 0, 15); });
    } else if (!strcmp(base_param, "DRIVER_HSTRT")) {
        SET_LANE({ TMC_HSTRT[idx] = clamp_i(iv, 0, 7); });
    } else if (!strcmp(base_param, "DRIVER_HEND")) {
        SET_LANE({ TMC_HEND[idx] = clamp_i(iv, -3, 12); });
    } else if (!strcmp(base_param, "FOLLOW_MS")) {
        SET_LANE({ FOLLOW_TIMEOUT_MS[idx] = clamp_i(iv, 1000, 60000); });
    } else {
#undef SET_LANE
        return false;
    }

#undef SET_LANE
    return true;
}

static cmd_set_result_t cmd_set_buffer_params(const char *base_param, int iv, float fv) {
    if (!strcmp(base_param, "BASELINE_RATE")) {
        int baseline_sps = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
        g_baseline_target_sps = baseline_sps;
        g_baseline_sps = baseline_sps;
        flow_schedule_refresh_scalar();
    } else if (!strcmp(base_param, "BASELINE_SPS")) {
        int baseline_sps = motion_clamp_rate_sps(clamp_i(iv, 200, 50000));
        g_baseline_target_sps = baseline_sps;
        g_baseline_sps = baseline_sps;
        flow_schedule_refresh_scalar();
    } else if (!strcmp(base_param, "BUF_SENSOR")) {
        if (sync_enabled || tc_state() != TC_IDLE || g_lane_l1.task != TASK_IDLE ||
            g_lane_l2.task != TASK_IDLE) {
            cmd_reply("ER", "BUSY");
            return CMD_SET_REPLIED;
        }
        BUF_SENSOR_TYPE = clamp_i(iv, 0, 1);
        sync_disable(false);
    } else if (!strcmp(base_param, "BUF_HOME_STATE"))
        BUF_HOME_STATE = clamp_i(iv, 0, 2);
    else if (!strcmp(base_param, "BUF_PSF_MAX_COMP"))
        BUF_PSF_MAX_COMP = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "BUF_PSF_MAX_TENS"))
        BUF_PSF_MAX_TENS = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "BUF_PSF_NEUTRAL"))
        BUF_PSF_NEUTRAL = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "BUF_GOAL"))
        BUF_GOAL = clamp_f(fv, 0.0f, 1.0f);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "BUF_ALPHA"))
        BUF_ANALOG_ALPHA = clamp_f(fv, 0.01f, 1.0f);
#endif
    else if (!strcmp(base_param, "AUTOLOAD_MAX"))
        AUTOLOAD_MAX_MM = clamp_i(iv, 10, 10000);
    else if (!strcmp(base_param, "LOAD_MAX"))
        LOAD_MAX_MM = clamp_i(iv, 100, 10000);
    else if (!strcmp(base_param, "UNLOAD_MAX"))
        UNLOAD_MAX_MM = clamp_i(iv, 100, 10000);
    else if (!strcmp(base_param, "UNLOAD_TENSION_BLOCK_MS"))
        UNLOAD_TENSION_BLOCK_MS = clamp_i(iv, 0, 60000);
    else if (!strcmp(base_param, "SYNC_KP_RATE"))
        SYNC_KP_SPS = clamp_i(mm_per_min_to_sps(fv), 0, 50000);
    else if (!strcmp(base_param, "KD_PSF"))
        KD_PSF = clamp_f(fv, 0.0f, 100.0f);
    else if (!strcmp(base_param, "SYNC_PSF_SLEW_PER_MM"))
        SYNC_PSF_SLEW_PER_MM = clamp_f(fv, 1.0f, 50000.0f);
    else if (!strcmp(base_param, "SYNC_PSF_FILTER_MM"))
        SYNC_PSF_FILTER_MM = clamp_f(fv, 0.1f, 500.0f);
    else if (!strcmp(base_param, "SYNC_PSF_DECAY_SPS_PER_S"))
        SYNC_PSF_DECAY_SPS_PER_S = clamp_f(fv, 0.0f, 200000.0f);
    else if (!strcmp(base_param, "PSF_STAB_STAGNANT_MS"))
        PSF_STAB_STAGNANT_MS = (iv < 0) ? 0 : iv;
    else if (!strcmp(base_param, "PSF_STAB_STAGNANT_NORM"))
        PSF_STAB_STAGNANT_NORM = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "PSF_STAB_RAIL_BREAK_MS"))
        PSF_STAB_RAIL_BREAK_MS = (iv < 0) ? 0 : iv;
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "SYNC_OVERSHOOT_PCT"))
        SYNC_OVERSHOOT_PCT = clamp_i(iv, 0, 200);
#endif
    else if (!strcmp(base_param, "SYNC_RESERVE_PCT"))
        SYNC_RESERVE_PCT = clamp_i(iv, 0, 150);
    else if (!strcmp(base_param, "COMPRESSION_BIAS_FRAC")) {
        SYNC_COMPRESSION_BIAS_FRAC = clamp_f(fv, 0.0f, 0.7f);
        flow_schedule_refresh_scalar();
    } else if (!strcmp(base_param, "SYNC_AUTO_STOP"))
        SYNC_AUTO_STOP_MS = clamp_i(iv, 0, 30000);
    else
        return CMD_SET_UNHANDLED;
    return CMD_SET_HANDLED;
}

static bool cmd_set_sync_advanced_params(const char *base_param, int iv, float fv) {
#ifdef FLARE_DEV_TUNING
    if (!strcmp(base_param, "NEUTRAL_CREEP_TIMEOUT_MS"))
        NEUTRAL_CREEP_TIMEOUT_MS = clamp_i(iv, 0, 60000);
    else if (!strcmp(base_param, "NEUTRAL_CREEP_RATE") ||
             !strcmp(base_param, "NEUTRAL_CREEP_RATE_SPS_PER_S"))
        NEUTRAL_CREEP_RATE_SPS_PER_S = clamp_i(iv, 0, 1000);
    else if (!strcmp(base_param, "NEUTRAL_CREEP_CAP") ||
             !strcmp(base_param, "NEUTRAL_CREEP_CAP_FRAC"))
        NEUTRAL_CREEP_CAP_FRAC = clamp_i(iv, 0, 100);
    else if (!strcmp(base_param, "VAR_BLEND_FRAC") ||
             !strcmp(base_param, "BUF_VARIANCE_BLEND_FRAC"))
        BUF_VARIANCE_BLEND_FRAC = clamp_f(fv, 0.0f, 0.9f);
    else if (!strcmp(base_param, "VAR_BLEND_REF_MM") ||
             !strcmp(base_param, "BUF_VARIANCE_BLEND_REF_MM"))
        BUF_VARIANCE_BLEND_REF_MM = clamp_f(fv, 0.5f, 5.0f);
    else if (!strcmp(base_param, "SYNC_TENSION_STOP_MS"))
        SYNC_TENSION_DWELL_STOP_MS = clamp_i(iv, 0, 30000);
    else if (!strcmp(base_param, "SYNC_TENSION_RAMP_MS"))
        SYNC_TENSION_RAMP_DELAY_MS = clamp_i(iv, 0, 5000);
    else if (!strcmp(base_param, "SYNC_OVERSHOOT_NEUTRAL_EXT"))
        SYNC_OVERSHOOT_NEUTRAL_EXTEND = clamp_i(iv, 0, 1);
    else if (!strcmp(base_param, "SYNC_INT_GAIN"))
        SYNC_RESERVE_INTEGRAL_GAIN = clamp_f(fv, 0.0f, 0.05f);
    else if (!strcmp(base_param, "SYNC_INT_CLAMP"))
        SYNC_RESERVE_INTEGRAL_CLAMP_MM = clamp_f(fv, 0.0f, 2.0f);
    else if (!strcmp(base_param, "SYNC_INT_DECAY_MS"))
        SYNC_RESERVE_INTEGRAL_DECAY_MS = clamp_i(iv, 0, 60000);
    else if (!strcmp(base_param, "EST_SIGMA_CAP"))
        EST_SIGMA_HARD_CAP_MM = clamp_f(fv, 0.5f, 5.0f);
    else if (!strcmp(base_param, "EST_LOW_CF_THR"))
        EST_LOW_CF_WARN_THRESHOLD = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "EST_FALLBACK_THR"))
        EST_FALLBACK_CF_THRESHOLD = clamp_f(fv, 0.0f, 0.5f);
    else
#endif
        return false;
    return true;
}

static bool cmd_set_sync_relay_probe_params(const char *base_param, int iv, float fv) {
    if (!strcmp(base_param, "RELAY_CATCHUP_FRAC"))
        RELAY_CATCHUP_FRAC = clamp_f(fv, 0.5f, 3.0f);
    else if (!strcmp(base_param, "RELAY_NEUTRAL_FRAC"))
        RELAY_NEUTRAL_FRAC = clamp_f(fv, 0.5f, 3.0f);
    else if (!strcmp(base_param, "SYNC_RELAY_TRIM_STEP_SPS"))
        SYNC_RELAY_TRIM_STEP_SPS = clamp_i(iv, 0, 5000);
    else if (!strcmp(base_param, "SYNC_RELAY_TRIM_CLAMP_SPS"))
        SYNC_RELAY_TRIM_CLAMP_SPS = clamp_i(iv, 0, 50000);
    else if (!strcmp(base_param, "SYNC_COMPRESSION_DRAIN_FRAC"))
        SYNC_COMPRESSION_DRAIN_FRAC = clamp_f(fv, 0.0f, 0.9f);
    else if (!strcmp(base_param, "SYNC_COMPRESSION_DRAIN_BUDGET_MM"))
        SYNC_COMPRESSION_DRAIN_BUDGET_MM = clamp_f(fv, 0.0f, 25.0f);
    else if (!strcmp(base_param, "SYNC_EST_ATTACK_ALPHA"))
        SYNC_EST_ATTACK_ALPHA = clamp_f(fv, 0.65f, 1.0f);
    else if (!strcmp(base_param, "SYNC_TENSION_FAST_MM_S"))
        SYNC_TENSION_FAST_MM_S = clamp_f(fv, 1.0f, 200.0f);
    else if (!strcmp(base_param, "SYNC_TENSION_PROBE_MAX"))
        SYNC_TENSION_PROBE_MAX_SPS = clamp_i(mm_per_min_to_sps(fv), 0, mm_per_min_to_sps(6000.0f));
    else if (!strcmp(base_param, "SYNC_TENSION_PROBE_UP"))
        SYNC_TENSION_PROBE_UP_SPS_PER_S =
            clamp_i(mm_per_min_to_sps(fv), 0, mm_per_min_to_sps(12000.0f));
    else if (!strcmp(base_param, "SYNC_TENSION_PROBE_DOWN"))
        SYNC_TENSION_PROBE_DOWN_SPS_PER_S =
            clamp_i(mm_per_min_to_sps(fv), 0, mm_per_min_to_sps(12000.0f));
    else if (!strcmp(base_param, "SYNC_TENSION_PROBE_NEUTRAL"))
        SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S =
            clamp_i(mm_per_min_to_sps(fv), 0, mm_per_min_to_sps(12000.0f));
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "RELAY_MIN_FLIP_MM"))
        RELAY_MIN_FLIP_MM = clamp_f(fv, 0.0f, 100.0f);
    else if (!strcmp(base_param, "RELAY_COLLAPSE_DELAY_MS"))
        RELAY_COLLAPSE_DELAY_MS = clamp_i(iv, 0, 5000);
    else if (!strcmp(base_param, "RELAY_COLLAPSE_RAMP_MULT"))
        RELAY_COLLAPSE_RAMP_MULT = clamp_i(iv, 1, 16);
    else if (!strcmp(base_param, "RELAY_COLLAPSE_CAP_MS"))
        RELAY_COLLAPSE_CAP_MS = clamp_i(iv, 0, 5000);
    else if (!strcmp(base_param, "BUF_DRIFT_TAU_MS"))
        BUF_DRIFT_EWMA_TAU_MS = clamp_i(iv, 5000, 600000);
    else if (!strcmp(base_param, "BUF_DRIFT_MIN_SMP"))
        BUF_DRIFT_MIN_SAMPLES = clamp_i(iv, 1, 32);
    else if (!strcmp(base_param, "BUF_DRIFT_THR_MM"))
        BUF_DRIFT_APPLY_THR_MM = clamp_f(fv, 0.0f, 5.0f);
    else if (!strcmp(base_param, "BUF_DRIFT_CLAMP"))
        BUF_DRIFT_CLAMP_MM = clamp_f(fv, 0.0f, BUF_DRIFT_CLAMP_LIMIT_MM);
    else if (!strcmp(base_param, "BUF_DRIFT_MIN_CF"))
        BUF_DRIFT_APPLY_MIN_CF = clamp_f(fv, 0.0f, 1.0f);
    else if (!strcmp(base_param, "TENSION_RISK_WINDOW"))
        TENSION_RISK_WINDOW_MS = clamp_i(iv, 5000, 300000);
    else if (!strcmp(base_param, "TENSION_RISK_THR"))
        TENSION_RISK_THRESHOLD = clamp_i(iv, 0, 1000);
    else if (!strcmp(base_param, "TS_BUF_MS"))
        TS_BUF_FALLBACK_MS = clamp_i(iv, 0, 30000);
    else if (!strcmp(base_param, "STARTUP_MS"))
        MOTION_STARTUP_MS = clamp_i(iv, 0, 30000);
#endif
    else
        return false;
    return true;
}

static bool cmd_set_cutter_params(const char *base_param, int iv, float fv) {
    if (!strcmp(base_param, "SERVO_OPEN"))
        SERVO_OPEN_US = clamp_i(iv, 400, 2700);
    else if (!strcmp(base_param, "SERVO_CLOSE"))
        SERVO_CLOSE_US = clamp_i(iv, 400, 2700);
    else if (!strcmp(base_param, "SERVO_BLOCK"))
        SERVO_BLOCK_US = clamp_i(iv, 400, 2700);
    else if (!strcmp(base_param, "SERVO_SETTLE") || !strcmp(base_param, "SERVO_SETTLE_MS"))
        SERVO_SETTLE_MS = clamp_i(iv, 100, 2000);
    else if (!strcmp(base_param, "UNLOAD_CUT"))
        UNLOAD_CUT = (iv == 1);
    else if (!strcmp(base_param, "CUT_FEED_RATE"))
        CUT_FEED_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 100, 30000));
    else if (!strcmp(base_param, "CUT_FEED"))
        CUT_FEED_MM = clamp_i(iv, 1, 200);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "CUT_FEED_MS"))
        CUT_TIMEOUT_FEED_MS = clamp_i(iv, 1000, 120000);
    else if (!strcmp(base_param, "CUT_SETTLE_MS"))
        CUT_TIMEOUT_SETTLE_MS = clamp_i(iv, 500, 10000);
#endif
    else if (!strcmp(base_param, "CUT_LEN"))
        CUT_LENGTH_MM = clamp_i(iv, 1, 50);
    else if (!strcmp(base_param, "CUT_AMT"))
        CUT_AMOUNT = clamp_i(iv, 1, 5);
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "TC_CUT_MS"))
        TC_TIMEOUT_CUT_MS = clamp_i(iv, 1000, 30000);
    else if (!strcmp(base_param, "TC_TH_MS"))
        TC_TIMEOUT_TH_MS = clamp_i(iv, 0, 10000);
    else if (!strcmp(base_param, "TC_Y_MS"))
        TC_TIMEOUT_Y_MS = clamp_i(iv, 0, 30000);
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
    char val_str[32];
    if (sscanf(p, "%63[^:]:%31s", param, val_str) != 2) {
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

static bool cmd_handle_cutter(const char *cmd, const char *p, uint32_t now_ms) {
    if (!strcmp(cmd, "CU")) {
        if (!ENABLE_CUTTER) {
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
            cmd_reply("ER", "BUSY");
            return true;
        }
        sync_retract_assist_set(false);
        cutter_start(lane, true, now_ms);
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "CX")) {
        if (!ENABLE_CUTTER) {
            cmd_reply("ER", "CUTTER_DISABLED");
            return true;
        }
        sync_retract_assist_set(false);
        cutter_start(NULL, false, now_ms);
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "CP")) {
        if (!ENABLE_CUTTER) {
            cmd_reply("ER", "CUTTER_DISABLED");
            return true;
        }
        int us = atoi(p);
        if (us < 400 || us > 2700) {
            cmd_reply("ER", "ARG");
            return true;
        }
        sync_retract_assist_set(false);
        cutter_test_us(us);
        cmd_reply("OK", NULL);
        return true;
    }
    return false;
}

static bool cmd_handle_unload(const char *cmd, const char *p, uint32_t now_ms) {
    if (!strcmp(cmd, "UL")) {
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
        if (ENABLE_CUTTER && UNLOAD_CUT) {
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

        bool active_target = !explicit_lane || target_lane == active_lane;
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
                if (ENABLE_CUTTER && UNLOAD_CUT && out_present_at_entry) {
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
                cmd_reply("ER", "BUSY");
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
    char dir_tok[8] = {0};
    char ignore_tok[8] = {0};
    int n = sscanf(p, "%f:%f:%7[^:]:%7s", &mm, &feed_mm_min, dir_tok, ignore_tok);
    if ((n < 2 || n > 4) || feed_mm_min <= 0.0f) {
        cmd_reply("ER", "ARG");
        return true;
    }
    int idx = lane_to_idx(active_lane);
    int sps = (int)(feed_mm_min / 60.0f / MM_PER_STEP[idx] + 0.5f);
    if (sps < 200)
        sps = 200;
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
        lane_t *lane = get_active_lane_and_clear_error();
        if (!lane)
            return true;
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        lane_start(lane, TASK_AUTOLOAD, AUTO_SPS, true, now_ms, (float)AUTOLOAD_MAX_MM);
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "FL")) {
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
        lane_t *other = lane_ptr(other_lane(active_lane));
        if (other && lane_out_present(other) && other->task == TASK_IDLE) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return true;
        }
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        set_toolhead_filament(false);
        lane_start(lane, TASK_LOAD_FULL, FEED_SPS, true, now_ms, (float)LOAD_MAX_MM);
        cmd_reply("OK", NULL);
        return true;
    } else if (!strcmp(cmd, "RL")) {
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
        lane_t *other = lane_ptr(other_lane(active_lane));
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
        int ln = atoi(p);
        if (ln == 1 || ln == 2) {
            if (active_lane != 1 && active_lane != 2) {
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
                    lane_t *_Aold = lane_ptr(active_lane);
                    if (_Aold && _Aold->task == TASK_FEED)
                        lane_stop(_Aold);
                }
                sync_disable(false);
                set_active_lane(ln);
                cmd_reply("OK", NULL);
            } else {
                sync_retract_assist_set(false);
                set_active_lane(ln);
                cmd_reply("OK", NULL);
            }
        } else {
            cmd_reply("ER", "ARG");
        }
        return true;
    } else if (!strcmp(cmd, "FD")) {
        lane_t *lane = get_active_lane_and_clear_error();
        if (!lane)
            return true;
        /* Double-load guard: do not feed the active lane into a hub already
           occupied by the other lane. MV: stays unguarded for raw recovery. */
        if (on_al(&g_y_split) && !lane_out_present(lane)) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return true;
        }
        lane_t *other = lane_ptr(other_lane(active_lane));
        if (other && lane_out_present(other) && other->task == TASK_IDLE) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return true;
        }
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        lane_start(lane, TASK_FEED, FEED_SPS, true, now_ms, 0);
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
        cmd_reply("ER", "BUSY");
        return true;
    }
    buffer_stabilize_cancel();
    if (sync_enabled) {
        sync_disable(false);
        lane_t *lane = lane_ptr(active_lane);
        if (lane && lane->task == TASK_FEED)
            lane_stop(lane);
    } else if (g_sync_state != SYNC_OFF && g_sync_state != SYNC_RETRACT_ASSIST) {
        sync_disable(false);
    }
    if (controller_activity_in_progress()) {
        cmd_reply("ER", "BUSY");
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
            cmd_reply("ER", "BUSY");
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
            cmd_reply("ER", "BUSY");
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
                 * TENSION + AUTO_MODE on. */
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
            sync_auto_started = false;
            sync_tail_assist_active = false;
            if (v == 0)
                sync_current_sps = 0;
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
        if (!strcmp(p, "PSF_COMP")) {
            buf_analog_update();
            BUF_PSF_MAX_COMP = clamp_f(g_buf_pos_raw_status, 0.0f, 1.0f);
            settings_save();
            cmd_reply("OK", NULL);
        } else if (!strcmp(p, "PSF_TENS")) {
            buf_analog_update();
            BUF_PSF_MAX_TENS = clamp_f(g_buf_pos_raw_status, 0.0f, 1.0f);
            settings_save();
            cmd_reply("OK", NULL);
        } else if (!strcmp(p, "PSF_NEUT")) {
            buf_analog_update();
            BUF_PSF_NEUTRAL = clamp_f(g_buf_pos_raw_status, 0.0f, 1.0f);
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
        sleep_ms(100);
        reset_usb_boot(0, 0);
        return true;
    }
    return false;
}

static void cmd_execute(const char *cmd, const char *p, uint32_t now_ms) {
    if (manual_unload_active() && strcmp(cmd, "ST") && strcmp(cmd, "?") && strcmp(cmd, "GET")) {
        cmd_reply("ER", "BUSY");
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
    } else if (cmd_handle_system(cmd, p, now_ms)) {
        return;
    } else if (!strcmp(cmd, "SET")) {
        cmd_handle_set(p, now_ms);
    } else if (!strcmp(cmd, "GET")) {
        cmd_handle_get(p, now_ms);
    } else {
        cmd_reply("ER", "UNKNOWN");
    }
}

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
