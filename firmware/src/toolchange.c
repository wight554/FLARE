/// @file toolchange.c
/// @brief Toolchange + RELOAD orchestration: the tc_state_t state machine that
///        sequences cut, unload, swap, load, and the RELOAD WAIT_Y->APPROACH->FOLLOW
///        runout flow. (Transition map documented at the tc_state_t handler.)
/// @details One owner of cutter/toolchange/RELOAD transitions. Sync is suppressed
///          while a toolchange or RELOAD runs. See BEHAVIOR.md "Toolchange - TC:";
///          spec toolchange-orchestration.

#include "toolchange.h"

#include <stdio.h>
#include <string.h>

#include "hardware/pwm.h"

#include "motion.h"
#include "protocol.h"
#include "sync.h"

#include "cutter.h"

#define RELOAD_COMPRESSION_SOFT_WALL_MS 900.0f
#define RELOAD_COMPRESSION_HARD_WALL_MS 450.0f
#define RELOAD_COMPRESSION_HARD_PUSH_MM_S 0.25f
#define RELOAD_COMPRESSION_HARD_HOLD_MS 250u
#define RELOAD_APPROACH_LIMIT_MM 2000.0f
#define RELOAD_TOUCH_FLOOR_PERCENT_DENOM 100
#define RELOAD_FOLLOW_ABSOLUTE_TIMEOUT_MS 300000u
#define RELOAD_STATUS_INTERVAL_MS 500u
#define TC_EVENT_BUF_LEN 8
#define TC_STATUS_EVENT_BUF_LEN 64

tc_state_t tc_state(void) {
    return g_tc_ctx.state;
}

void tc_enter_error(const char *reason) {
    cmd_event_critical("TC:ERROR", reason);
    stop_all();
    cutter_abort();
    g_tc_ctx.state = TC_ERROR;
}

void tc_start(int target_lane, uint32_t now_ms) {
    if (g_tc_ctx.state != TC_IDLE)
        return;
    memset(&g_tc_ctx, 0, sizeof(g_tc_ctx));
    if (target_lane != 1 && target_lane != 2)
        return;
    if (g_active_lane != 1 && g_active_lane != 2)
        return;

    g_tc_ctx.target_lane = target_lane;
    g_tc_ctx.from_lane = g_active_lane;
    g_tc_ctx.phase_start_ms = now_ms;
    // Same lane = nothing to unload, go straight to (re)load. Otherwise unload
    // first; if a toolhead sensor says filament is still in the hotend, wait for
    // the slicer's tip-retract to clear it before reversing (avoids a tug-of-war).
    if (target_lane == g_active_lane) {
        g_tc_ctx.state = TC_LOAD_START;
    } else if (g_tc_timeout_th_ms > 0 && g_toolhead_has_filament) {
        g_tc_ctx.state = TC_UNLOAD_WAIT_TH;
    } else {
        g_tc_ctx.state = TC_UNLOAD_REVERSE;
    }
}

void tc_manual_reload(uint32_t now_ms) {
    if (g_tc_ctx.state != TC_IDLE && g_tc_ctx.state != TC_ERROR)
        return;
    lane_t *lane = lane_ptr(g_active_lane);
    if (!lane)
        return;

    memset(&g_tc_ctx, 0, sizeof(g_tc_ctx));
    g_tc_ctx.target_lane = g_active_lane;
    g_tc_ctx.from_lane = g_active_lane;
    set_toolhead_filament(false);

    char lane_s[2];
    lane_id_str(lane_s, g_active_lane);
    cmd_event("RELOAD:JOINING", lane_s);

    int approach_sps = g_feed_sps;
    if (approach_sps <= 0)
        approach_sps = g_join_sps;

    lane_start(lane, TASK_FEED, approach_sps, true, now_ms, RELOAD_APPROACH_LIMIT_MM);

    g_tc_ctx.ready_to_join_since_ms = 0;
    g_tc_ctx.reload_tick_ms = now_ms;
    g_tc_ctx.reload_current_sps = 0;
    g_tc_ctx.last_compression_ms = 0;
    g_tc_ctx.phase_start_ms = now_ms;
    g_tc_ctx.state = TC_RELOAD_APPROACH;
}

