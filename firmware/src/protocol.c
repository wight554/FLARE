#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"

#include "controller_shared.h"
#include "motion.h"
#include "protocol.h"
#include "settings_store.h"
#include "sync.h"
#include "toolchange.h"
#include "cutter.h"

#define CMD_POLL_BYTE_BUDGET 128
#define CMD_POLL_COMMAND_BUDGET 4
#define CMD_EVENT_WINDOW_MS 100
#define CMD_EVENT_BUDGET 8

typedef struct {
    char buf[64];
    int pos;
    bool overflow;
} cmd_parser_t;

static cmd_parser_t g_cmd = {0};
static uint32_t g_cmd_event_window_ms = 0;
static int g_cmd_event_count = 0;
static bool g_live_tune_lock = false;

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
    if (tok[0] == '\0') return true;

    if (!strcmp(tok, "F") || !strcmp(tok, "f") || !strcmp(tok, "+")) {
        *forward = true;
    } else if (!strcmp(tok, "R") || !strcmp(tok, "r") ||
               !strcmp(tok, "B") || !strcmp(tok, "b") ||
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
    bool l1 = lane_out_present(&g_lane_l1);
    bool l2 = lane_out_present(&g_lane_l2);
    if (l1 && !l2 && active_lane != 1) set_active_lane(1);
    else if (l2 && !l1 && active_lane != 2) set_active_lane(2);
}

static void manual_unload_complete(void) {
    maybe_autoselect_lane();
    manual_unload_reset();
}

static bool live_tune_locked_param(const char *param) {
    return !strcmp(param, "BASELINE_RATE") ||
           !strcmp(param, "BASELINE_SPS") ||
           !strcmp(param, "COMPRESSION_BIAS_FRAC") ||
           !strcmp(param, "NEUTRAL_CREEP_TIMEOUT_MS") ||
           !strcmp(param, "NEUTRAL_CREEP_RATE") ||
           !strcmp(param, "NEUTRAL_CREEP_RATE_SPS_PER_S") ||
           !strcmp(param, "NEUTRAL_CREEP_CAP") ||
           !strcmp(param, "NEUTRAL_CREEP_CAP_FRAC") ||
           !strcmp(param, "RELAY_CATCHUP_FRAC") ||
           !strcmp(param, "RELAY_NEUTRAL_FRAC") ||
           !strcmp(param, "RELAY_MIN_FLIP_MM") ||
           !strcmp(param, "RELAY_COLLAPSE_DELAY_MS") ||
           !strcmp(param, "RELAY_COLLAPSE_RAMP_MULT") ||
           !strcmp(param, "RELAY_COLLAPSE_CAP_MS") ||
           !strcmp(param, "VAR_BLEND_FRAC") ||
           !strcmp(param, "BUF_VARIANCE_BLEND_FRAC") ||
           !strcmp(param, "VAR_BLEND_REF_MM") ||
           !strcmp(param, "BUF_VARIANCE_BLEND_REF_MM");
}

static float buf_switch_span_half_from_full(float span_mm, int max_travel_mm) {
    float max_span_mm = (float)max_travel_mm;
    if (max_span_mm < 2.0f) max_span_mm = 2.0f;
    return clamp_f(span_mm, 2.0f, max_span_mm) * 0.5f;
}

static bool controller_activity_in_progress(void) {
    if (manual_unload_active()) return true;
    /* TC_ERROR means the TC concluded (failed) — not running. Treat as idle
     * so BS/BL/SV/LD/RS are usable for error recovery without a board reset. */
    if ((g_tc_ctx.state != TC_IDLE && g_tc_ctx.state != TC_ERROR) ||
        cutter_busy() || g_boot_stabilizing) return true;
    if (g_lane_l1.task != TASK_IDLE || g_lane_l2.task != TASK_IDLE) return true;
    return false;
}

static bool cmd_event_permitted(void) {
    if (!stdio_usb_connected()) return false;

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now_ms - g_cmd_event_window_ms) >= CMD_EVENT_WINDOW_MS) {
        g_cmd_event_window_ms = now_ms;
        g_cmd_event_count = 0;
    }

    if (g_cmd_event_count >= CMD_EVENT_BUDGET) return false;
    g_cmd_event_count++;
    return true;
}

static void cmd_write_line(const char *prefix, const char *type, const char *data, bool best_effort) {
    if (best_effort && !cmd_event_permitted()) return;

    char line[768];
    int len;
    if (data && *data) len = snprintf(line, sizeof(line), "%s%s:%s\n", prefix, type, data);
    else len = snprintf(line, sizeof(line), "%s%s\n", prefix, type);
    if (len <= 0) return;
    if (len >= (int)sizeof(line)) len = (int)sizeof(line) - 1;
    (void)stdio_put_string(line, len, false, false);
}

void cmd_reply(const char *status, const char *data) {
    cmd_write_line("", status, data, false);
}

void cmd_event(const char *type, const char *data) {
    cmd_write_line("EV:", type, data, true);
}

static const char *buf_status_label(void) {
    /* Type-P: the goal-relative zone names (TENSION/COMPRESSION/NEUTRAL) mislead
       on the status line — home sits at +1.0 and goal is compression-side, so a
       near-goal buffer reads "TENSION". Report the signed position instead; the
       magnitude is in BP. Type-D keeps its switch-state name. */
    if (BUF_SENSOR_TYPE != 0) {
        if (g_buf_pos >  0.1f) return "+";
        if (g_buf_pos < -0.1f) return "-";
        return "0";
    }
    return buf_state_name(g_buf.state);
}

static void status_dump(void) {
    int idx = (active_lane == 2) ? 1 : 0;
    flow_param_t active_flow_param = flow_param((int)extruder_est_sps);

    char b[768];
    int blen = snprintf(b, sizeof(b),
        "LN:%d,TC:%s,L1T:%s,L2T:%s,"
        "I1:%d,O1:%d,I2:%d,O2:%d,"
        "TH:%d,YS:%d,BUF:%s,MM:%.1f,BF:%.1f,BP:%.2f,SM:%d,BL:%s,ST:%d,TPR:%d,CU:%d,RELOAD:%d,UC:%d,BST:%d,"
        "EST:%.1f,RE:%.2f,AV:%.2f,SC:%.1f",
        active_lane, tc_state_name(g_tc_ctx.state),
        task_name(g_lane_l1.task), task_name(g_lane_l2.task),
        lane_in_present(&g_lane_l1) ? 1 : 0,
        lane_out_present(&g_lane_l1) ? 1 : 0,
        lane_in_present(&g_lane_l2) ? 1 : 0,
        lane_out_present(&g_lane_l2) ? 1 : 0,
        toolhead_has_filament ? 1 : 0,
        on_al(&g_y_split) ? 1 : 0,
        buf_status_label(),
        (double)sps_to_mm_per_min(sync_current_sps),
        (double)sps_to_mm_per_min(active_flow_param.baseline_sps),
        (double)g_buf_pos,
        sync_enabled ? 1 : 0,
        sync_buffer_lock_arm_str(),
        (int)g_sync_state,
        AUTO_PRELOAD ? 1 : 0,
        ENABLE_CUTTER ? 1 : 0,
        RELOAD_MODE,
        UNLOAD_CUT ? 1 : 0,
        BUF_SENSOR_TYPE,
        (double)sps_to_mm_per_min((int)extruder_est_sps),
        (double)sync_reserve_error_mm(),
        (double)g_buf.arm_vel_mm_s,
        (double)sps_to_mm_per_min_idx(TMC_STEALTHCHOP_SPS[idx], idx));

    if (blen > 0 && blen < (int)sizeof(b)) {
        uint32_t now_ms = g_now_ms;
        uint32_t ad_ms = sync_tension_dwell_ms(now_ms);
        uint32_t td_ms = (g_buf.state == BUF_COMPRESSION && g_buf.entered_ms > 0)
                         ? (now_ms - g_buf.entered_ms) : 0;
        snprintf(b + blen, sizeof(b) - (size_t)blen,
            ",RT:%.2f,TT:%u,CT:%u,SK:%u,CF:%.2f,ES:%.2f"
            ",TPX:%d,CB:%d,BPV:%d,MK:%u:%s"
            ",SYNC_REFILL_MM:%d,SYNC_RELIEVE_MM:%d,TF:%.1f,FL_RATE:%.1f,UL_RATE:%.1f",
            (double)sync_reserve_target_mm(),
            (unsigned)ad_ms,
            (unsigned)td_ms,
            (unsigned)g_buf_signal.kind,
            (double)g_buf_signal.confidence,
            (double)sync_buf_sigma_mm(),
            sync_tension_pin_window_count(now_ms),
            (active_flow_param.bias_milli + 5) / 10,
            (int)(g_buf_pos * 100.0f),
            g_marker_seq,
            g_marker_tag,
            (int)g_sync_refill_effort_mm,
            (int)g_sync_relieve_effort_mm,
            (double)g_sync_mmu_total_mm,
            (double)sps_to_mm_per_min_idx(FEED_SPS, idx),
            (double)sps_to_mm_per_min_idx(REV_SPS, idx));
    }

    cmd_reply("OK", b);
}

static lane_t* get_active_lane_and_clear_error(void) {
    if (g_tc_ctx.state == TC_ERROR) tc_abort();
    lane_t *A = lane_ptr(active_lane);
    if (!A) { cmd_reply("ER", "NO_ACTIVE_LANE"); return NULL; }
    return A;
}

static bool parse_optional_lane_payload(const char *p, int *lane_out, bool *explicit_lane) {
    *explicit_lane = false;
    *lane_out = active_lane;

    if (!p || p[0] == '\0') return true;
    if ((p[0] == '1' || p[0] == '2') && p[1] == '\0') {
        *explicit_lane = true;
        *lane_out = p[0] - '0';
        return true;
    }
    return false;
}