void tc_abort(void) {
    if (g_tc_ctx.state == TC_IDLE)
        return;
    stop_all();
    cutter_abort();
    set_toolhead_filament(false);
    g_tc_ctx.state = TC_IDLE;
    cmd_event_critical("TC:ERROR", "ABORTED");
}

void reload_trigger(int runout_lane, uint32_t now_ms) {
    memset(&g_tc_ctx, 0, sizeof(g_tc_ctx));
    int other = (runout_lane == 1) ? 2 : 1;
    lane_t *other_lane_ptr = lane_ptr(other);
    if (!other_lane_ptr || !lane_in_present(other_lane_ptr)) {
        sync_disable(true);
        cmd_event_critical("RELOAD:FAULT", "NO_FILAMENT");
        return;
    }
    char event[TC_EVENT_BUF_LEN];
    snprintf(event, sizeof(event), "%d->%d", runout_lane, other);
    cmd_event("RELOAD:SWITCHING", event);
    g_tc_ctx.target_lane = other;
    g_tc_ctx.from_lane = runout_lane;
    g_tc_ctx.phase_start_ms = now_ms;
    g_tc_ctx.state = TC_RELOAD_WAIT_Y;
}

const char *tc_state_name(tc_state_t s) {
    switch (s) {
    case TC_IDLE:
        return "IDLE";
    case TC_UNLOAD_CUT:
        return "UNLOAD_CUT";
    case TC_UNLOAD_WAIT_CUT:
        return "UNLOAD_WAIT_CUT";
    case TC_UNLOAD_REVERSE:
        return "UNLOAD_REVERSE";
    case TC_UNLOAD_WAIT_OUT:
        return "UNLOAD_WAIT_OUT";
    case TC_UNLOAD_WAIT_Y:
        return "UNLOAD_WAIT_Y";
    case TC_UNLOAD_WAIT_TH:
        return "UNLOAD_WAIT_TH";
    case TC_UNLOAD_DONE:
        return "UNLOAD_DONE";
    case TC_SWAP:
        return "SWAP";
    case TC_LOAD_START:
        return "LOAD_START";
    case TC_LOAD_WAIT_OUT:
        return "LOAD_WAIT_OUT";
    case TC_LOAD_WAIT_TH:
        return "LOAD_WAIT_TH";
    case TC_LOAD_DONE:
        return "LOAD_DONE";
    case TC_RELOAD_WAIT_Y:
        return "RELOAD_WAIT_Y";
    case TC_RELOAD_APPROACH:
        return "RELOAD_APPROACH";
    case TC_RELOAD_FOLLOW:
        return "RELOAD_FOLLOW";
    case TC_ERROR:
        return "ERROR";
    default:
        return "?";
    }
}

const char *task_name(task_t t) {
    switch (t) {
    case TASK_IDLE:
        return "IDLE";
    case TASK_AUTOLOAD:
        return "AUTOLOAD";
    case TASK_FEED:
        return "FEED";
    case TASK_UNLOAD:
        return "UNLOAD";
    case TASK_LOAD_FULL:
        return "LOAD_FULL";
    case TASK_MOVE:
        return "MOVE";
    default:
        return "?";
    }
}

static bool tc_unload_cut_pending(void) {
    return g_enable_cutter && g_unload_cut && !g_tc_ctx.unload_cut_done;
}

static void tc_unload_next_after_clear(uint32_t now_ms) {
    g_tc_ctx.phase_start_ms = now_ms;
    if (tc_unload_cut_pending()) {
        g_tc_ctx.state = TC_UNLOAD_CUT;
    } else {
        g_tc_ctx.state = (g_tc_timeout_y_ms > 0) ? TC_UNLOAD_WAIT_Y : TC_UNLOAD_DONE;
    }
}

static void tc_start_unload_lane(lane_t *lane, uint32_t now_ms) {
    lane->unload_to_in = false;
    lane->unload_buf_recover_done = true;
    lane_start(lane, TASK_UNLOAD, g_rev_sps, false, now_ms, (float)g_unload_max_mm);
    lane->suppress_unloaded_event = true;
}