static void start_manual_unload_lane(lane_t *A, bool suppress_event, uint32_t now_ms) {
    A->unload_to_in = false;
    A->unload_buf_recover_done = false;
    lane_start(A, TASK_UNLOAD, REV_SPS, false, now_ms, (float)UNLOAD_MAX_MM);
    A->suppress_unloaded_event = suppress_event;
}

static void start_manual_unload_to_in(lane_t *A, uint32_t now_ms) {
    A->unload_to_in = true;
    A->unload_buf_recover_done = false;
    lane_start(A, TASK_UNLOAD, REV_SPS, false, now_ms, (float)UNLOAD_MAX_MM);
}



static void manual_unload_tick(uint32_t now_ms) {
    lane_t *A = g_manual_unload.lane;
    if (!A) {
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
                    start_manual_unload_lane(A, g_manual_unload.finish_to_in, now_ms);
                    if (g_manual_unload.finish_to_in) {
                        g_manual_unload.state = MANUAL_UNLOAD_WAIT_FIRST_CLEAR;
                    } else {
                        manual_unload_complete();
                    }
                }
            }
            break;

        case MANUAL_UNLOAD_WAIT_FIRST_CLEAR:
            if (A->task == TASK_IDLE) {
                if (lane_out_present(A)) {
                    manual_unload_reset();
                    return;
                }
                if (g_manual_unload.cut_pending) {
                    if (on_al(&g_y_split)) {
                        g_manual_unload.cut_pending = false;
                        if (g_manual_unload.finish_to_in) {
                            start_manual_unload_to_in(A, now_ms);
                            g_manual_unload.state = MANUAL_UNLOAD_WAIT_IN_CLEAR;
                        } else {
                            manual_unload_complete();
                        }
                    } else {
                        cutter_start(A, true, now_ms);
                        g_manual_unload.state = MANUAL_UNLOAD_WAIT_CUT;
                    }
                } else if (g_manual_unload.finish_to_in) {
                    start_manual_unload_to_in(A, now_ms);
                    g_manual_unload.state = MANUAL_UNLOAD_WAIT_IN_CLEAR;
                } else {
                    manual_unload_complete();
                }
            }
            break;

        case MANUAL_UNLOAD_WAIT_IN_CLEAR:
            if (A->task == TASK_IDLE) {
                manual_unload_complete();
            }
            break;
    }
}

static void cmd_execute(const char *cmd, const char *p, uint32_t now_ms) {
    if (manual_unload_active() &&
        strcmp(cmd, "ST") && strcmp(cmd, "?") && strcmp(cmd, "GET")) {
        cmd_reply("ER", "BUSY");
        return;
    }

    if (!strcmp(cmd, "TC")) {
        int ln = atoi(p);
        if (ln == 1 || ln == 2) {
            if (active_lane != 1 && active_lane != 2) {
                cmd_reply("ER", "NO_ACTIVE_LANE");
                return;
            }
            /* Double-load guard: if both OUT sensors are triggered the hub is
               stuck with two filaments.  Reject TC before touching anything so
               the caller can use T: to select a lane and UL: to clear it. */
            if (lane_out_present(&g_lane_l1) && lane_out_present(&g_lane_l2)) {
                cmd_reply("ER", "DOUBLE_LOAD");
                return;
            }
            sync_retract_assist_set(false);
            sync_set_state(SYNC_OFF);
            tc_start(ln, now_ms);
            cmd_reply("OK", NULL);
        } else {
            cmd_reply("ER", "ARG");
        }
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
                    if (_Aold && _Aold->task == TASK_FEED) lane_stop(_Aold);
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
    } else if (!strcmp(cmd, "LO")) {
        lane_t *A = get_active_lane_and_clear_error();
        if (!A) return;
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        lane_start(A, TASK_AUTOLOAD, AUTO_SPS, true, now_ms, (float)AUTOLOAD_MAX_MM);
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "UL")) {
        lane_t *A = get_active_lane_and_clear_error();
        if (!A) return;
        if (!lane_out_present(A)) { cmd_reply("ER", "NOT_LOADED"); return; }
        sync_retract_assist_set(false);
        sync_bl_clear_autostart_suppress();
        sync_set_state(SYNC_OFF);
        sync_disable(false);
        set_toolhead_filament(false);
        if (ENABLE_CUTTER && UNLOAD_CUT) {
            g_manual_unload.lane = A;
            g_manual_unload.finish_to_in = false;
            g_manual_unload.cut_pending = true;
            g_manual_unload.state = MANUAL_UNLOAD_WAIT_FIRST_CLEAR;
            start_manual_unload_lane(A, true, now_ms);
        } else {
            start_manual_unload_lane(A, false, now_ms);
        }
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "UM")) {
        int target_lane = 0;
        bool explicit_lane = false;
        if (!parse_optional_lane_payload(p, &target_lane, &explicit_lane)) {
            cmd_reply("ER", "ARG");
            return;
        }

        bool active_target = !explicit_lane || target_lane == active_lane;
        lane_t *A = active_target ? get_active_lane_and_clear_error() : lane_ptr(target_lane);
        if (!A) { cmd_reply("ER", "ARG"); return; }

        if (active_target) {
            if (!lane_in_present(A)) { cmd_reply("ER", "NOT_LOADED"); return; }
            sync_retract_assist_set(false);
            sync_bl_clear_autostart_suppress();
            sync_set_state(SYNC_OFF);
            sync_disable(false);
            set_toolhead_filament(false);
            bool out_present_at_entry = lane_out_present(A);
            if (out_present_at_entry || on_al(&g_y_split)) {
                if (ENABLE_CUTTER && UNLOAD_CUT && out_present_at_entry) {
                    g_manual_unload.lane = A;
                    g_manual_unload.finish_to_in = true;
                    g_manual_unload.cut_pending = true;
                    g_manual_unload.state = MANUAL_UNLOAD_WAIT_FIRST_CLEAR;
                    start_manual_unload_lane(A, true, now_ms);
                } else {
                    start_manual_unload_lane(A, true, now_ms);
                    g_manual_unload.lane = A;
                    g_manual_unload.finish_to_in = true;
                    g_manual_unload.cut_pending = false;
                    g_manual_unload.state = MANUAL_UNLOAD_WAIT_FIRST_CLEAR;
                }
            } else {
                start_manual_unload_to_in(A, now_ms);
            }
        } else {
            if (g_tc_ctx.state != TC_IDLE || A->task != TASK_IDLE) { cmd_reply("ER", "BUSY"); return; }
            if (!lane_in_present(A)) { cmd_reply("ER", "NOT_LOADED"); return; }
            if (lane_out_present(A)) { cmd_reply("ER", "NOT_PRELOADED"); return; }
            start_manual_unload_to_in(A, now_ms);
        }
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "FL")) {
        lane_t *A = get_active_lane_and_clear_error();
        if (!A) return;
        if (!lane_in_present(A)) { cmd_reply("ER", "NO_FILAMENT"); return; }
        if (on_al(&g_y_split) && !lane_out_present(A)) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return;
        }
        lane_t *other = lane_ptr(other_lane(active_lane));
        if (other && lane_out_present(other) && other->task == TASK_IDLE) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return;
        }
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        set_toolhead_filament(false);
        lane_start(A, TASK_LOAD_FULL, FEED_SPS, true, now_ms, (float)LOAD_MAX_MM);
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "RL")) {
        lane_t *A = get_active_lane_and_clear_error();
        if (!A) return;
        if (!lane_in_present(A)) { cmd_reply("ER", "NO_FILAMENT"); return; }
        if (on_al(&g_y_split) && !lane_out_present(A)) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return;
        }
        lane_t *other = lane_ptr(other_lane(active_lane));
        if (other && lane_out_present(other) && other->task == TASK_IDLE) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return;
        }
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        tc_manual_reload(now_ms);
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "FD")) {
        lane_t *A = get_active_lane_and_clear_error();
        if (!A) return;
        /* Double-load guard: do not feed the active lane into a hub already
           occupied by the other lane. MV: stays unguarded for raw recovery. */
        if (on_al(&g_y_split) && !lane_out_present(A)) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return;
        }
        lane_t *other = lane_ptr(other_lane(active_lane));
        if (other && lane_out_present(other) && other->task == TASK_IDLE) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return;
        }
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        lane_start(A, TASK_FEED, FEED_SPS, true, now_ms, 0);
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "CU")) {
        if (!ENABLE_CUTTER) {
            cmd_reply("ER", "CUTTER_DISABLED");
            return;
        }
        lane_t *A = get_active_lane_and_clear_error();
        if (!A) return;
        lane_t *B = (A == &g_lane_l1) ? &g_lane_l2 : &g_lane_l1;
        if (!lane_in_present(A)) {
            cmd_reply("ER", "NO_FILAMENT");
            return;
        }
        if (lane_out_present(B)) {
            cmd_reply("ER", "OTHER_LANE_ACTIVE");
            return;
        }
        if (A->task != TASK_IDLE || g_tc_ctx.state != TC_IDLE) {
            cmd_reply("ER", "BUSY");
            return;
        }
        sync_retract_assist_set(false);
        cutter_start(A, true, now_ms);
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "CX")) {
        if (!ENABLE_CUTTER) {
            cmd_reply("ER", "CUTTER_DISABLED");
            return;
        }
        sync_retract_assist_set(false);
        cutter_start(NULL, false, now_ms);
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "CP")) {
        if (!ENABLE_CUTTER) {
            cmd_reply("ER", "CUTTER_DISABLED");
            return;
        }
        int us = atoi(p);
        if (us < 400 || us > 2700) {
            cmd_reply("ER", "ARG");
            return;
        }
        sync_retract_assist_set(false);
        cutter_test_us(us);
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "MV")) {
        lane_t *A = get_active_lane_and_clear_error();
        if (!A) return;
        float mm = 0.0f;
        float feed_mm_min = 0.0f;
        char dir_tok[8] = {0};
        char ignore_tok[8] = {0};
        int n = sscanf(p, "%f:%f:%7[^:]:%7s", &mm, &feed_mm_min, dir_tok, ignore_tok);
        if ((n < 2 || n > 4) || feed_mm_min <= 0.0f) {
            cmd_reply("ER", "ARG");
            return;
        }
        int idx = lane_to_idx(active_lane);
        int sps = (int)(feed_mm_min / 60.0f / MM_PER_STEP[idx] + 0.5f);
        if (sps < 200) sps = 200;
        sps = motion_clamp_rate_sps(sps);

        bool forward = (mm >= 0.0f);
        bool ignore_buffer = false;

        if ((n >= 3 && !parse_mv_option(dir_tok, &forward, &ignore_buffer)) ||
            (n == 4 && !parse_mv_option(ignore_tok, &forward, &ignore_buffer))) {
            cmd_reply("ER", "ARG");
            return;
        }

        float limit = mm < 0.0f ? -mm : mm;
        if (limit <= 0.0f) {
            cmd_reply("ER", "ARG");
            return;
        }

        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        sync_disable(false);
        lane_start(A, TASK_MOVE, sps, forward, now_ms, limit);
        A->move_ignore_buffer = ignore_buffer;
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "ST")) {
        tc_abort();
        cutter_abort();
        manual_unload_reset();
        sync_retract_assist_set(false);
        sync_set_state(SYNC_OFF);
        sync_disable(false);
        stop_all();
        set_toolhead_filament(false);
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "BS")) {
        if (controller_activity_in_progress() || sync_enabled) {
            cmd_reply("ER", "BUSY");
            return;
        }
        /* Always force a full stabilize. If buffer-lock is active, release
         * it first AND clear the BL auto-start suppression so the stabilize
         * can drive the buffer back to NEUTRAL even when raw is still
         * pinned at the armed extreme (e.g. filament parked at TENSION
         * after a tip-form unload move with no external force to push it
         * off the switch). Without clearing suppression the buffer can
         * stay stuck and sync never re-engages. */
        if (g_sync_state == SYNC_RETRACT_ASSIST) {
            sync_retract_assist_set(false);
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
            return;
        }
        cmd_reply("OK", NULL);
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
    } else if (!strcmp(cmd, "RA")) {
        /* Legacy RA command removed; BL replaces it (task 4.3). */
        cmd_reply("ER", "CMD");
    } else if (!strcmp(cmd, "BL")) {
        /* Buffer-lock arm command.
         *   BL or BL:T → arm TENSION (no follow-on)
         *   BL:C → arm COMPRESSION (no follow-on)
         *   BL:T:<follow_mm>:<follow_rate_mmpm> → arm TENSION + follow-on
         *   BL:C:<follow_mm>:<follow_rate_mmpm> → arm COMPRESSION + follow-on
         * Follow-on fires concurrent MMU motion (prime direction) on the
         * first raw transition off the armed extreme — i.e. the moment
         * the extruder starts filling the buffer. Mass-balances long
         * extruder retracts that exceed the buffer's mechanical headroom.
         * BL is allowed to take over from SYNC_ACTIVE. Reject only if a
         * non-sync task (FL/UL/MV/AUTOLOAD/TC/cutter/manual_unload) is
         * running. */
        if (controller_activity_in_progress()) {
            cmd_reply("ER", "BUSY");
        } else {
            char dir_tok = 'T';
            float follow_mm = 0.0f;
            float follow_rate = 0.0f;
            int n = sscanf(p, "%c:%f:%f", &dir_tok, &follow_mm, &follow_rate);
            if (n < 1) dir_tok = 'T';
            if (dir_tok != 'T' && dir_tok != 'C') {
                cmd_reply("ER", "ARG");
                return;
            }
            if (n == 2 || (n == 3 && (follow_mm <= 0.0f || follow_rate <= 0.0f))) {
                cmd_reply("ER", "ARG");
                return;
            }
            if (sync_enabled) sync_disable(false);
            buf_state_t target = (dir_tok == 'C') ? BUF_COMPRESSION : BUF_TENSION;
            sync_buffer_lock_arm(target, follow_mm, follow_rate, now_ms);
            cmd_reply("OK", NULL);
        }
    } else if (!strcmp(cmd, "SM")) {
        int v = atoi(p);
        if (v == 0 || v == 1) {
            if (v == 1) sync_set_state(SYNC_ACTIVE); else sync_disable(false);
            sync_auto_started = false;
            sync_tail_assist_active = false;
            if (v == 0) sync_current_sps = 0;
            cmd_reply("OK", NULL);
        } else {
            cmd_reply("ER", "ARG");
        }
    } else if (!strcmp(cmd, "CA")) {
        int ln = 0;
        int ma = 0;
        if (sscanf(p, "%d:%d", &ln, &ma) == 2 && (ln == 1 || ln == 2) && ma >= 0 && ma <= 2000) {
            tmc_t *t = (ln == 1) ? &g_tmc_l1 : &g_tmc_l2;
            int idx = lane_to_idx(ln);
            if (tmc_set_run_current_ma(t, ma, TMC_HOLD_CURRENT_MA[idx])) {
                TMC_RUN_CURRENT_MA[idx] = ma;
                g_shadow_vsense[idx] = (ma <= 980);
                g_shadow_ihold_irun[idx] = build_ihold_irun_reg(TMC_RUN_CURRENT_MA[idx], TMC_HOLD_CURRENT_MA[idx], g_shadow_vsense[idx]);
                g_shadow_ihold_irun_valid[idx] = true;
                cmd_reply("OK", NULL);
            } else {
                cmd_reply("ER", "CA:NO_RESPONSE");
            }
        } else {
            cmd_reply("ER", "ARG");
        }
    } else if (!strcmp(cmd, "CAL")) {
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
    } else if (!strcmp(cmd, "SV")) {
        if (controller_activity_in_progress()) {
            cmd_reply("ER", "PERSIST_BUSY");
            return;
        }
        settings_save();
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "LD")) {
        if (controller_activity_in_progress()) {
            cmd_reply("ER", "PERSIST_BUSY");
            return;
        }
        settings_load();
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "RS")) {
        if (controller_activity_in_progress()) {
            cmd_reply("ER", "PERSIST_BUSY");
            return;
        }
        settings_defaults();
        settings_save();
        cmd_reply("OK", NULL);
    } else if (!strcmp(cmd, "VR")) {
        cmd_reply("OK", CONF_FW_VERSION);
    } else if (!strcmp(cmd, "?")) {
        status_dump();
    } else if (!strcmp(cmd, "MARK")) {
        size_t n = strlen(p);
        if (n >= sizeof(g_marker_tag)) n = sizeof(g_marker_tag) - 1;
        memcpy(g_marker_tag, p, n);
        g_marker_tag[n] = '\0';
        g_marker_seq++;
        cmd_reply("OK", "MARK");
    } else if (!strcmp(cmd, "SET")) {
        char param[32];
        char val_str[32];
        if (sscanf(p, "%31[^:]:%31s", param, val_str) != 2) {
            cmd_reply("ER", "SET:ARG");
            return;
        }
        int iv = atoi(val_str);
        float fv = (float)atof(val_str);
        bool handled = true;

        int lane_mask = 3;
        char base_param[32];
        strncpy(base_param, param, 32);
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

        #define SET_LANE(BLOCK) for(int l=1; l<=NUM_LANES; l++) if(lane_mask & (1<<(l-1))) { int idx=l-1; BLOCK; sync_tmc_settings(l); }

        if (!strcmp(base_param, "FEED_RATE")) FEED_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
        else if (!strcmp(base_param, "REV_RATE")) REV_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
        else if (!strcmp(base_param, "AUTO_RATE")) AUTO_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
        else if (!strcmp(base_param, "SYNC_MAX_RATE")) SYNC_MAX_SPS = sync_clamp_max_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
        else if (!strcmp(base_param, "GLOBAL_MAX_RATE")) {
            GLOBAL_MAX_SPS = clamp_i(mm_per_min_to_sps(fv), mm_per_min_to_sps(1000.0f), mm_per_min_to_sps(12000.0f));
        }
        else if (!strcmp(base_param, "SYNC_MIN_RATE")) SYNC_MIN_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 0, 50000));
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "SYNC_RAMP_ACCEL")) {
            float tick_s = (float)SYNC_TICK_MS / 1000.0f;
            int sps = (int)(fv * tick_s / MM_PER_STEP[0] + 0.5f);
            SYNC_RAMP_UP_SPS = motion_clamp_rate_sps(clamp_i(sps, 1, 50000));
        }
        else if (!strcmp(base_param, "SYNC_RAMP_DECEL")) {
            float tick_s = (float)SYNC_TICK_MS / 1000.0f;
            int sps = (int)(fv * tick_s / MM_PER_STEP[0] + 0.5f);
            SYNC_RAMP_DN_SPS = motion_clamp_rate_sps(clamp_i(sps, 1, 50000));
        }
        else if (!strcmp(base_param, "SYNC_TICK_MS")) SYNC_TICK_MS = clamp_i(iv, 1, 1000);
        else if (!strcmp(base_param, "GLOBAL_MAX_ACCEL")) {
            float tick_s = (float)RAMP_TICK_MS / 1000.0f;
            int sps = (int)(fv * tick_s / MM_PER_STEP[0] + 0.5f);
            RAMP_STEP_SPS = motion_clamp_rate_sps(clamp_i(sps, 1, 30000));
        }
        else if (!strcmp(base_param, "RAMP_TICK_MS")) RAMP_TICK_MS = clamp_i(iv, 1, 1000);
        else if (!strcmp(base_param, "PRE_RAMP_RATE")) PRE_RAMP_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 0, 50000));