static uint32_t tc_cut_watchdog_ms(lane_t *lane) {
    uint32_t configured_ms = (g_tc_timeout_cut_ms > 0) ? (uint32_t)g_tc_timeout_cut_ms : 0u;
    uint32_t expected_ms = cutter_expected_ms(lane, true);
    return (configured_ms > expected_ms) ? configured_ms : expected_ms;
}

static void tc_tick_unload_states(lane_t *lane, uint32_t now_ms, uint32_t age) {
    switch (g_tc_ctx.state) {
    case TC_UNLOAD_CUT:
        cutter_start(lane, true, now_ms);
        g_tc_ctx.phase_start_ms = now_ms;
        g_tc_ctx.state = TC_UNLOAD_WAIT_CUT;
        break;

    case TC_UNLOAD_WAIT_CUT:
        if (!cutter_busy()) {
            if (cutter_failed()) {
                tc_enter_error("CUT_FAILED");
            } else {
                g_tc_ctx.unload_cut_done = true;
                g_tc_ctx.phase_start_ms = now_ms;
                g_tc_ctx.state = TC_UNLOAD_REVERSE;
            }
        } else if (age > tc_cut_watchdog_ms(lane)) {
            tc_enter_error("CUT_TIMEOUT");
        }
        break;

    case TC_UNLOAD_REVERSE: {
        char lane_s[2];
        lane_id_str(lane_s, g_active_lane);
        cmd_event("TC:UNLOADING", lane_s);
        if (!lane_out_present(lane)) {
            if (!g_tc_ctx.unload_cut_done)
                g_tc_ctx.unload_cut_done = true;
            tc_unload_next_after_clear(now_ms);
        } else {
            tc_start_unload_lane(lane, now_ms);
            g_tc_ctx.phase_start_ms = now_ms;
            g_tc_ctx.state = TC_UNLOAD_WAIT_OUT;
        }
        break;
    }

    case TC_UNLOAD_WAIT_OUT:
        if (lane->task == TASK_IDLE) {
            if (lane_out_present(lane)) {
                tc_enter_error("UNLOAD_TIMEOUT");
            } else {
                tc_unload_next_after_clear(now_ms);
            }
        }
        break;

    case TC_UNLOAD_WAIT_Y:
        if (!on_al(&g_y_split)) {
            g_tc_ctx.phase_start_ms = now_ms;
            g_tc_ctx.state = TC_UNLOAD_DONE;
        } else if (age > (uint32_t)g_tc_timeout_y_ms) {
            tc_enter_error("Y_TIMEOUT");
        }
        break;

    case TC_UNLOAD_WAIT_TH:
        if (!g_toolhead_has_filament || age > (uint32_t)g_tc_timeout_th_ms) {
            set_toolhead_filament(false);
            g_tc_ctx.phase_start_ms = now_ms;
            g_tc_ctx.state = TC_UNLOAD_REVERSE;
        }
        break;

    case TC_UNLOAD_DONE:
        g_tc_ctx.state = TC_SWAP;
        break;

    default:
        break;
    }
}

static void tc_tick_load_states(lane_t *lane, uint32_t now_ms, uint32_t age) {
    switch (g_tc_ctx.state) {
    case TC_SWAP: {
        char swap_s[TC_EVENT_BUF_LEN];
        snprintf(swap_s, sizeof(swap_s), "%d->%d", g_active_lane, g_tc_ctx.target_lane);
        cmd_event("TC:SWAPPING", swap_s);
        set_active_lane(g_tc_ctx.target_lane);
        g_tc_ctx.phase_start_ms = now_ms;
        g_tc_ctx.state = TC_LOAD_START;
        break;
    }

    case TC_LOAD_START: {
        if (on_al(&g_y_split)) {
            tc_enter_error("HUB_NOT_CLEAR");
            break;
        }
        char lane_s[2];
        lane_id_str(lane_s, g_active_lane);
        cmd_event("TC:LOADING", lane_s);
        set_toolhead_filament(false);
        lane_start(lane, TASK_LOAD_FULL, g_feed_sps, true, now_ms, (float)g_load_max_mm);
        g_tc_ctx.phase_start_ms = now_ms;
        g_tc_ctx.state = TC_LOAD_WAIT_OUT;
        break;
    }

    case TC_LOAD_WAIT_OUT:
        if (lane_out_present(lane)) {
            g_tc_ctx.phase_start_ms = now_ms;
            g_tc_ctx.state = TC_LOAD_WAIT_TH;
        } else if (lane->task == TASK_IDLE) {
            tc_enter_error("LOAD_TIMEOUT");
        }
        break;

    case TC_LOAD_WAIT_TH:
        if (lane->task == TASK_IDLE) {
            if (lane->load_completed) {
                g_tc_ctx.state = TC_LOAD_DONE;
            } else {
                tc_enter_error("LOAD_TIMEOUT");
            }
        }
        break;

    case TC_LOAD_DONE: {
        char lane_s[2];
        lane_id_str(lane_s, g_active_lane);
        cmd_event("TC:DONE", lane_s);
        g_tc_ctx.state = TC_IDLE;
        break;
    }

    default:
        break;
    }
}