#endif
        else if (!strcmp(base_param, "BUF_SWITCH_SPAN")) {
            BUF_SWITCH_SPAN_HALF_MM = buf_switch_span_half_from_full(fv, BUF_MAX_TRAVEL_MM);
        }
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "BUF_HYST")) BUF_HYST_MS = clamp_i(iv, 5, 500);
        else if (!strcmp(base_param, "BUF_PREDICT_THR_MS")) BUF_PREDICT_THR_MS = clamp_i(iv, 0, 10000);
#endif
        else if (!strcmp(base_param, "AUTO_PRELOAD")) AUTO_PRELOAD = (iv != 0);
        else if (!strcmp(base_param, "RETRACT_MM")) AUTOLOAD_RETRACT_MM = clamp_i(iv, 0, 50);
        else if (!strcmp(base_param, "CUTTER")) ENABLE_CUTTER = (iv != 0);
        else if (!strcmp(base_param, "AUTO_MODE")) AUTO_MODE = clamp_i(iv, 0, 1);
        else if (!strcmp(base_param, "RELOAD_MODE")) RELOAD_MODE = (iv != 0) ? 1 : 0;
        else if (!strcmp(base_param, "RUNOUT_COOLDOWN_MS")) RUNOUT_COOLDOWN_MS = clamp_i(iv, 0, 60000);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "POST_PRINT_STAB_MS")) POST_PRINT_STAB_DELAY_MS = clamp_i(iv, 0, 300000);
#endif
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "RELOAD_Y_MS")) RELOAD_Y_TIMEOUT_MS = clamp_i(iv, 100, 30000);
#endif
        else if (!strcmp(base_param, "RELOAD_JOIN_MS")) RELOAD_JOIN_DELAY_MS = clamp_i(iv, 0, 10000);
        else if (!strcmp(base_param, "DIST_IN_OUT")) DIST_IN_OUT = clamp_i(iv, 10, 5000);
        else if (!strcmp(base_param, "DIST_OUT_Y")) DIST_OUT_Y = clamp_i(iv, 0, 5000);
        else if (!strcmp(base_param, "DIST_Y_BUF")) DIST_Y_BUF = clamp_i(iv, 0, 5000);
        else if (!strcmp(base_param, "BUF_BODY_LEN")) BUF_BODY_LEN = clamp_i(iv, 0, 5000);
        else if (!strcmp(base_param, "BUF_MAX_TRAVEL")) {
            BUF_MAX_TRAVEL_MM = clamp_i(iv, 10, 1000);
            float max_half = (float)BUF_MAX_TRAVEL_MM * 0.5f;
            if (BUF_SWITCH_SPAN_HALF_MM > max_half) BUF_SWITCH_SPAN_HALF_MM = max_half;
            if (BUF_SWITCH_SPAN_HALF_MM < 1.0f) BUF_SWITCH_SPAN_HALF_MM = 1.0f;
        }
        else if (!strcmp(base_param, "JOIN_RATE")) JOIN_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
        else if (!strcmp(base_param, "PRESS_RATE")) PRESS_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
        else if (!strcmp(base_param, "COMPRESSION_RATE")) COMPRESSION_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 10, 10000));
        else if (!strcmp(base_param, "BUF_STAB_RATE")) BUF_STAB_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 10, 10000));
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "BASELINE_ALPHA")) g_baseline_alpha = clamp_f(fv, 0.0f, 1.0f);
        else if (!strcmp(base_param, "EST_ALPHA_MIN")) EST_ALPHA_MIN = clamp_f(fv, 0.01f, 1.0f);
        else if (!strcmp(base_param, "EST_ALPHA_MAX")) EST_ALPHA_MAX = clamp_f(fv, 0.01f, 1.0f);
        else if (!strcmp(base_param, "ZONE_BIAS_BASE")) ZONE_BIAS_BASE_SPS = clamp_i(mm_per_min_to_sps(fv), 0, 5000);
        else if (!strcmp(base_param, "ZONE_BIAS_RAMP")) ZONE_BIAS_RAMP_SPS_S = clamp_i(mm_per_min_to_sps(fv), 0, 5000);
        else if (!strcmp(base_param, "ZONE_BIAS_MAX")) ZONE_BIAS_MAX_SPS = clamp_i(mm_per_min_to_sps(fv), 0, 5000);
        else if (!strcmp(base_param, "RELOAD_LEAN")) RELOAD_LEAN_FACTOR = clamp_f(fv, 0.0f, 5.0f);
#endif
        else if (!strcmp(base_param, "RUN_CURRENT_MA")) { SET_LANE({ TMC_RUN_CURRENT_MA[idx] = clamp_i(iv, 0, 2000); }); }
        else if (!strcmp(base_param, "HOLD_CURRENT_MA")) { SET_LANE({ TMC_HOLD_CURRENT_MA[idx] = clamp_i(iv, 0, 2000); }); }
        else if (!strcmp(base_param, "MICROSTEPS")) { SET_LANE({ TMC_MICROSTEPS[idx] = clamp_i(iv, 1, 256); }); }
        else if (!strcmp(base_param, "ROTATION_DIST")) { SET_LANE({ TMC_ROTATION_DISTANCE[idx] = clamp_f(fv, 0.1f, 1000.0f); }); }
        else if (!strcmp(base_param, "GEAR_RATIO")) { SET_LANE({ TMC_GEAR_RATIO[idx] = clamp_f(fv, 0.001f, 1000.0f); }); }
        else if (!strcmp(base_param, "FULL_STEPS")) { SET_LANE({ TMC_FULL_STEPS[idx] = (iv == 400 ? 400 : 200); }); }
        else if (!strcmp(base_param, "INTERPOLATE")) { SET_LANE({ TMC_INTERPOLATE[idx] = (iv != 0); }); }
        else if (!strcmp(base_param, "STEALTHCHOP")) { SET_LANE({ TMC_STEALTHCHOP_SPS[idx] = (iv == 0) ? 0 : mm_per_min_to_sps_idx(fv, idx); }); }
        else if (!strcmp(base_param, "DRIVER_TBL")) { SET_LANE({ TMC_TBL[idx] = clamp_i(iv, 0, 3); }); }
        else if (!strcmp(base_param, "DRIVER_TOFF")) { SET_LANE({ TMC_TOFF[idx] = clamp_i(iv, 0, 15); }); }
        else if (!strcmp(base_param, "DRIVER_HSTRT")) { SET_LANE({ TMC_HSTRT[idx] = clamp_i(iv, 0, 7); }); }
        else if (!strcmp(base_param, "DRIVER_HEND")) { SET_LANE({ TMC_HEND[idx] = clamp_i(iv, -3, 12); }); }
        else if (!strcmp(base_param, "FOLLOW_MS")) { SET_LANE({ FOLLOW_TIMEOUT_MS[idx] = clamp_i(iv, 1000, 60000); }); }
        else if (!strcmp(base_param, "BASELINE_RATE")) {
            int baseline_sps = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 200, 50000));
            g_baseline_target_sps = baseline_sps;
            g_baseline_sps = baseline_sps;
            flow_schedule_refresh_scalar();
        }
        else if (!strcmp(base_param, "BASELINE_SPS")) {
            int baseline_sps = motion_clamp_rate_sps(clamp_i(iv, 200, 50000));
            g_baseline_target_sps = baseline_sps;
            g_baseline_sps = baseline_sps;
            flow_schedule_refresh_scalar();
        }
        else if (!strcmp(base_param, "BUF_SENSOR")) {
            if (sync_enabled || tc_state() != TC_IDLE ||
                    g_lane_l1.task != TASK_IDLE || g_lane_l2.task != TASK_IDLE) {
                cmd_reply("ER", "BUSY");
                return;
            }
            BUF_SENSOR_TYPE = clamp_i(iv, 0, 1);
            sync_disable(false);
        }
        else if (!strcmp(base_param, "BUF_HOME_STATE")) BUF_HOME_STATE = clamp_i(iv, 0, 2);
        else if (!strcmp(base_param, "BUF_PSF_MAX_COMP")) BUF_PSF_MAX_COMP = clamp_f(fv, 0.0f, 1.0f);
        else if (!strcmp(base_param, "BUF_PSF_MAX_TENS")) BUF_PSF_MAX_TENS = clamp_f(fv, 0.0f, 1.0f);
        else if (!strcmp(base_param, "BUF_PSF_NEUTRAL")) BUF_PSF_NEUTRAL = clamp_f(fv, 0.0f, 1.0f);
        else if (!strcmp(base_param, "BUF_GOAL")) BUF_GOAL = clamp_f(fv, 0.0f, 1.0f);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "BUF_ALPHA")) BUF_ANALOG_ALPHA = clamp_f(fv, 0.01f, 1.0f);
#endif
        else if (!strcmp(base_param, "AUTOLOAD_MAX")) AUTOLOAD_MAX_MM = clamp_i(iv, 10, 10000);
        else if (!strcmp(base_param, "LOAD_MAX")) LOAD_MAX_MM = clamp_i(iv, 100, 10000);
        else if (!strcmp(base_param, "UNLOAD_MAX")) UNLOAD_MAX_MM = clamp_i(iv, 100, 10000);
        else if (!strcmp(base_param, "UNLOAD_TENSION_BLOCK_MS")) UNLOAD_TENSION_BLOCK_MS = clamp_i(iv, 0, 60000);
        else if (!strcmp(base_param, "SYNC_KP_RATE")) SYNC_KP_SPS = clamp_i(mm_per_min_to_sps(fv), 0, 50000);
        else if (!strcmp(base_param, "KD_PSF")) KD_PSF = clamp_f(fv, 0.0f, 100.0f);
        else if (!strcmp(base_param, "SYNC_PSF_SLEW_PER_MM")) SYNC_PSF_SLEW_PER_MM = clamp_f(fv, 1.0f, 50000.0f);
        else if (!strcmp(base_param, "SYNC_PSF_FILTER_MM")) SYNC_PSF_FILTER_MM = clamp_f(fv, 0.1f, 500.0f);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "SYNC_OVERSHOOT_PCT")) SYNC_OVERSHOOT_PCT = clamp_i(iv, 0, 200);
#endif
        else if (!strcmp(base_param, "SYNC_RESERVE_PCT")) SYNC_RESERVE_PCT = clamp_i(iv, 0, 150);
        else if (!strcmp(base_param, "COMPRESSION_BIAS_FRAC")) {
            SYNC_COMPRESSION_BIAS_FRAC = clamp_f(fv, 0.0f, 0.7f);
            flow_schedule_refresh_scalar();
        }
        else if (!strcmp(base_param, "SYNC_AUTO_STOP")) SYNC_AUTO_STOP_MS = clamp_i(iv, 0, 30000);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "NEUTRAL_CREEP_TIMEOUT_MS")) NEUTRAL_CREEP_TIMEOUT_MS = clamp_i(iv, 0, 60000);
        else if (!strcmp(base_param, "NEUTRAL_CREEP_RATE") || !strcmp(base_param, "NEUTRAL_CREEP_RATE_SPS_PER_S")) NEUTRAL_CREEP_RATE_SPS_PER_S = clamp_i(iv, 0, 1000);
        else if (!strcmp(base_param, "NEUTRAL_CREEP_CAP") || !strcmp(base_param, "NEUTRAL_CREEP_CAP_FRAC")) NEUTRAL_CREEP_CAP_FRAC = clamp_i(iv, 0, 100);
        else if (!strcmp(base_param, "VAR_BLEND_FRAC") || !strcmp(base_param, "BUF_VARIANCE_BLEND_FRAC")) BUF_VARIANCE_BLEND_FRAC = clamp_f(fv, 0.0f, 0.9f);
        else if (!strcmp(base_param, "VAR_BLEND_REF_MM") || !strcmp(base_param, "BUF_VARIANCE_BLEND_REF_MM")) BUF_VARIANCE_BLEND_REF_MM = clamp_f(fv, 0.5f, 5.0f);
        else if (!strcmp(base_param, "SYNC_TENSION_STOP_MS")) SYNC_TENSION_DWELL_STOP_MS = clamp_i(iv, 0, 30000);
        else if (!strcmp(base_param, "SYNC_TENSION_RAMP_MS")) SYNC_TENSION_RAMP_DELAY_MS = clamp_i(iv, 0, 5000);
        else if (!strcmp(base_param, "SYNC_OVERSHOOT_NEUTRAL_EXT")) SYNC_OVERSHOOT_NEUTRAL_EXTEND = clamp_i(iv, 0, 1);
        else if (!strcmp(base_param, "SYNC_INT_GAIN")) SYNC_RESERVE_INTEGRAL_GAIN = clamp_f(fv, 0.0f, 0.05f);
        else if (!strcmp(base_param, "SYNC_INT_CLAMP")) SYNC_RESERVE_INTEGRAL_CLAMP_MM = clamp_f(fv, 0.0f, 2.0f);
        else if (!strcmp(base_param, "SYNC_INT_DECAY_MS")) SYNC_RESERVE_INTEGRAL_DECAY_MS = clamp_i(iv, 0, 60000);
        else if (!strcmp(base_param, "EST_SIGMA_CAP")) EST_SIGMA_HARD_CAP_MM = clamp_f(fv, 0.5f, 5.0f);
#endif
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "EST_LOW_CF_THR")) EST_LOW_CF_WARN_THRESHOLD = clamp_f(fv, 0.0f, 1.0f);
        else if (!strcmp(base_param, "EST_FALLBACK_THR")) EST_FALLBACK_CF_THRESHOLD = clamp_f(fv, 0.0f, 0.5f);
#endif
        else if (!strcmp(base_param, "RELAY_CATCHUP_FRAC")) RELAY_CATCHUP_FRAC = clamp_f(fv, 0.5f, 3.0f);
        else if (!strcmp(base_param, "RELAY_NEUTRAL_FRAC")) RELAY_NEUTRAL_FRAC = clamp_f(fv, 0.5f, 3.0f);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "RELAY_MIN_FLIP_MM")) RELAY_MIN_FLIP_MM = clamp_f(fv, 0.0f, 100.0f);
        else if (!strcmp(base_param, "RELAY_COLLAPSE_DELAY_MS")) RELAY_COLLAPSE_DELAY_MS = clamp_i(iv, 0, 5000);
        else if (!strcmp(base_param, "RELAY_COLLAPSE_RAMP_MULT")) RELAY_COLLAPSE_RAMP_MULT = clamp_i(iv, 1, 16);
        else if (!strcmp(base_param, "RELAY_COLLAPSE_CAP_MS")) RELAY_COLLAPSE_CAP_MS = clamp_i(iv, 0, 5000);
#endif
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "BUF_DRIFT_TAU_MS")) BUF_DRIFT_EWMA_TAU_MS = clamp_i(iv, 5000, 600000);
        else if (!strcmp(base_param, "BUF_DRIFT_MIN_SMP")) BUF_DRIFT_MIN_SAMPLES = clamp_i(iv, 1, 32);
        else if (!strcmp(base_param, "BUF_DRIFT_THR_MM")) BUF_DRIFT_APPLY_THR_MM = clamp_f(fv, 0.0f, 5.0f);
        else if (!strcmp(base_param, "BUF_DRIFT_CLAMP")) BUF_DRIFT_CLAMP_MM = clamp_f(fv, 0.0f, BUF_DRIFT_CLAMP_LIMIT_MM);
        else if (!strcmp(base_param, "BUF_DRIFT_MIN_CF")) BUF_DRIFT_APPLY_MIN_CF = clamp_f(fv, 0.0f, 1.0f);
#endif
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "TENSION_RISK_WINDOW")) TENSION_RISK_WINDOW_MS = clamp_i(iv, 5000, 300000);
        else if (!strcmp(base_param, "TENSION_RISK_THR")) TENSION_RISK_THRESHOLD = clamp_i(iv, 0, 1000);
#endif
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "TS_BUF_MS")) TS_BUF_FALLBACK_MS = clamp_i(iv, 0, 30000);
        else if (!strcmp(base_param, "STARTUP_MS")) MOTION_STARTUP_MS = clamp_i(iv, 0, 30000);
#endif
        else if (!strcmp(base_param, "SERVO_OPEN")) SERVO_OPEN_US = clamp_i(iv, 400, 2700);
        else if (!strcmp(base_param, "SERVO_CLOSE")) SERVO_CLOSE_US = clamp_i(iv, 400, 2700);
        else if (!strcmp(base_param, "SERVO_BLOCK")) SERVO_BLOCK_US = clamp_i(iv, 400, 2700);
        else if (!strcmp(base_param, "SERVO_SETTLE") || !strcmp(base_param, "SERVO_SETTLE_MS")) SERVO_SETTLE_MS = clamp_i(iv, 100, 2000);
        else if (!strcmp(base_param, "UNLOAD_CUT")) UNLOAD_CUT = (iv == 1);
        else if (!strcmp(base_param, "CUT_FEED_RATE")) CUT_FEED_SPS = motion_clamp_rate_sps(clamp_i(mm_per_min_to_sps(fv), 100, 30000));
        else if (!strcmp(base_param, "CUT_FEED")) CUT_FEED_MM = clamp_i(iv, 1, 200);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "CUT_FEED_MS")) CUT_TIMEOUT_FEED_MS = clamp_i(iv, 1000, 120000);
        else if (!strcmp(base_param, "CUT_SETTLE_MS")) CUT_TIMEOUT_SETTLE_MS = clamp_i(iv, 500, 10000);
#endif
        else if (!strcmp(base_param, "CUT_LEN")) CUT_LENGTH_MM = clamp_i(iv, 1, 50);
        else if (!strcmp(base_param, "CUT_AMT")) CUT_AMOUNT = clamp_i(iv, 1, 5);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(base_param, "TC_CUT_MS")) TC_TIMEOUT_CUT_MS = clamp_i(iv, 1000, 30000);
        else if (!strcmp(base_param, "TC_TH_MS")) TC_TIMEOUT_TH_MS = clamp_i(iv, 0, 10000);
        else if (!strcmp(base_param, "TC_Y_MS")) TC_TIMEOUT_Y_MS = clamp_i(iv, 0, 30000);