static void tc_tick_reload_wait_y(lane_t *lane, uint32_t now_ms, uint32_t age) {
    lane_t *from_lane_ptr = lane_ptr(g_tc_ctx.from_lane);
    if (from_lane_ptr && lane_out_present(from_lane_ptr)) {
        if (from_lane_ptr->task != TASK_FEED)
            lane_start(from_lane_ptr, TASK_FEED, g_compression_sps, true, now_ms, 0);
    } else if (from_lane_ptr && from_lane_ptr->task == TASK_FEED) {
        lane_stop(from_lane_ptr);
    }

    // Debounce the "ready to join" condition: stamp the time it first goes true,
    // and reset to 0 the moment it drops. The join only fires after the condition
    // has held continuously for RELOAD_JOIN_DELAY_MS, so a flickering sensor near
    // the Y-splitter cannot trigger an early join into a not-yet-clear hub.
    bool tail_cleared = (!from_lane_ptr || !lane_out_present(from_lane_ptr));
    bool y_cleared = (!on_al(&g_y_split) || g_reload_y_timeout_ms == 0);
    if (tail_cleared && y_cleared) {
        if (g_tc_ctx.ready_to_join_since_ms == 0)
            g_tc_ctx.ready_to_join_since_ms = now_ms;
    } else {
        g_tc_ctx.ready_to_join_since_ms = 0;
    }

    bool join_delay_elapsed =
        (g_reload_join_delay_ms <= 0) ||
        (g_tc_ctx.ready_to_join_since_ms != 0 &&
         (now_ms - g_tc_ctx.ready_to_join_since_ms) >= (uint32_t)g_reload_join_delay_ms);

    if (tail_cleared && y_cleared && join_delay_elapsed) {
        char lane_s[2];
        lane_id_str(lane_s, g_tc_ctx.target_lane);
        set_active_lane(g_tc_ctx.target_lane);
        lane_t *new_lane = lane_ptr(g_active_lane);
        if (from_lane_ptr && from_lane_ptr->task != TASK_IDLE)
            lane_stop(from_lane_ptr);
        cmd_event("RELOAD:JOINING", lane_s);
        int approach_sps = g_feed_sps;
        if (approach_sps <= 0)
            approach_sps = g_join_sps;
        lane_start(new_lane, TASK_FEED, approach_sps, true, now_ms, RELOAD_APPROACH_LIMIT_MM);
        g_tc_ctx.ready_to_join_since_ms = 0;
        g_tc_ctx.reload_tick_ms = now_ms;
        g_tc_ctx.reload_current_sps = 0;
        g_tc_ctx.last_compression_ms = 0;
        g_tc_ctx.phase_start_ms = now_ms;
        g_tc_ctx.state = TC_RELOAD_APPROACH;
    } else if ((!tail_cleared || !y_cleared) && age > (uint32_t)g_reload_y_timeout_ms) {
        tc_enter_error("RELOAD_Y_TIMEOUT");
    }
}