#endif
        else handled = false;

        #undef SET_LANE

        if (handled) {
            motion_limit_runtime_rates(true);
            cmd_reply("OK", NULL);
        }
        else cmd_reply("ER", "SET:UNKNOWN_PARAM");
    } else if (!strcmp(cmd, "GET")) {
        char out[64];
        char param[32];
        int lane_mask = 1;
        strncpy(param, p, 32);
        size_t len = strlen(p);
        if (len > 3 && !strcmp(p + len - 3, "_L1")) {
            lane_mask = 1;
            param[len - 3] = '\0';
        } else if (len > 3 && !strcmp(p + len - 3, "_L2")) {
            lane_mask = 2;
            param[len - 3] = '\0';
        }
        int idx = (lane_mask == 2) ? 1 : 0;

        bool handled = true;
        if (!strcmp(param, "LIVE_TUNE_LOCK")) snprintf(out, sizeof(out), "LIVE_TUNE_LOCK:%d", g_live_tune_lock ? 1 : 0);
        else if (!strcmp(param, "FEED_RATE")) snprintf(out, sizeof(out), "FEED_RATE:%.1f", (double)sps_to_mm_per_min_idx(FEED_SPS, idx));
        else if (!strcmp(param, "REV_RATE")) snprintf(out, sizeof(out), "REV_RATE:%.1f", (double)sps_to_mm_per_min_idx(REV_SPS, idx));
        else if (!strcmp(param, "AUTO_RATE")) snprintf(out, sizeof(out), "AUTO_RATE:%.1f", (double)sps_to_mm_per_min_idx(AUTO_SPS, idx));
        else if (!strcmp(param, "SYNC_MAX_RATE")) snprintf(out, sizeof(out), "SYNC_MAX_RATE:%.1f", (double)sps_to_mm_per_min_idx(SYNC_MAX_SPS, idx));
        else if (!strcmp(param, "GLOBAL_MAX_RATE")) snprintf(out, sizeof(out), "GLOBAL_MAX_RATE:%.1f", (double)sps_to_mm_per_min_idx(GLOBAL_MAX_SPS, idx));
        else if (!strcmp(param, "SYNC_MIN_RATE")) snprintf(out, sizeof(out), "SYNC_MIN_RATE:%.1f", (double)sps_to_mm_per_min_idx(SYNC_MIN_SPS, idx));
        else if (!strcmp(param, "SYNC_RAMP_ACCEL")) {
            float tick_s = (float)SYNC_TICK_MS / 1000.0f;
            float accel = (float)SYNC_RAMP_UP_SPS * MM_PER_STEP[0] / tick_s;
            snprintf(out, sizeof(out), "SYNC_RAMP_ACCEL:%.0f", (double)accel);
        }
        else if (!strcmp(param, "SYNC_RAMP_DECEL")) {
            float tick_s = (float)SYNC_TICK_MS / 1000.0f;
            float accel = (float)SYNC_RAMP_DN_SPS * MM_PER_STEP[0] / tick_s;
            snprintf(out, sizeof(out), "SYNC_RAMP_DECEL:%.0f", (double)accel);
        }
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "SYNC_TICK_MS")) snprintf(out, sizeof(out), "SYNC_TICK_MS:%d", SYNC_TICK_MS);
#endif
        else if (!strcmp(param, "GLOBAL_MAX_ACCEL")) {
            float tick_s = (float)RAMP_TICK_MS / 1000.0f;
            float accel = (float)RAMP_STEP_SPS * MM_PER_STEP[0] / tick_s;
            snprintf(out, sizeof(out), "GLOBAL_MAX_ACCEL:%.0f", (double)accel);
        }
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "RAMP_TICK_MS")) snprintf(out, sizeof(out), "RAMP_TICK_MS:%d", RAMP_TICK_MS);
        else if (!strcmp(param, "PRE_RAMP_RATE")) snprintf(out, sizeof(out), "PRE_RAMP_RATE:%.1f", (double)sps_to_mm_per_min_idx(PRE_RAMP_SPS, idx));
#endif
        else if (!strcmp(param, "BUF_SWITCH_SPAN")) snprintf(out, sizeof(out), "BUF_SWITCH_SPAN:%.3f", (double)(BUF_SWITCH_SPAN_HALF_MM * 2.0f));
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "BUF_HYST")) snprintf(out, sizeof(out), "BUF_HYST:%d", BUF_HYST_MS);
        else if (!strcmp(param, "BUF_PREDICT_THR_MS")) snprintf(out, sizeof(out), "BUF_PREDICT_THR_MS:%d", BUF_PREDICT_THR_MS);
#endif
        else if (!strcmp(param, "AUTO_PRELOAD")) snprintf(out, sizeof(out), "AUTO_PRELOAD:%d", AUTO_PRELOAD ? 1 : 0);
        else if (!strcmp(param, "BL")) snprintf(out, sizeof(out), "BL:%s", sync_buffer_lock_arm_str());
        else if (!strcmp(param, "SYNC_STATE")) snprintf(out, sizeof(out), "SYNC_STATE:%d", (int)g_sync_state);
        else if (!strcmp(param, "RETRACT_MM")) snprintf(out, sizeof(out), "RETRACT_MM:%d", AUTOLOAD_RETRACT_MM);
        else if (!strcmp(param, "CUTTER")) snprintf(out, sizeof(out), "CUTTER:%d", ENABLE_CUTTER ? 1 : 0);
        else if (!strcmp(param, "AUTO_MODE")) snprintf(out, sizeof(out), "AUTO_MODE:%d", AUTO_MODE);
        else if (!strcmp(param, "RELOAD_MODE")) snprintf(out, sizeof(out), "RELOAD_MODE:%d", RELOAD_MODE);
        else if (!strcmp(param, "RUNOUT_COOLDOWN_MS")) snprintf(out, sizeof(out), "RUNOUT_COOLDOWN_MS:%d", RUNOUT_COOLDOWN_MS);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "POST_PRINT_STAB_MS")) snprintf(out, sizeof(out), "POST_PRINT_STAB_MS:%d", POST_PRINT_STAB_DELAY_MS);
#endif
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "RELOAD_Y_MS")) snprintf(out, sizeof(out), "RELOAD_Y_MS:%d", RELOAD_Y_TIMEOUT_MS);
#endif
        else if (!strcmp(param, "RELOAD_JOIN_MS")) snprintf(out, sizeof(out), "RELOAD_JOIN_MS:%d", RELOAD_JOIN_DELAY_MS);
        else if (!strcmp(param, "DIST_IN_OUT")) snprintf(out, sizeof(out), "DIST_IN_OUT:%d", DIST_IN_OUT);
        else if (!strcmp(param, "DIST_OUT_Y")) snprintf(out, sizeof(out), "DIST_OUT_Y:%d", DIST_OUT_Y);
        else if (!strcmp(param, "DIST_Y_BUF")) snprintf(out, sizeof(out), "DIST_Y_BUF:%d", DIST_Y_BUF);
        else if (!strcmp(param, "BUF_BODY_LEN")) snprintf(out, sizeof(out), "BUF_BODY_LEN:%d", BUF_BODY_LEN);
        else if (!strcmp(param, "BUF_MAX_TRAVEL")) snprintf(out, sizeof(out), "BUF_MAX_TRAVEL:%d", BUF_MAX_TRAVEL_MM);
        else if (!strcmp(param, "JOIN_RATE")) snprintf(out, sizeof(out), "JOIN_RATE:%.1f", (double)sps_to_mm_per_min_idx(JOIN_SPS, idx));
        else if (!strcmp(param, "PRESS_RATE")) snprintf(out, sizeof(out), "PRESS_RATE:%.1f", (double)sps_to_mm_per_min_idx(PRESS_SPS, idx));
        else if (!strcmp(param, "COMPRESSION_RATE")) snprintf(out, sizeof(out), "COMPRESSION_RATE:%.1f", (double)sps_to_mm_per_min_idx(COMPRESSION_SPS, idx));
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "BUF_STAB_RATE")) snprintf(out, sizeof(out), "BUF_STAB_RATE:%.1f", (double)sps_to_mm_per_min_idx(BUF_STAB_SPS, idx));
#endif
        else if (!strcmp(param, "FOLLOW_MS")) snprintf(out, sizeof(out), "FOLLOW_MS:%d", FOLLOW_TIMEOUT_MS[idx]);
        else if (!strcmp(param, "BASELINE_RATE")) snprintf(out, sizeof(out), "BASELINE_RATE:%.1f", (double)sps_to_mm_per_min_idx(g_baseline_target_sps, idx));
        else if (!strcmp(param, "BASELINE_SPS")) snprintf(out, sizeof(out), "BASELINE_SPS:%d", g_baseline_target_sps);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "BASELINE_ALPHA")) snprintf(out, sizeof(out), "BASELINE_ALPHA:%.3f", (double)g_baseline_alpha);
#endif
        else if (!strcmp(param, "BUF_SENSOR")) snprintf(out, sizeof(out), "BUF_SENSOR:%d", BUF_SENSOR_TYPE);
        else if (!strcmp(param, "BUF_HOME_STATE")) snprintf(out, sizeof(out), "BUF_HOME_STATE:%d", BUF_HOME_STATE);
        else if (!strcmp(param, "BUF_PSF_MAX_COMP")) snprintf(out, sizeof(out), "BUF_PSF_MAX_COMP:%.3f", (double)BUF_PSF_MAX_COMP);
        else if (!strcmp(param, "BUF_PSF_MAX_TENS")) snprintf(out, sizeof(out), "BUF_PSF_MAX_TENS:%.3f", (double)BUF_PSF_MAX_TENS);
        else if (!strcmp(param, "BUF_PSF_NEUTRAL")) snprintf(out, sizeof(out), "BUF_PSF_NEUTRAL:%.3f", (double)BUF_PSF_NEUTRAL);
        else if (!strcmp(param, "BUF_GOAL")) snprintf(out, sizeof(out), "BUF_GOAL:%.3f", (double)BUF_GOAL);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "BUF_ALPHA")) snprintf(out, sizeof(out), "BUF_ALPHA:%.3f", (double)BUF_ANALOG_ALPHA);
#endif
        else if (!strcmp(param, "AUTOLOAD_MAX")) snprintf(out, sizeof(out), "AUTOLOAD_MAX:%d", AUTOLOAD_MAX_MM);
        else if (!strcmp(param, "LOAD_MAX")) snprintf(out, sizeof(out), "LOAD_MAX:%d", LOAD_MAX_MM);
        else if (!strcmp(param, "UNLOAD_MAX")) snprintf(out, sizeof(out), "UNLOAD_MAX:%d", UNLOAD_MAX_MM);
        else if (!strcmp(param, "UNLOAD_TENSION_BLOCK_MS")) snprintf(out, sizeof(out), "UNLOAD_TENSION_BLOCK_MS:%d", UNLOAD_TENSION_BLOCK_MS);
        else if (!strcmp(param, "TC_LOAD_MS")) snprintf(out, sizeof(out), "TC_LOAD_MS:%d", LOAD_MAX_MM);
        else if (!strcmp(param, "TC_UNLOAD_MS")) snprintf(out, sizeof(out), "TC_UNLOAD_MS:%d", UNLOAD_MAX_MM);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "TC_CUT_MS")) snprintf(out, sizeof(out), "TC_CUT_MS:%d", TC_TIMEOUT_CUT_MS);
        else if (!strcmp(param, "TC_TH_MS")) snprintf(out, sizeof(out), "TC_TH_MS:%d", TC_TIMEOUT_TH_MS);
        else if (!strcmp(param, "TC_Y_MS")) snprintf(out, sizeof(out), "TC_Y_MS:%d", TC_TIMEOUT_Y_MS);