static void tc_tick_reload_approach(lane_t *lane, uint32_t now_ms, uint32_t age) {
    if (!lane) {
        tc_enter_error("RELOAD_APPROACH_FAULT");
        return;
    }

    if (lane->task == TASK_IDLE) {
        tc_enter_error("RELOAD_APPROACH_FAULT");
        return;
    }

    bool contacted = false;
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
        contacted = (g_buf_pos < PSF_HOME_DEVIATION_THRESHOLD_NORM);
    } else {
        contacted = (g_buf.state == BUF_COMPRESSION);
    }

    if (lane->task_limit_mm > 0.0f && lane->task_dist_mm >= lane->task_limit_mm) {
        lane_stop(lane);
        tc_enter_error("RELOAD_APPROACH_TIMEOUT");
        return;
    }

    if (contacted) {
        lane_stop(lane);
        g_tc_ctx.reload_current_sps = g_compression_sps;
        g_tc_ctx.last_compression_ms = (g_buf.state == BUF_COMPRESSION) ? now_ms : 0;
        g_tc_ctx.wall_critical_since_ms = 0;
        g_tc_ctx.reload_tick_ms = now_ms;
        g_tc_ctx.phase_start_ms = now_ms;
        g_tc_ctx.state = TC_RELOAD_FOLLOW;
    } else if (lane->task == TASK_IDLE) {
        tc_enter_error("RELOAD_APPROACH_TIMEOUT");
    }
}

static int tc_reload_follow_target_sps(lane_t *lane, uint32_t now_ms, uint32_t follow_age_ms) {
    float compression_wall_ms = sync_compression_wall_time_ms(lane);

    // We must OVER-feed to close the gap and maintain pressure on the old tail.
    // Under-feeding creates a gap because the MMU pushes slower than the extruder pulls!
    int target_sps = (int)(g_extruder_est_sps * g_reload_lean_factor);
    if (target_sps < g_press_sps) {
        target_sps = g_press_sps;
    }
    if (g_buf.state == BUF_COMPRESSION) {
        target_sps = g_compression_sps;
    } else if (g_buf.state == BUF_TENSION) {
        target_sps = g_join_sps;
    }

    if (g_buf.state != BUF_COMPRESSION && compression_wall_ms < RELOAD_COMPRESSION_SOFT_WALL_MS) {
        float urgency = (RELOAD_COMPRESSION_SOFT_WALL_MS - compression_wall_ms) /
                        RELOAD_COMPRESSION_SOFT_WALL_MS;
        urgency = clamp_f(urgency, 0.0f, 1.0f);
        int wall_trim = (int)(urgency * (float)(target_sps - g_compression_sps));
        target_sps -= wall_trim;
    }
    target_sps = clamp_i(target_sps, g_compression_sps, g_join_sps);

    if (follow_age_ms < (uint32_t)g_reload_touch_settle_ms) {
        target_sps = g_compression_sps;
    } else if (g_buf.state != BUF_COMPRESSION &&
               follow_age_ms < (uint32_t)(g_reload_touch_settle_ms + g_reload_touch_boost_ms)) {
        int floor_sps = (g_press_sps * g_reload_touch_floor_pct) / RELOAD_TOUCH_FLOOR_PERCENT_DENOM;
        if (floor_sps < g_compression_sps)
            floor_sps = g_compression_sps;
        if (target_sps < floor_sps)
            target_sps = floor_sps;
    }
    return target_sps;
}

static bool tc_reload_follow_check_jam(lane_t *lane, uint32_t now_ms, uint32_t follow_age_ms) {
    float compression_push_mm_s = sync_compression_wall_velocity_mm_s(lane);
    float compression_wall_ms = sync_compression_wall_time_ms(lane);

    if (g_buf.state == BUF_COMPRESSION) {
        if (g_tc_ctx.last_compression_ms == 0)
            g_tc_ctx.last_compression_ms = now_ms;
        bool wall_critical = compression_push_mm_s > RELOAD_COMPRESSION_HARD_PUSH_MM_S &&
                             compression_wall_ms < RELOAD_COMPRESSION_HARD_WALL_MS;
        if (wall_critical) {
            if (g_tc_ctx.wall_critical_since_ms == 0)
                g_tc_ctx.wall_critical_since_ms = now_ms;
            else if ((now_ms - g_tc_ctx.wall_critical_since_ms) >=
                     RELOAD_COMPRESSION_HARD_HOLD_MS) {
                tc_enter_error("FOLLOW_JAM");
                lane_stop(lane);
                return true;
            }
        } else {
            g_tc_ctx.wall_critical_since_ms = 0;
        }
        if ((now_ms - g_tc_ctx.last_compression_ms) >
            (uint32_t)g_follow_timeout_ms[lane_to_idx(lane->lane_id)]) {
            tc_enter_error("FOLLOW_JAM");
            lane_stop(lane);
            return true;
        }
    } else {
        g_tc_ctx.last_compression_ms = 0;
        g_tc_ctx.wall_critical_since_ms = 0;
    }

    if (follow_age_ms > RELOAD_FOLLOW_ABSOLUTE_TIMEOUT_MS) {
        tc_enter_error("FOLLOW_TIMEOUT_ABS");
        lane_stop(lane);
        return true;
    }
    return false;
}