#endif
        else if (!strcmp(param, "SYNC_REFILL_MM")) snprintf(out, sizeof(out), "SYNC_REFILL_MM:%d", (int)g_sync_refill_effort_mm);
        else if (!strcmp(param, "SYNC_RELIEVE_MM")) snprintf(out, sizeof(out), "SYNC_RELIEVE_MM:%d", (int)g_sync_relieve_effort_mm);
        else if (!strcmp(param, "SYNC_KP_RATE")) snprintf(out, sizeof(out), "SYNC_KP_RATE:%.1f", (double)sps_to_mm_per_min(SYNC_KP_SPS));
        else if (!strcmp(param, "KD_PSF")) snprintf(out, sizeof(out), "KD_PSF:%.3f", (double)KD_PSF);
        else if (!strcmp(param, "SYNC_PSF_SLEW_PER_MM")) snprintf(out, sizeof(out), "SYNC_PSF_SLEW_PER_MM:%.1f", (double)SYNC_PSF_SLEW_PER_MM);
        else if (!strcmp(param, "SYNC_PSF_FILTER_MM")) snprintf(out, sizeof(out), "SYNC_PSF_FILTER_MM:%.2f", (double)SYNC_PSF_FILTER_MM);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "SYNC_OVERSHOOT_PCT")) snprintf(out, sizeof(out), "SYNC_OVERSHOOT_PCT:%d", SYNC_OVERSHOOT_PCT);
#endif
        else if (!strcmp(param, "SYNC_RESERVE_PCT")) snprintf(out, sizeof(out), "SYNC_RESERVE_PCT:%d", SYNC_RESERVE_PCT);
        else if (!strcmp(param, "COMPRESSION_BIAS_FRAC")) snprintf(out, sizeof(out), "COMPRESSION_BIAS_FRAC:%.3f", (double)SYNC_COMPRESSION_BIAS_FRAC);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "NEUTRAL_CREEP_TIMEOUT_MS")) snprintf(out, sizeof(out), "NEUTRAL_CREEP_TIMEOUT_MS:%d", NEUTRAL_CREEP_TIMEOUT_MS);
        else if (!strcmp(param, "NEUTRAL_CREEP_RATE") || !strcmp(param, "NEUTRAL_CREEP_RATE_SPS_PER_S")) snprintf(out, sizeof(out), "%s:%d", param, NEUTRAL_CREEP_RATE_SPS_PER_S);
        else if (!strcmp(param, "NEUTRAL_CREEP_CAP") || !strcmp(param, "NEUTRAL_CREEP_CAP_FRAC")) snprintf(out, sizeof(out), "%s:%d", param, NEUTRAL_CREEP_CAP_FRAC);
        else if (!strcmp(param, "VAR_BLEND_FRAC") || !strcmp(param, "BUF_VARIANCE_BLEND_FRAC")) snprintf(out, sizeof(out), "%s:%.3f", param, (double)BUF_VARIANCE_BLEND_FRAC);
        else if (!strcmp(param, "VAR_BLEND_REF_MM") || !strcmp(param, "BUF_VARIANCE_BLEND_REF_MM")) snprintf(out, sizeof(out), "%s:%.3f", param, (double)BUF_VARIANCE_BLEND_REF_MM);
#endif
        else if (!strcmp(param, "SYNC_AUTO_STOP")) snprintf(out, sizeof(out), "SYNC_AUTO_STOP:%d", SYNC_AUTO_STOP_MS);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "SYNC_TENSION_STOP_MS")) snprintf(out, sizeof(out), "SYNC_TENSION_STOP_MS:%d", SYNC_TENSION_DWELL_STOP_MS);
        else if (!strcmp(param, "SYNC_TENSION_RAMP_MS")) snprintf(out, sizeof(out), "SYNC_TENSION_RAMP_MS:%d", SYNC_TENSION_RAMP_DELAY_MS);
        else if (!strcmp(param, "SYNC_OVERSHOOT_NEUTRAL_EXT")) snprintf(out, sizeof(out), "SYNC_OVERSHOOT_NEUTRAL_EXT:%d", SYNC_OVERSHOOT_NEUTRAL_EXTEND);
        else if (!strcmp(param, "SYNC_INT_GAIN")) snprintf(out, sizeof(out), "SYNC_INT_GAIN:%.4f", (double)SYNC_RESERVE_INTEGRAL_GAIN);
        else if (!strcmp(param, "SYNC_INT_CLAMP")) snprintf(out, sizeof(out), "SYNC_INT_CLAMP:%.3f", (double)SYNC_RESERVE_INTEGRAL_CLAMP_MM);
        else if (!strcmp(param, "SYNC_INT_DECAY_MS")) snprintf(out, sizeof(out), "SYNC_INT_DECAY_MS:%d", SYNC_RESERVE_INTEGRAL_DECAY_MS);
        else if (!strcmp(param, "EST_SIGMA_CAP")) snprintf(out, sizeof(out), "EST_SIGMA_CAP:%.3f", (double)EST_SIGMA_HARD_CAP_MM);
#endif
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "EST_LOW_CF_THR")) snprintf(out, sizeof(out), "EST_LOW_CF_THR:%.3f", (double)EST_LOW_CF_WARN_THRESHOLD);
        else if (!strcmp(param, "EST_FALLBACK_THR")) snprintf(out, sizeof(out), "EST_FALLBACK_THR:%.3f", (double)EST_FALLBACK_CF_THRESHOLD);
#endif
        else if (!strcmp(param, "RELAY_CATCHUP_FRAC")) snprintf(out, sizeof(out), "RELAY_CATCHUP_FRAC:%.3f", (double)RELAY_CATCHUP_FRAC);
        else if (!strcmp(param, "RELAY_NEUTRAL_FRAC")) snprintf(out, sizeof(out), "RELAY_NEUTRAL_FRAC:%.3f", (double)RELAY_NEUTRAL_FRAC);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "RELAY_MIN_FLIP_MM")) snprintf(out, sizeof(out), "RELAY_MIN_FLIP_MM:%.3f", (double)RELAY_MIN_FLIP_MM);
        else if (!strcmp(param, "RELAY_COLLAPSE_DELAY_MS")) snprintf(out, sizeof(out), "RELAY_COLLAPSE_DELAY_MS:%d", RELAY_COLLAPSE_DELAY_MS);
        else if (!strcmp(param, "RELAY_COLLAPSE_RAMP_MULT")) snprintf(out, sizeof(out), "RELAY_COLLAPSE_RAMP_MULT:%d", RELAY_COLLAPSE_RAMP_MULT);
        else if (!strcmp(param, "RELAY_COLLAPSE_CAP_MS")) snprintf(out, sizeof(out), "RELAY_COLLAPSE_CAP_MS:%d", RELAY_COLLAPSE_CAP_MS);
#endif
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "BUF_DRIFT_TAU_MS")) snprintf(out, sizeof(out), "BUF_DRIFT_TAU_MS:%d", BUF_DRIFT_EWMA_TAU_MS);
        else if (!strcmp(param, "BUF_DRIFT_MIN_SMP")) snprintf(out, sizeof(out), "BUF_DRIFT_MIN_SMP:%d", BUF_DRIFT_MIN_SAMPLES);
        else if (!strcmp(param, "BUF_DRIFT_THR_MM")) snprintf(out, sizeof(out), "BUF_DRIFT_THR_MM:%.3f", (double)BUF_DRIFT_APPLY_THR_MM);
        else if (!strcmp(param, "BUF_DRIFT_CLAMP")) snprintf(out, sizeof(out), "BUF_DRIFT_CLAMP:%.3f", (double)BUF_DRIFT_CLAMP_MM);
        else if (!strcmp(param, "BUF_DRIFT_MIN_CF")) snprintf(out, sizeof(out), "BUF_DRIFT_MIN_CF:%.3f", (double)BUF_DRIFT_APPLY_MIN_CF);
#endif
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "TENSION_RISK_WINDOW")) snprintf(out, sizeof(out), "TENSION_RISK_WINDOW:%d", TENSION_RISK_WINDOW_MS);
        else if (!strcmp(param, "TENSION_RISK_THR")) snprintf(out, sizeof(out), "TENSION_RISK_THR:%d", TENSION_RISK_THRESHOLD);
#endif
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "TS_BUF_MS")) snprintf(out, sizeof(out), "TS_BUF_MS:%d", TS_BUF_FALLBACK_MS);
        else if (!strcmp(param, "STARTUP_MS")) snprintf(out, sizeof(out), "STARTUP_MS:%d", MOTION_STARTUP_MS);
        else if (!strcmp(param, "EST_ALPHA_MIN")) snprintf(out, sizeof(out), "EST_ALPHA_MIN:%.3f", (double)EST_ALPHA_MIN);
        else if (!strcmp(param, "EST_ALPHA_MAX")) snprintf(out, sizeof(out), "EST_ALPHA_MAX:%.3f", (double)EST_ALPHA_MAX);
        else if (!strcmp(param, "ZONE_BIAS_BASE")) snprintf(out, sizeof(out), "ZONE_BIAS_BASE:%.1f", (double)sps_to_mm_per_min(ZONE_BIAS_BASE_SPS));
        else if (!strcmp(param, "ZONE_BIAS_RAMP")) snprintf(out, sizeof(out), "ZONE_BIAS_RAMP:%.1f", (double)sps_to_mm_per_min(ZONE_BIAS_RAMP_SPS_S));
        else if (!strcmp(param, "ZONE_BIAS_MAX")) snprintf(out, sizeof(out), "ZONE_BIAS_MAX:%.1f", (double)sps_to_mm_per_min(ZONE_BIAS_MAX_SPS));
        else if (!strcmp(param, "RELOAD_LEAN")) snprintf(out, sizeof(out), "RELOAD_LEAN:%.2f", (double)RELOAD_LEAN_FACTOR);
#endif
        else if (!strcmp(param, "MICROSTEPS")) snprintf(out, sizeof(out), "MICROSTEPS:%d", TMC_MICROSTEPS[idx]);
        else if (!strcmp(param, "INTERPOLATE")) snprintf(out, sizeof(out), "INTERPOLATE:%d", TMC_INTERPOLATE[idx] ? 1 : 0);
        else if (!strcmp(param, "STEALTHCHOP")) snprintf(out, sizeof(out), "STEALTHCHOP:%.1f", (double)sps_to_mm_per_min_idx(TMC_STEALTHCHOP_SPS[idx], idx));
        else if (!strcmp(param, "DRIVER_TBL")) snprintf(out, sizeof(out), "DRIVER_TBL:%d", TMC_TBL[idx]);
        else if (!strcmp(param, "DRIVER_TOFF")) snprintf(out, sizeof(out), "DRIVER_TOFF:%d", TMC_TOFF[idx]);
        else if (!strcmp(param, "DRIVER_HSTRT")) snprintf(out, sizeof(out), "DRIVER_HSTRT:%d", TMC_HSTRT[idx]);
        else if (!strcmp(param, "DRIVER_HEND")) snprintf(out, sizeof(out), "DRIVER_HEND:%d", TMC_HEND[idx]);
        else if (!strcmp(param, "ROTATION_DIST")) snprintf(out, sizeof(out), "ROTATION_DIST:%.3f", (double)TMC_ROTATION_DISTANCE[idx]);
        else if (!strcmp(param, "GEAR_RATIO")) snprintf(out, sizeof(out), "GEAR_RATIO:%.3f", (double)TMC_GEAR_RATIO[idx]);
        else if (!strcmp(param, "FULL_STEPS")) snprintf(out, sizeof(out), "FULL_STEPS:%d", TMC_FULL_STEPS[idx]);
        else if (!strcmp(param, "RUN_CURRENT_MA")) snprintf(out, sizeof(out), "RUN_CURRENT_MA:%d", TMC_RUN_CURRENT_MA[idx]);
        else if (!strcmp(param, "HOLD_CURRENT_MA")) snprintf(out, sizeof(out), "HOLD_CURRENT_MA:%d", TMC_HOLD_CURRENT_MA[idx]);
        else if (!strcmp(param, "SERVO_OPEN")) snprintf(out, sizeof(out), "SERVO_OPEN:%d", SERVO_OPEN_US);
        else if (!strcmp(param, "SERVO_CLOSE")) snprintf(out, sizeof(out), "SERVO_CLOSE:%d", SERVO_CLOSE_US);
        else if (!strcmp(param, "SERVO_BLOCK")) snprintf(out, sizeof(out), "SERVO_BLOCK:%d", SERVO_BLOCK_US);
        else if (!strcmp(param, "SERVO_SETTLE") || !strcmp(param, "SERVO_SETTLE_MS")) snprintf(out, sizeof(out), "%s:%d", param, SERVO_SETTLE_MS);
        else if (!strcmp(param, "UNLOAD_CUT")) snprintf(out, sizeof(out), "UNLOAD_CUT:%d", UNLOAD_CUT ? 1 : 0);
        else if (!strcmp(param, "CUT_FEED_RATE")) snprintf(out, sizeof(out), "CUT_FEED_RATE:%.1f", (double)sps_to_mm_per_min_idx(CUT_FEED_SPS, idx));
        else if (!strcmp(param, "CUT_FEED")) snprintf(out, sizeof(out), "CUT_FEED:%d", CUT_FEED_MM);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "CUT_FEED_MS")) snprintf(out, sizeof(out), "CUT_FEED_MS:%d", CUT_TIMEOUT_FEED_MS);
        else if (!strcmp(param, "CUT_SETTLE_MS")) snprintf(out, sizeof(out), "CUT_SETTLE_MS:%d", CUT_TIMEOUT_SETTLE_MS);
#endif
        else if (!strcmp(param, "CUT_LEN")) snprintf(out, sizeof(out), "CUT_LEN:%d", CUT_LENGTH_MM);
        else if (!strcmp(param, "CUT_AMT")) snprintf(out, sizeof(out), "CUT_AMT:%d", CUT_AMOUNT);
#ifdef FLARE_DEV_TUNING
        else if (!strcmp(param, "TC_CUT_MS")) snprintf(out, sizeof(out), "TC_CUT_MS:%d", TC_TIMEOUT_CUT_MS);
        else if (!strcmp(param, "TC_TH_MS")) snprintf(out, sizeof(out), "TC_TH_MS:%d", TC_TIMEOUT_TH_MS);
        else if (!strcmp(param, "TC_Y_MS")) snprintf(out, sizeof(out), "TC_Y_MS:%d", TC_TIMEOUT_Y_MS);
#endif
        else handled = false;

        if (handled) cmd_reply("OK", out);
        else cmd_reply("ER", "GET:UNKNOWN_PARAM");
    } else if (!strcmp(cmd, "TW")) {
        int ln, reg;
        uint32_t val;
        if (sscanf(p, "%d:%d:%i", &ln, &reg, &val) == 3 && (ln == 1 || ln == 2) && reg >= 0 && reg <= 127) {
            tmc_t *t = (ln == 1) ? &g_tmc_l1 : &g_tmc_l2;
            if (tmc_write(t, (uint8_t)reg, val)) {
                int idx = lane_to_idx(ln);
                if (reg == TMC_REG_IHOLD_IRUN) {
                    g_shadow_ihold_irun[idx] = val;
                    g_shadow_ihold_irun_valid[idx] = true;
                    sync_currents_from_ihold_irun(ln, val);
                } else if (reg == TMC_REG_CHOPCONF) {
                    g_shadow_vsense[idx] = ((val >> 17) & 0x1u) != 0u;
                    if (g_shadow_ihold_irun_valid[idx]) {
                        sync_currents_from_ihold_irun(ln, g_shadow_ihold_irun[idx]);
                    }
                }
                cmd_reply("OK", NULL);
            } else {
                cmd_reply("ER", "TW:FAILED");
            }
        } else {
            cmd_reply("ER", "ARG");
        }
    } else if (!strcmp(cmd, "TR")) {
        int ln, reg;
        if (sscanf(p, "%d:%d", &ln, &reg) == 2 && (ln == 1 || ln == 2) && reg >= 0 && reg <= 127) {
            int idx = lane_to_idx(ln);
            if (reg == TMC_REG_IHOLD_IRUN && g_shadow_ihold_irun_valid[idx]) {
                char out[32];
                snprintf(out, sizeof(out), "%d:%d:0x%08X", ln, reg, (unsigned int)g_shadow_ihold_irun[idx]);
                cmd_reply("OK", out);
                return;
            }
            tmc_t *t = (ln == 1) ? &g_tmc_l1 : &g_tmc_l2;
            uint32_t val = 0;
            if (tmc_read(t, (uint8_t)reg, &val)) {
                char out[32];
                snprintf(out, sizeof(out), "%d:%d:0x%08X", ln, reg, (unsigned int)val);
                cmd_reply("OK", out);
            } else {
                cmd_reply("ER", "TR:NO_RESPONSE");
            }
        } else {
            cmd_reply("ER", "ARG");
        }
    } else if (!strcmp(cmd, "TW")) {
        int ln, reg;
        uint32_t val;
        if (sscanf(p, "%d:%d:%u", &ln, &reg, &val) == 3 && (ln == 1 || ln == 2) && reg >= 0 && reg <= 127) {
            tmc_t *t = (ln == 1) ? &g_tmc_l1 : &g_tmc_l2;
            if (tmc_write(t, (uint8_t)reg, val)) {
                cmd_reply("OK", NULL);
            } else {
                cmd_reply("ER", "TW:FAIL");
            }
        } else {
            cmd_reply("ER", "ARG");
        }
    } else if (!strcmp(cmd, "RR")) {
        int ln = atoi(p);
        if (ln != 1 && ln != 2) {
            cmd_reply("ER", "ARG");
        } else {
            tmc_t probe = (ln == 1) ? g_tmc_l1 : g_tmc_l2;
            char out[128];
            int pos = snprintf(out, sizeof(out), "%d:", ln);
            for (uint8_t a = 0; a < 4; a++) {
                probe.addr = a;
                uint8_t buf[8] = {0};
                int n = tmc_read_raw(&probe, TMC_REG_GCONF, buf);
                pos += snprintf(out + pos, sizeof(out) - pos,
                    "A%u:N=%d:%02X%02X%02X%02X%02X%02X%02X%02X ",
                    a, n, buf[0], buf[1], buf[2], buf[3],
                    buf[4], buf[5], buf[6], buf[7]);
            }
            cmd_reply("OK", out);
        }
    } else if (!strcmp(cmd, "BOOT")) {
        cmd_reply("OK", "REBOOTING_TO_BOOTSEL");
        sleep_ms(100);
        reset_usb_boot(0, 0);
    } else {
        cmd_reply("ER", "UNKNOWN");
    }
}

void cmd_poll(uint32_t now_ms) {
    manual_unload_tick(now_ms);

    int c;
    int bytes_processed = 0;
    int commands_processed = 0;

    while (bytes_processed < CMD_POLL_BYTE_BUDGET &&
           commands_processed < CMD_POLL_COMMAND_BUDGET &&
           (c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        bytes_processed++;

        if (c == '\r') continue;

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