static void tc_tick_reload_follow(lane_t *lane, uint32_t now_ms, uint32_t age) {
    // Success signal: the new filament has reached the extruder and started pulling,
    // which yanks the buffer to TENSION (or the toolhead sensor trips). Check both
    // the filtered and the raw instantaneous buffer state so a sharp pull is not
    // missed between filter updates. Any of these = loaded, stop feeding and finish.
    buf_state_t instant_buf_state = buf_state_raw();
    if (g_buf.state == BUF_TENSION || instant_buf_state == BUF_TENSION || g_toolhead_has_filament) {
        if (lane)
            lane_stop(lane);
        set_toolhead_filament(true);
        char lane_s[2];
        lane_id_str(lane_s, g_active_lane);
        cmd_event("RELOAD:LOADED", lane_s);
        g_tc_ctx.state = TC_IDLE;
        return;
    }

    if (lane->task_dist_mm >= (float)g_load_max_mm) {
        if (lane)
            lane_stop(lane);
        set_toolhead_filament(true);
        char lane_s[2];
        lane_id_str(lane_s, g_active_lane);
        cmd_event("RELOAD:LOADED", lane_s);
        g_tc_ctx.state = TC_IDLE;
        return;
    }

    if ((now_ms - g_tc_ctx.reload_tick_ms) < (uint32_t)g_sync_tick_ms)
        return;
    g_tc_ctx.reload_tick_ms = now_ms;

    uint32_t follow_age_ms = now_ms - g_tc_ctx.phase_start_ms;

    int target_sps = tc_reload_follow_target_sps(lane, now_ms, follow_age_ms);

    if (g_tc_ctx.reload_current_sps > target_sps)
        g_tc_ctx.reload_current_sps -= g_sync_ramp_dn_sps;
    else if (g_tc_ctx.reload_current_sps < target_sps)
        g_tc_ctx.reload_current_sps += g_sync_ramp_up_sps;
    g_tc_ctx.reload_current_sps = clamp_i(g_tc_ctx.reload_current_sps, 0, g_press_sps);

    if (lane) {
        if (g_tc_ctx.reload_current_sps > 0) {
            if (lane->task != TASK_FEED && lane->fault == FAULT_NONE) {
                lane_start(lane, TASK_FEED, g_tc_ctx.reload_current_sps, true, now_ms, 0);
            } else if (lane->task == TASK_FEED) {
                lane->current_sps = g_tc_ctx.reload_current_sps;
                lane->target_sps = g_tc_ctx.reload_current_sps;
                motor_enable(&lane->m, true);
                motor_set_dir(&lane->m, true);
                motor_set_rate_sps(&lane->m, g_tc_ctx.reload_current_sps);
            }
        } else if (lane->task == TASK_FEED) {
            motor_stop(&lane->m);
            lane->current_sps = 0;
            lane->target_sps = 0;
            int idx = lane->lane_id - 1;
            tmc_set_stealthchop_sps(lane->tmc, g_tmc_stealthchop_sps[idx], g_tmc_microsteps[idx]);
        }
    }

    if (tc_reload_follow_check_jam(lane, now_ms, follow_age_ms)) {
        return;
    }

    static uint32_t last_reload_report_ms = 0;
    if ((now_ms - last_reload_report_ms) >= RELOAD_STATUS_INTERVAL_MS) {
        last_reload_report_ms = now_ms;
        char ev[TC_STATUS_EVENT_BUF_LEN];
        snprintf(ev, sizeof(ev), "%s,%.1f,%.2f", buf_state_name(g_buf.state),
                 (double)sps_to_mm_per_min(g_tc_ctx.reload_current_sps), (double)g_buf_pos);
        cmd_event("BS", ev);
    }
}

// tc_state_t transition map. Entry points set the start state; tc_tick advances
// one edge per call. Any state can jump to TC_ERROR via tc_enter_error() (stops
// motion, aborts cutter); tc_abort() forces TC_IDLE. `age` = ms in current phase.
//
//   ENTRY  tc_start (TC:)        -> LOAD_START            (target == active lane)
//                                -> UNLOAD_WAIT_TH        (toolhead still loaded)
//                                -> UNLOAD_REVERSE        (otherwise)
//   ENTRY  reload_trigger (auto runout)                   -> RELOAD_WAIT_Y
//   ENTRY  tc_manual_reload                               -> RELOAD_APPROACH
//
//   -- unload branch (tc_tick_unload_states) --
//   UNLOAD_WAIT_TH   toolhead clears / timeout            -> UNLOAD_REVERSE
//   UNLOAD_REVERSE   tail already clear                   -> CUT pending? UNLOAD_CUT
//                                                            : Y gated? UNLOAD_WAIT_Y : UNLOAD_DONE
//                    tail present -> drive unload          -> UNLOAD_WAIT_OUT
//   UNLOAD_WAIT_OUT  cleared                               -> (next-after-clear, as above)
//                    still present at task end             -> ERROR UNLOAD_TIMEOUT
//   UNLOAD_CUT       fire cutter                           -> UNLOAD_WAIT_CUT
//   UNLOAD_WAIT_CUT  cut ok -> UNLOAD_REVERSE; fail/timeout-> ERROR
//   UNLOAD_WAIT_Y    Y-splitter clears                     -> UNLOAD_DONE  (timeout -> ERROR)
//   UNLOAD_DONE                                            -> SWAP
//
//   -- load branch (tc_tick_load_states) --
//   SWAP             set active = target lane              -> LOAD_START
//   LOAD_START       Y not clear -> ERROR HUB_NOT_CLEAR; else drive load -> LOAD_WAIT_OUT
//   LOAD_WAIT_OUT    OUT present -> LOAD_WAIT_TH; task end -> ERROR LOAD_TIMEOUT
//   LOAD_WAIT_TH     loaded ok -> LOAD_DONE; else          -> ERROR LOAD_TIMEOUT
//   LOAD_DONE        emit TC:DONE                          -> IDLE
//
//   -- reload branch (auto runout: hot-join new lane behind the old tail) --
//   RELOAD_WAIT_Y    tail+Y clear & join delay elapsed     -> RELOAD_APPROACH (timeout -> ERROR)
//   RELOAD_APPROACH  buffer contact (COMPRESSION / PSF)    -> RELOAD_FOLLOW   (limit -> ERROR)
//   RELOAD_FOLLOW    TENSION | toolhead | LOAD_MAX -> IDLE (RELOAD:LOADED); jam/abs-timeout -> ERROR
void tc_tick(uint32_t now_ms) {
    uint32_t age = now_ms - g_tc_ctx.phase_start_ms;
    lane_t *lane = lane_ptr(g_active_lane);

    switch (g_tc_ctx.state) {
    case TC_IDLE:
    case TC_ERROR:
        return;

    case TC_UNLOAD_CUT:
    case TC_UNLOAD_WAIT_CUT:
    case TC_UNLOAD_REVERSE:
    case TC_UNLOAD_WAIT_OUT:
    case TC_UNLOAD_WAIT_Y:
    case TC_UNLOAD_WAIT_TH:
    case TC_UNLOAD_DONE:
        tc_tick_unload_states(lane, now_ms, age);
        break;

    case TC_SWAP:
    case TC_LOAD_START:
    case TC_LOAD_WAIT_OUT:
    case TC_LOAD_WAIT_TH:
    case TC_LOAD_DONE:
        tc_tick_load_states(lane, now_ms, age);
        break;

    case TC_RELOAD_WAIT_Y:
        tc_tick_reload_wait_y(lane, now_ms, age);
        break;

    case TC_RELOAD_APPROACH:
        tc_tick_reload_approach(lane, now_ms, age);
        break;

    case TC_RELOAD_FOLLOW:
        tc_tick_reload_follow(lane, now_ms, age);
        break;
    }
}
