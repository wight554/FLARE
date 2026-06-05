#include "sync_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cutter.h"
#include "motion.h"
#include "protocol.h"
#include "toolchange.h"

sync_state_t g_sync_state = SYNC_OFF;
bool sync_auto_started = false;
bool sync_tail_assist_active = false;
uint32_t sync_idle_since_ms = 0;
int sync_current_sps = 0;

int g_baseline_target_sps = CONF_BASELINE_SPS;
int g_baseline_sps = CONF_BASELINE_SPS;
float g_baseline_alpha = FLARE_INT_BASELINE_ALPHA;
const flow_schedule_point_t g_flow_sched_config[CONF_FLOW_SCHED_CAP] = CONF_FLOW_SCHED;
flow_schedule_point_t g_flow_sched_runtime[CONF_FLOW_SCHED_CAP] = CONF_FLOW_SCHED;
int g_flow_sched_live_delta[CONF_FLOW_SCHED_CAP] = {0};
int g_flow_sched_len = CONF_FLOW_SCHED_LEN;
uint32_t sync_fast_brake_until_ms = 0;

bool sync_compression_recovery_active = false;
uint32_t sync_continuous_compression_since_ms = 0;
uint32_t sync_post_compression_boost_until_ms = 0;
uint32_t sync_recent_negative_until_ms = 0;
uint32_t sync_tension_pin_since_ms = 0;

bool g_sync_cannot_refill_warned = false;
bool g_sync_cannot_relieve_warned = false;

uint32_t sync_last_tick_ms = 0;
uint32_t sync_last_evt_ms = 0;
float extruder_est_sps = 0.0f;
float extruder_est_prev_sps = 0.0f;
uint32_t extruder_est_last_update_ms = 0;
uint32_t last_slope_update_ms = 0;

/* Buffer-lock (BL) lifecycle sub-states — active while g_sync_state == SYNC_RETRACT_ASSIST */
typedef enum {
    BL_IDLE = 0,
    BL_PRIME,  /* driving lane toward armed extreme at SYNC_MAX_SPS */
    BL_LOCKED, /* holding at extreme; motor energized, zero net feed */
    BL_FOLLOW, /* event-triggered follow-on retract concurrent with extruder */
} bl_sub_state_t;

static bl_sub_state_t g_bl_sub_state = BL_IDLE;
static buf_state_t g_bl_target_state = BUF_TENSION;
buf_state_t g_bl_goal_override = BUF_NEUTRAL;
static uint32_t g_bl_prime_start_ms = 0;      /* when prime search began */
static float g_bl_prime_mm_per_s = 0.0f;      /* stab speed in mm/s */
static float g_bl_prime_cap_mm = 0.0f;        /* abs 1: outer safety cap = BUF_MAX_TRAVEL_MM */
static bool g_bl_prime_switch_hit = false;    /* switch fired during search phase */
static uint32_t g_bl_prime_post_start_ms = 0; /* when post-click settle began */
static float g_bl_prime_post_cap_mm = 0.0f;   /* abs 2: post-click extra travel = (max-span)/2 */
static float g_bl_follow_mm = 0.0f;           /* armed follow-on distance; 0 = disabled */
static float g_bl_follow_rate_mmpm = 0.0f;    /* armed follow-on rate (mm/min) */
static uint32_t g_bl_follow_start_ms = 0;     /* when FOLLOW motion began */
static float g_bl_follow_mm_per_s = 0.0f;     /* FOLLOW commanded speed in mm/s */
static uint32_t g_bl_watchdog_ms = 0;
static float g_bl_follow_traveled_mm = 0.0f;
static uint32_t g_bl_last_tick_ms = 0;
static int g_bl_follow_cur_sps = 0;           /* FOLLOW current ramped rate */
static int g_bl_follow_target_sps = 0;        /* FOLLOW target rate (clamped) */
static uint32_t g_bl_follow_ramp_tick_ms = 0; /* last FOLLOW ramp step */

bool g_bl_autostart_suppressed = false;
bool g_sync_tension_transitioned = false;

bool g_boot_stabilizing = false;
uint32_t g_boot_stabilize_deadline_ms = 0;
lane_t *g_boot_stabilize_lane = NULL;
bool g_boot_stabilize_forward = false;
static bool g_buffer_stabilize_emit_events = false;
static uint32_t g_boot_stabilize_started_ms = 0;
static uint32_t g_stab_stagnant_since_ms = 0;
static float g_boot_stabilize_start_pos = 0.0f;

static buffer_service_mode_t g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
static uint32_t g_idle_compression_since_ms = 0;

int g_settle_history[16] = {0};
uint8_t g_settle_history_count = 0;
uint32_t g_last_baseline_update_ms = 0;
float g_last_baseline_update_mm = 0.0f;
float g_sync_mmu_total_mm = 0.0f;

void sync_init(uint32_t now_ms) {
    buf_state_t raw = buf_state_raw();
    buf_force_stable_state(raw, now_ms);

    g_sync_tension_transitioned = false; /* Never false-trigger tension auto-start on boot! */
    g_sync_state = SYNC_OFF;
    sync_auto_started = false;
    sync_tail_assist_active = false;
    sync_current_sps = 0;
    g_buf_pos_prev = g_buf_pos;
    g_vel_norm = 0.0f;
    g_vel_norm_f = 0.0f;
}

void boot_stabilize_stop(void) {
    if (g_boot_stabilize_lane) {
        motor_stop(&g_boot_stabilize_lane->m);
    }
    g_boot_stabilizing = false;
    g_boot_stabilize_deadline_ms = 0;
    g_boot_stabilize_lane = NULL;
    g_buffer_stabilize_emit_events = false;
    g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
}

void boot_stabilize_disarm(void) {
    g_boot_stabilizing = false;
    g_boot_stabilize_deadline_ms = 0;
    g_boot_stabilize_lane = NULL;
    g_buffer_stabilize_emit_events = false;
    g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
}

bool buffer_stabilize_controller_idle(void) {
    /* TC_ERROR: TC concluded (failed), motors stopped — allow stabilize. */
    if ((g_tc_ctx.state != TC_IDLE && g_tc_ctx.state != TC_ERROR) || cutter_busy() || sync_enabled)
        return false;
    if (g_lane_l1.task != TASK_IDLE || g_lane_l2.task != TASK_IDLE)
        return false;
    return true;
}

bool buffer_negative_sync_eligible(void) {
    lane_t *active = lane_ptr(active_lane);
    return active && lane_out_present(active);
}

bool buffer_stabilize_start_internal(uint32_t now_ms, bool emit_events,
                                     buffer_service_mode_t mode) {
    if (g_boot_stabilizing)
        return true;
    if (!buffer_stabilize_controller_idle())
        return false;
    if (BUF_SENSOR_TYPE != 0) {
        /* D23 Gate A: type-P idle stabilize. Drive toward goal via the shared
           goal-relative path below (buf_state_raw() zones pick direction, the
           stabilize tick stops at BUF_NEUTRAL = near goal), but only when
           filament is present on a board-local sensor. Unloaded, the buffer
           rests at the tension/home rail by design, so driving the motor would
           dry-spin against the home stop. */
        lane_t *pl = pick_boot_stabilize_lane();
        if (!(lane_out_present(pl) || lane_in_present(pl)))
            return true;
    }
    if (mode == BUFFER_SERVICE_NEG_SYNC && sync_guard_active)
        return true;

    buf_state_t buf_state = buf_state_raw();
    lane_t *stab_lane = NULL;
    bool forward = false;

    if (mode == BUFFER_SERVICE_NEG_SYNC) {
        if (buf_state != BUF_COMPRESSION || !buffer_negative_sync_eligible())
            return true;
        stab_lane = lane_ptr(active_lane);
        forward = false;
    } else {
        if (buf_state != BUF_COMPRESSION && buf_state != BUF_TENSION)
            return true;
        stab_lane = pick_boot_stabilize_lane();
        forward = (buf_state == BUF_TENSION);
    }

    if (!stab_lane || BUF_STAB_SPS <= 0)
        return false;

    g_boot_stabilizing = true;
    g_boot_stabilize_deadline_ms = now_ms + 10000u;
    g_boot_stabilize_lane = stab_lane;
    g_buffer_stabilize_emit_events = emit_events;
    g_buffer_service_mode = mode;
    g_idle_compression_since_ms = 0;
    g_boot_stabilize_started_ms = now_ms;
    g_stab_stagnant_since_ms = now_ms;
    g_boot_stabilize_start_pos = g_buf_pos;

    motor_enable(&stab_lane->m, true);
    motor_set_dir(&stab_lane->m, forward);
    motor_set_rate_sps(&stab_lane->m, BUF_STAB_SPS);
    g_boot_stabilize_forward = forward;

    if (g_buffer_stabilize_emit_events)
        cmd_event("BUF_STAB", "START");
    return true;
}

bool buffer_stabilize_request(uint32_t now_ms) {
    g_idle_compression_since_ms = 0;
    /* BS (and BL timeout, which routes here) ends the buffer-lock context: drop
       the BL-implied goal override back to the configured BUF_GOAL. */
    g_bl_goal_override = BUF_NEUTRAL;
    return buffer_stabilize_start_internal(now_ms, true, BUFFER_SERVICE_STABILIZE);
}

void buffer_stabilize_cancel(void) {
    boot_stabilize_stop();
}

/* True once a stabilize has settled the buffer at goal. A successful DONE forces
   g_buf_stable_state = BUF_NEUTRAL (predict parks a hair tension-side of goal, by
   design); a STAGNANT abort does not. Lets the boot retry stop on the first real
   success instead of chasing raw position past the predict's safe early stop. */
bool boot_stabilize_settled(void) {
    return !g_boot_stabilizing && g_buf.state == BUF_NEUTRAL;
}

void boot_stabilize_start(uint32_t now_ms) {
    /* emit_events=true: boot-stab shares the BS path; keeping it observable
       (BUF_STAB:START/DONE/STAGNANT) is essential for diagnosing boot behavior. */
    (void)buffer_stabilize_start_internal(now_ms, true, BUFFER_SERVICE_STABILIZE);
}

static bool boot_stabilize_tick_type_p(uint32_t now_ms) {
    if (BUF_SENSOR_TYPE != 1)
        return false;

    if (g_boot_stabilizing) {
        bool at_rail = (g_buf_analog_saturated_since_ms != 0) || (fabsf(g_buf_pos) >= 0.99f);
        if (at_rail) {
            if ((int32_t)(now_ms - g_boot_stabilize_started_ms) >= PSF_STAB_RAIL_BREAK_MS) {
                if (g_buffer_stabilize_emit_events)
                    cmd_event("BUF_STAB", "STAGNANT_TIMEOUT");
                boot_stabilize_stop();
                return true;
            }
            g_boot_stabilize_start_pos = g_buf_pos;
            g_stab_stagnant_since_ms = now_ms;
        } else if ((int32_t)(now_ms - g_stab_stagnant_since_ms) >= PSF_STAB_STAGNANT_MS) {
            float change = fabsf(g_buf_pos - g_boot_stabilize_start_pos);
            if (change < PSF_STAB_STAGNANT_NORM) {
                if (g_buffer_stabilize_emit_events)
                    cmd_event("BUF_STAB", "STAGNANT_TIMEOUT");
                boot_stabilize_stop();
                return true;
            }
        }
    }

    if (g_boot_stabilize_lane) {
        float goal = psf_goal_norm();
        float predicted = g_buf_pos + SYNC_STAB_PREDICT_LEAD_S * g_vel_norm_f;
        bool reached = g_boot_stabilize_forward ? (predicted >= goal) : (predicted <= goal);
        if (reached) {
            buf_force_stable_state(BUF_NEUTRAL, now_ms);
            if (g_buffer_stabilize_emit_events)
                cmd_event("BUF_STAB", "DONE");
            boot_stabilize_stop();
            return true;
        }
    }
    return false;
}

void buffer_stabilize_tick(uint32_t now_ms) {
    if (sync_guard_active && g_boot_stabilizing &&
        g_buffer_service_mode == BUFFER_SERVICE_NEG_SYNC) {
        boot_stabilize_stop();
        return;
    }

    if (!g_boot_stabilizing) {
        if (!buffer_stabilize_controller_idle()) {
            g_idle_compression_since_ms = 0;
        } else if (buf_state_raw() == BUF_COMPRESSION && buffer_negative_sync_eligible()) {
            if (g_idle_compression_since_ms == 0)
                g_idle_compression_since_ms = now_ms;
            if (POST_PRINT_STAB_DELAY_MS <= 0 ||
                (now_ms - g_idle_compression_since_ms) >= (uint32_t)POST_PRINT_STAB_DELAY_MS) {
                (void)buffer_stabilize_start_internal(now_ms, true, BUFFER_SERVICE_NEG_SYNC);
            }
        } else {
            g_idle_compression_since_ms = 0;
        }
    }

    if (!g_boot_stabilizing)
        return;

    if (!g_boot_stabilize_lane) {
        boot_stabilize_disarm();
        return;
    }

    if (g_boot_stabilize_lane->task != TASK_IDLE) {
        boot_stabilize_disarm();
        return;
    }

    if (!buffer_stabilize_controller_idle()) {
        boot_stabilize_stop();
        return;
    }

    if (boot_stabilize_tick_type_p(now_ms))
        return;

    buf_state_t raw_state = buf_state_raw();

    if (g_buffer_service_mode == BUFFER_SERVICE_NEG_SYNC) {
        if (raw_state == BUF_NEUTRAL) {
            buf_force_stable_state(BUF_NEUTRAL, now_ms);
            if (g_buffer_stabilize_emit_events)
                cmd_event("BUF_STAB", "DONE");
            boot_stabilize_stop();
            return;
        }

        if (raw_state == BUF_TENSION) {
            g_buffer_service_mode = BUFFER_SERVICE_STABILIZE;
            g_boot_stabilize_deadline_ms = now_ms + 10000u;
            motor_enable(&g_boot_stabilize_lane->m, true);
            motor_set_dir(&g_boot_stabilize_lane->m, true);
            motor_set_rate_sps(&g_boot_stabilize_lane->m, BUF_STAB_SPS);
            g_boot_stabilize_forward = true;
            return;
        }
    } else {
        if (raw_state == BUF_NEUTRAL) {
            buf_force_stable_state(BUF_NEUTRAL, now_ms);
            if (g_buffer_stabilize_emit_events)
                cmd_event("BUF_STAB", "DONE");
            boot_stabilize_stop();
            return;
        }

        bool need_forward = (raw_state == BUF_TENSION);
        if (g_boot_stabilize_forward != need_forward) {
            motor_set_dir(&g_boot_stabilize_lane->m, need_forward);
            g_boot_stabilize_forward = need_forward;
            g_boot_stabilize_deadline_ms = now_ms + 10000u;
            if (g_buffer_stabilize_emit_events)
                cmd_event("BUF_STAB", "REVERSE");
        }
    }

    if ((int32_t)(now_ms - g_boot_stabilize_deadline_ms) >= 0) {
        if (g_buffer_stabilize_emit_events)
            cmd_event("BUF_STAB", "TIMEOUT");
        boot_stabilize_stop();
    }
}

void flow_schedule_refresh_scalar(void) {
    g_flow_sched_len = 1;
    g_flow_sched_runtime[0].flow_sps = g_baseline_target_sps;
    g_flow_sched_runtime[0].baseline_sps = g_baseline_target_sps;
    g_flow_sched_runtime[0].bias_milli =
        clamp_i((int)(SYNC_COMPRESSION_BIAS_FRAC * 1000.0f + 0.5f), 0, 700);
    for (int i = 0; i < CONF_FLOW_SCHED_CAP; i++) {
        g_flow_sched_live_delta[i] = 0;
    }
}

void flow_schedule_reset_runtime(void) {
    g_flow_sched_len = CONF_FLOW_SCHED_LEN;
    if (g_flow_sched_len < 1)
        g_flow_sched_len = 1;
    if (g_flow_sched_len > CONF_FLOW_SCHED_CAP)
        g_flow_sched_len = CONF_FLOW_SCHED_CAP;

    for (int i = 0; i < g_flow_sched_len; i++) {
        g_flow_sched_runtime[i] = g_flow_sched_config[i];
    }
    for (int i = 0; i < CONF_FLOW_SCHED_CAP; i++) {
        g_flow_sched_live_delta[i] = 0;
    }

    if (CONF_FLOW_SCHED_LEN <= 1) {
        flow_schedule_refresh_scalar();
    }
}

int lerp_i(int a, int b, int x, int x0, int x1) {
    int span = x1 - x0;
    if (span <= 0)
        return a;
    return a + (int)(((int64_t)(b - a) * (int64_t)(x - x0)) / (int64_t)span);
}

int flow_active_segment(int flow_sps) {
    int len = flow_sched_len_clamped();
    flow_schedule_point_t *sched = g_flow_sched_runtime;
    if (len <= 1 || flow_sps <= sched[0].flow_sps)
        return 0;

    int last = len - 1;
    if (flow_sps >= sched[last].flow_sps)
        return last;

    for (int i = 0; i < last; i++) {
        if (flow_sps <= sched[i + 1].flow_sps)
            return i;
    }
    return last;
}

flow_param_t flow_param(int flow_sps) {
    int len = flow_sched_len_clamped();
    flow_schedule_point_t *sched = g_flow_sched_runtime;
    int segment = flow_active_segment(flow_sps);
    flow_param_t p;

    if (len <= 1 || flow_sps <= sched[0].flow_sps) {
        p = (flow_param_t){sched[0].baseline_sps, sched[0].bias_milli};
        p.baseline_sps += g_flow_sched_live_delta[segment];
        return p;
    }

    int last = len - 1;
    if (flow_sps >= sched[last].flow_sps) {
        p = (flow_param_t){sched[last].baseline_sps, sched[last].bias_milli};
        p.baseline_sps += g_flow_sched_live_delta[segment];
        return p;
    }

    for (int i = 0; i < last; i++) {
        int x0 = sched[i].flow_sps;
        int x1 = sched[i + 1].flow_sps;
        if (flow_sps >= x0 && flow_sps <= x1) {
            p.baseline_sps =
                lerp_i(sched[i].baseline_sps, sched[i + 1].baseline_sps, flow_sps, x0, x1);
            p.bias_milli = lerp_i(sched[i].bias_milli, sched[i + 1].bias_milli, flow_sps, x0, x1);
            p.baseline_sps += g_flow_sched_live_delta[segment];
            return p;
        }
    }

    p = (flow_param_t){sched[last].baseline_sps, sched[last].bias_milli};
    p.baseline_sps += g_flow_sched_live_delta[segment];
    return p;
}

int lane_motion_sps(lane_t *lane) {
    if (!lane)
        return 0;
    if (lane->current_sps > 0)
        return lane->current_sps;
    if (g_tc_ctx.state == TC_RELOAD_FOLLOW && g_tc_ctx.reload_current_sps > 0)
        return g_tc_ctx.reload_current_sps;
    return sync_current_sps;
}

lane_t *pick_boot_stabilize_lane(void) {
    lane_t *stab_lane = lane_ptr(active_lane);
    if (stab_lane)
        return stab_lane;
    if (lane_out_present(&g_lane_l1) && !lane_out_present(&g_lane_l2))
        return &g_lane_l1;
    if (lane_out_present(&g_lane_l2) && !lane_out_present(&g_lane_l1))
        return &g_lane_l2;
    return &g_lane_l1;
}

int flow_sched_len_clamped(void) {
    if (g_flow_sched_len < 1)
        return 1;
    if (g_flow_sched_len > CONF_FLOW_SCHED_CAP)
        return CONF_FLOW_SCHED_CAP;
    return g_flow_sched_len;
}

void baseline_update_on_settle(uint32_t neutral_dwell_ms, uint32_t now_ms) {
    if (neutral_dwell_ms <= 500) {
        g_settle_history_count = 0;
        return;
    }

    uint8_t n = CONF_BASELINE_SETTLE_COUNT;
    if (n > 16)
        n = 16;
    if (n < 1)
        n = 1;

    for (int i = 15; i > 0; i--) {
        g_settle_history[i] = g_settle_history[i - 1];
    }
    g_settle_history[0] = sync_current_sps;

    if (g_settle_history_count < n) {
        g_settle_history_count++;
    }

    if (g_settle_history_count >= n) {
        int min_sps = g_settle_history[0];
        int max_sps = g_settle_history[0];
        int sum_sps = 0;
        for (int i = 0; i < n; i++) {
            if (g_settle_history[i] < min_sps)
                min_sps = g_settle_history[i];
            if (g_settle_history[i] > max_sps)
                max_sps = g_settle_history[i];
            sum_sps += g_settle_history[i];
        }

        float mean_sps = (float)sum_sps / (float)n;
        float variance_frac = (mean_sps > 0.1f) ? ((float)(max_sps - min_sps) / mean_sps) : 0.0f;

        if (variance_frac <= CONF_BASELINE_VARIANCE_REJECT_FRAC) {
            uint32_t elapsed_ms = now_ms - g_last_baseline_update_ms;
            float elapsed_mm = g_sync_mmu_total_mm - g_last_baseline_update_mm;

            if (g_last_baseline_update_ms == 0 || (elapsed_ms >= CONF_BASELINE_COOLDOWN_MS &&
                                                   elapsed_mm >= CONF_BASELINE_COOLDOWN_MM)) {

                int flow_sps = (int)extruder_est_sps;
                int segment = flow_active_segment(flow_sps);
                flow_param_t fp = flow_param(flow_sps);
                int new_baseline =
                    (int)(FLARE_INT_BASELINE_ALPHA * (float)sync_current_sps +
                          (1.0f - FLARE_INT_BASELINE_ALPHA) * (float)fp.baseline_sps);
                if (new_baseline > fp.baseline_sps) {
                    g_flow_sched_live_delta[segment] += new_baseline - fp.baseline_sps;
                    g_baseline_sps = new_baseline;
                }

                g_last_baseline_update_ms = now_ms;
                g_last_baseline_update_mm = g_sync_mmu_total_mm;
                g_settle_history_count = 0;
            }
        } else {
            g_settle_history_count = 0;
        }
    }
}

int baseline_control_floor_sps(void) {
    flow_param_t fp = flow_param((int)extruder_est_sps);
    return (fp.baseline_sps > g_baseline_target_sps) ? fp.baseline_sps : g_baseline_target_sps;
}

int sync_neutral_anti_tension_floor_sps(buf_state_t s, lane_t *lane, float error_norm,
                                        float deadband_norm, int neutral_target_sps,
                                        uint32_t now_ms) {
    if (g_sync_state != SYNC_ACTIVE || s != BUF_NEUTRAL || !lane)
        return 0;
    if (lane->task != TASK_FEED || lane->fault != FAULT_NONE)
        return 0;
    (void)now_ms;

    if (BUF_SENSOR_TYPE == 0) {
        if (error_norm >= -deadband_norm)
            return 0;

        int demand_floor_sps = (int)(extruder_est_sps * 1.05f);
        if (demand_floor_sps < SYNC_MIN_SPS)
            demand_floor_sps = SYNC_MIN_SPS;
        if (neutral_target_sps > 0 && demand_floor_sps > neutral_target_sps) {
            demand_floor_sps = neutral_target_sps;
        }

        int baseline_floor_sps = baseline_control_floor_sps();
        int assist_floor_sps =
            (int)((float)baseline_floor_sps * SYNC_NEUTRAL_ANTI_TENSION_FLOOR_FRAC);
        if (assist_floor_sps > demand_floor_sps)
            assist_floor_sps = demand_floor_sps;
        if (assist_floor_sps <= SYNC_MIN_SPS)
            return 0;
        return assist_floor_sps;
    }

    if (error_norm > deadband_norm)
        return 0;

    /* Type-P legacy path: unconditional refill floor. The floor is baseline-
     * derived so it can never itself drive the buffer toward TENSION. */
    int baseline_floor_sps = baseline_control_floor_sps();
    int assist_floor_sps = (int)((float)baseline_floor_sps * SYNC_NEUTRAL_ANTI_TENSION_FLOOR_FRAC);
    if (assist_floor_sps <= SYNC_MIN_SPS)
        return 0;

    return assist_floor_sps;
}

int sync_clamp_max_sps(int requested_sps) {
    return motion_clamp_rate_sps(requested_sps);
}

void sync_set_state(sync_state_t new_state) {
    if (g_sync_state == new_state)
        return;
    /* Seed the type-P smoothing filter to the current feed on (re)entering active
       sync so it doesn't slew from a stale value left by a prior session. */
    if (new_state == SYNC_ACTIVE)
        g_psf_target_filt = (float)sync_current_sps;
    g_sync_state = new_state;
    g_sync_tension_transitioned = false;
    g_sync_refill_effort_mm = 0.0f;
    g_sync_relieve_effort_mm = 0.0f;
    g_sync_cannot_refill_warned = false;
    g_sync_cannot_relieve_warned = false;
}

void sync_retract_assist_set(bool enabled) {
    lane_t *lane = lane_ptr(active_lane);
    if (enabled) {
        sync_current_sps = 0;
        sync_auto_started = false;
        sync_tail_assist_active = false;
        sync_idle_since_ms = 0;
        g_bl_autostart_suppressed = false;
        sync_set_state(SYNC_RETRACT_ASSIST);
        if (lane && lane->task == TASK_FEED)
            lane_stop(lane);
    } else {
        if (g_sync_state == SYNC_RETRACT_ASSIST) {
            /* Stop any BL-driven motor motion before transitioning to SYNC_OFF */
            if (g_bl_sub_state != BL_IDLE && lane) {
                motor_set_rate_sps(&lane->m, 0);
                motor_enable(&lane->m, false);
            }
            g_bl_sub_state = BL_IDLE;
            g_bl_prime_start_ms = 0;
            g_bl_prime_mm_per_s = 0.0f;
            g_bl_prime_cap_mm = 0.0f;
            g_bl_prime_switch_hit = false;
            g_bl_prime_post_start_ms = 0;
            g_bl_prime_post_cap_mm = 0.0f;
            g_bl_follow_mm = 0.0f;
            g_bl_follow_rate_mmpm = 0.0f;
            g_bl_follow_start_ms = 0;
            g_bl_follow_mm_per_s = 0.0f;
            g_bl_watchdog_ms = 0;
            sync_set_state(SYNC_OFF);
            /* Suppress auto-start: buffer is at the BL extreme, not extruder-driven. */
            g_bl_autostart_suppressed = true;
        }
    }
}

void sync_retract_assist_release(uint32_t now_ms) {
    bool was_active = (g_sync_state == SYNC_RETRACT_ASSIST);
    sync_retract_assist_set(false);
    if (was_active) {
        (void)buffer_stabilize_start_internal(now_ms, true, BUFFER_SERVICE_NEG_SYNC);
    }
}

void handle_bl_watchdog_timeout(uint32_t now_ms) {
    cmd_event("EV:BL", "TIMEOUT");
    sync_retract_assist_release(now_ms);
    sync_bl_clear_autostart_suppress();
    (void)buffer_stabilize_request(now_ms);
}

void sync_bl_clear_autostart_suppress(void) {
    g_bl_autostart_suppressed = false;
}

bool sync_retract_assist_enabled(void) {
    return g_sync_state == SYNC_RETRACT_ASSIST;
}

void sync_buffer_lock_arm(buf_state_t target, float follow_mm, float follow_rate_mmpm,
                          uint32_t now_ms) {
    lane_t *lane = lane_ptr(active_lane);
    if (!lane)
        return;

    /* Stop any in-progress BL motor drive before re-arming */
    if (g_bl_sub_state != BL_IDLE) {
        motor_set_rate_sps(&lane->m, 0);
        motor_enable(&lane->m, false);
    }

    /* Enter (or stay in) SYNC_RETRACT_ASSIST */
    sync_current_sps = 0;
    sync_auto_started = false;
    sync_tail_assist_active = false;
    sync_idle_since_ms = 0;
    sync_set_state(SYNC_RETRACT_ASSIST);
    if (lane->task == TASK_FEED)
        lane_stop(lane);

    g_bl_target_state = target;
    /* Park the buffer goal at the armed rail until BS/timeout clears it. */
    g_bl_goal_override = target;

    /* Prime runs at SYNC_MAX_SPS. Overtravel is controlled by the two-phase
     * gates:
     *   phase 1 — search until switch fires (outer cap: BUF_MAX_TRAVEL_MM)
     *   phase 2 — lock at switch click (no post-settle travel). */
    int idx = lane->lane_id - 1;
    /* Type-P (analog) primes gently at BUF_STAB_SPS: the PSF EMA filter lags,
     * so a full-speed SYNC_MAX_SPS prime overshoots PSF_HOME_THRESHOLD_NORM and
     * slams the rail before the motor reads the threshold and stops. Type-D
     * (switch) is bang-bang — the click stops it instantly, so full speed is fine. */
    int prime_sps = (BUF_SENSOR_TYPE == 1) ? sync_clamp_max_sps(BUF_STAB_SPS)
                                           : sync_clamp_max_sps(SYNC_MAX_SPS);
    float mm_per_s = (float)prime_sps * MM_PER_STEP[idx];
    float max_cap_mm = (BUF_MAX_TRAVEL_MM > 0) ? (float)BUF_MAX_TRAVEL_MM : 25.0f;
    g_bl_prime_start_ms = now_ms;
    g_bl_prime_mm_per_s = mm_per_s;
    g_bl_prime_cap_mm = max_cap_mm;
    g_bl_prime_switch_hit = false;
    g_bl_prime_post_start_ms = 0;
    g_bl_prime_post_cap_mm = 0.0f;
    /* Subtract BUF_MAX_TRAVEL_MM/2 so FOLLOW finishes near NEUTRAL rather
     * than at the switch click. After the extruder stops the MMU keeps
     * draining; parking at the switch click leaves one step before the
     * mechanical hard end. If the adjusted distance is ≤ 0, the macro
     * doesn't need a follow-on for such a short move — use passive lock. */
    {
        float half_travel = (BUF_MAX_TRAVEL_MM > 0) ? ((float)BUF_MAX_TRAVEL_MM * 0.5f) : 12.5f;
        float effective_follow_mm =
            (follow_mm > 0.0f && follow_rate_mmpm > 0.0f) ? (follow_mm - half_travel) : 0.0f;
        g_bl_follow_mm = (effective_follow_mm > 0.0f) ? effective_follow_mm : 0.0f;
        g_bl_follow_rate_mmpm = (g_bl_follow_mm > 0.0f) ? follow_rate_mmpm : 0.0f;
    }
    g_bl_follow_start_ms = 0;
    g_bl_follow_mm_per_s = 0.0f;
    g_bl_follow_traveled_mm = 0.0f;
    g_bl_last_tick_ms = now_ms;

    g_bl_watchdog_ms = 0;
    g_bl_sub_state = BL_PRIME;

    /* BL:T → retract (forward=false) to pull buffer toward tension extreme.
     * BL:C → extrude (forward=true) to push buffer toward compression extreme. */
    bool forward = (target == BUF_COMPRESSION);
    motor_enable(&lane->m, true);
    motor_set_dir(&lane->m, forward);
    motor_set_rate_sps(&lane->m, prime_sps);

    cmd_event("BL", "PRIME");
}

const char *sync_buffer_lock_arm_str(void) {
    if (g_sync_state != SYNC_RETRACT_ASSIST || g_bl_sub_state == BL_IDLE)
        return "0";
    return (g_bl_target_state == BUF_TENSION) ? "T" : "C";
}

bool sync_buffer_lock_motor_moving(void) {
    return g_sync_state == SYNC_RETRACT_ASSIST &&
           (g_bl_sub_state == BL_PRIME || g_bl_sub_state == BL_FOLLOW);
}

static void sync_buffer_lock_prime(lane_t *lane, uint32_t now_ms) {
    bool reached = false;
    if (BUF_SENSOR_TYPE == 1) {
        if (g_bl_target_state == BUF_TENSION)
            reached = (g_buf_pos <= -PSF_HOME_THRESHOLD_NORM);
        else if (g_bl_target_state == BUF_COMPRESSION)
            reached = (g_buf_pos >= PSF_HOME_THRESHOLD_NORM);
    } else {
        buf_state_t raw = buf_state_raw();
        reached = (raw == g_bl_target_state);
    }

    /* Phase 1 — search: outer safety cap fires if switch never triggers */
    float traveled_mm =
        (g_bl_prime_mm_per_s > 0.0f)
            ? ((float)(now_ms - g_bl_prime_start_ms) / 1000.0f * g_bl_prime_mm_per_s)
            : g_bl_prime_cap_mm;
    bool deadline_hit = (!g_bl_prime_switch_hit && traveled_mm >= g_bl_prime_cap_mm);

    /* Switch click: transition to post-click settle phase */
    if (!g_bl_prime_switch_hit && reached) {
        g_bl_prime_switch_hit = true;
        g_bl_prime_post_start_ms = now_ms;
    }

    /* Phase 2 — post-click settle: continue (max-span)/2 past the switch */
    bool post_done = false;
    if (g_bl_prime_switch_hit) {
        float post_mm =
            (g_bl_prime_mm_per_s > 0.0f)
                ? ((float)(now_ms - g_bl_prime_post_start_ms) / 1000.0f * g_bl_prime_mm_per_s)
                : g_bl_prime_post_cap_mm;
        post_done = (post_mm >= g_bl_prime_post_cap_mm);
    }

    if (post_done || deadline_hit) {
        /* Prime done — stop motor but keep enabled for holding torque */
        motor_set_rate_sps(&lane->m, 0);
        /* motor_enable stays true: locked hold needs energized stepper */

        if (deadline_hit) {
            cmd_event("EV:BL", "PRIME_BOUND");
        }

        g_bl_sub_state = BL_LOCKED;
        g_bl_watchdog_ms = now_ms + BL_WATCHDOG_DEFAULT_MS;
        cmd_event("BL", "LOCKED");
    }
}

static void sync_buffer_lock_locked(lane_t *lane, uint32_t now_ms) {
    if (g_bl_follow_mm > 0.0f) {
        bool lock_broken = false;
        if (BUF_SENSOR_TYPE == 1) {
            if (g_bl_target_state == BUF_TENSION)
                lock_broken = (g_buf_pos > -PSF_HOME_THRESHOLD_NORM);
            else if (g_bl_target_state == BUF_COMPRESSION)
                lock_broken = (g_buf_pos < PSF_HOME_THRESHOLD_NORM);
        } else {
            buf_state_t raw = buf_state_raw();
            lock_broken = (raw != g_bl_target_state);
        }

        if (lock_broken) {
            int idx = lane->lane_id - 1;
            int follow_sps = (int)(g_bl_follow_rate_mmpm / 60.0f / MM_PER_STEP[idx] + 0.5f);
            if (follow_sps < 1)
                follow_sps = 1;
            follow_sps = sync_clamp_max_sps(follow_sps);
            bool forward = (g_bl_target_state == BUF_COMPRESSION);
            motor_set_dir(&lane->m, forward);
            int start_sps = RAMP_STEP_SPS;
            if (start_sps > follow_sps)
                start_sps = follow_sps;
            if (start_sps < 1)
                start_sps = 1;
            motor_set_rate_sps(&lane->m, start_sps);

            g_bl_follow_start_ms = now_ms;
            g_bl_follow_cur_sps = start_sps;
            g_bl_follow_target_sps = follow_sps;
            g_bl_follow_ramp_tick_ms = now_ms;
            g_bl_follow_mm_per_s = (float)start_sps * MM_PER_STEP[idx];
            g_bl_follow_traveled_mm = 0.0f;
            g_bl_last_tick_ms = now_ms;
            g_bl_sub_state = BL_FOLLOW;
            cmd_event("BL", "FOLLOW");
            return;
        }
    }
    if (g_bl_watchdog_ms != 0 && (int32_t)(now_ms - g_bl_watchdog_ms) >= 0) {
        handle_bl_watchdog_timeout(now_ms);
    }
}

static void sync_buffer_lock_follow(lane_t *lane, uint32_t now_ms) {
    int idx = lane->lane_id - 1;
    float dt_s = (float)(now_ms - g_bl_last_tick_ms) / 1000.0f;
    if (dt_s < 0.0001f)
        dt_s = 0.0001f;
    if (dt_s > 0.1f)
        dt_s = 0.001f;

    if (BUF_SENSOR_TYPE == 1) {
        bool rail_hit = (g_bl_target_state == BUF_TENSION)
                            ? (g_buf_pos <= -PSF_FOLLOW_RAIL_NORM)
                            : (g_buf_pos >= PSF_FOLLOW_RAIL_NORM);
        if (rail_hit) {
            motor_set_rate_sps(&lane->m, 0);
            g_bl_sub_state = BL_LOCKED;
            cmd_event("EV:BL", "FOLLOW_GATED");
            g_bl_last_tick_ms = now_ms;
            return;
        }
    }

    /* Accelerate toward the target follow rate (pull-in-safe ramp). */
    if (g_bl_follow_cur_sps < g_bl_follow_target_sps &&
        (int32_t)(now_ms - g_bl_follow_ramp_tick_ms) >= RAMP_TICK_MS) {
        g_bl_follow_ramp_tick_ms = now_ms;
        g_bl_follow_cur_sps += RAMP_STEP_SPS;
        if (g_bl_follow_cur_sps > g_bl_follow_target_sps)
            g_bl_follow_cur_sps = g_bl_follow_target_sps;
        motor_set_rate_sps(&lane->m, g_bl_follow_cur_sps);
        g_bl_follow_mm_per_s = (float)g_bl_follow_cur_sps * MM_PER_STEP[idx];
    }

    /* Integrate distance at the current (ramping) rate, not a fixed one. */
    g_bl_follow_traveled_mm += g_bl_follow_mm_per_s * dt_s;
    float traveled = g_bl_follow_traveled_mm;

    if (traveled >= g_bl_follow_mm) {
        motor_set_rate_sps(&lane->m, 0);
        g_bl_follow_mm = 0.0f;
        g_bl_follow_rate_mmpm = 0.0f;
        g_bl_follow_start_ms = 0;
        g_bl_follow_mm_per_s = 0.0f;
        g_bl_follow_traveled_mm = 0.0f;
        g_bl_sub_state = BL_LOCKED;
        cmd_event("BL", "FOLLOW_DONE");
    } else if (g_bl_watchdog_ms != 0 && (int32_t)(now_ms - g_bl_watchdog_ms) >= 0) {
        handle_bl_watchdog_timeout(now_ms);
    }
}

void sync_buffer_lock_tick(lane_t *lane, uint32_t now_ms) {
    if (!lane)
        return;

    if (g_bl_sub_state == BL_PRIME) {
        sync_buffer_lock_prime(lane, now_ms);
    } else if (g_bl_sub_state == BL_LOCKED) {
        sync_buffer_lock_locked(lane, now_ms);
    } else if (g_bl_sub_state == BL_FOLLOW) {
        sync_buffer_lock_follow(lane, now_ms);
    }
    g_bl_last_tick_ms = now_ms;
}

void sync_relief_pause(void) {
    sync_set_state(SYNC_RELIEF_PAUSE);
    sync_current_sps = 0;
}

static uint32_t g_sync_fault_hold_entry_ms = 0;

void sync_fault_hold(void) {
    sync_set_state(SYNC_FAULT_HOLD);
    sync_current_sps = 0;
    g_sync_fault_hold_entry_ms = g_now_ms;
}

void sync_disable(bool reset_estimator) {
    sync_set_state(SYNC_OFF);
    sync_auto_started = false;
    sync_tail_assist_active = false;
    sync_current_sps = 0;
    sync_idle_since_ms = 0;
    sync_fast_brake_until_ms = 0;
    sync_compression_recovery_active = false;
    sync_continuous_compression_since_ms = 0;
    sync_post_compression_boost_until_ms = 0;
    sync_recent_negative_until_ms = 0;
    sync_tension_pin_since_ms = 0;
    g_buf_confidence = 1.0f;
    sync_reserve_integral_mm = 0.0f;
    g_buf_pos_sigma_accum_mm = 0.0f;
    g_buf_sigma_mm = 0.0f;
    g_buf_est_low_cf_emit_ms = 0;
    g_buf_tension_dwell_warn_emit_ms = 0;
    g_buf_est_fallback_emitted = false;
    if (g_bp_drift_samples > 0)
        cmd_event("BUF", "DRIFT_RESET");
    g_bp_residual_last_mm = 0.0f;
    g_bp_drift_ewma_mm = 0.0f;
    g_bp_drift_samples = 0;
    g_bp_drift_last_ms = 0;
    g_bp_drift_correction_applied_mm = 0.0f;
    memset(g_tension_pin_ts, 0, sizeof(g_tension_pin_ts));
    g_tension_pin_ts_idx = 0;
    g_tension_risk_emit_ms = 0;
    g_tension_floor_sps = 0.0f;
    g_relay_flip_travel_since_mm = 0.0f;
    g_relay_neutral_trim_sps = 0.0f;
    g_relay_trim_last_leak_ms = 0;
    type_d_neutral_feed_reset();

    if (reset_estimator) {
        extruder_est_sps = 0.0f;
        extruder_est_prev_sps = 0.0f;
        extruder_est_last_update_ms = g_now_ms;
    }
}

int sync_bootstrap_sps(void) {
    int max_sps = sync_clamp_max_sps(SYNC_MAX_SPS);
    int startup_floor_sps = COMPRESSION_SPS + PRE_RAMP_SPS;
    int baseline_floor_sps = baseline_control_floor_sps();

    /* Adaptive floor: if we have a learned baseline, don't start way below it. */
    if (baseline_floor_sps > (startup_floor_sps * 2)) {
        int adaptive_floor = baseline_floor_sps / 2;
        if (adaptive_floor > startup_floor_sps)
            startup_floor_sps = adaptive_floor;
    }
    if (startup_floor_sps < BUF_STAB_SPS)
        startup_floor_sps = BUF_STAB_SPS;

    bool est_fresh = extruder_est_last_update_ms != 0 &&
                     (g_now_ms - extruder_est_last_update_ms) < SYNC_EST_FRESH_MS &&
                     extruder_est_sps >= (float)startup_floor_sps;

    int res;
    if (est_fresh) {
        res = clamp_i((int)extruder_est_sps, startup_floor_sps, max_sps);
    } else {
        res = clamp_i(startup_floor_sps, COMPRESSION_SPS, max_sps);
    }

    /* F2b: a post-recovery start ramps from the learned baseline, not a
     * collapsed/high estimator, so bootstrap cannot slam TENSION. The
     * startup_floor lower bound is still honored. */
    if (baseline_floor_sps >= startup_floor_sps && res > baseline_floor_sps)
        res = baseline_floor_sps;

    if (!est_fresh) {
        extruder_est_sps = (float)res;
        extruder_est_prev_sps = (float)res;
    }

    extruder_est_last_update_ms = g_now_ms;
    sync_fast_brake_until_ms = 0;
    sync_continuous_compression_since_ms = 0;
    return res;
}

int sync_effective_kp_sps(buf_state_t s) {
    int baseline_ref_sps = baseline_control_floor_sps();
    int baseline_limited_kp = (s == BUF_TENSION) ? (baseline_ref_sps * 2) : (baseline_ref_sps / 3);
    if (baseline_limited_kp < COMPRESSION_SPS)
        baseline_limited_kp = COMPRESSION_SPS;
    return (SYNC_KP_SPS < baseline_limited_kp) ? SYNC_KP_SPS : baseline_limited_kp;
}

void sync_apply_to_active(void) {
    lane_t *lane = lane_ptr(active_lane);
    if (!lane) {
        sync_current_sps = 0;
        return;
    }
    if (lane->task == TASK_MOVE)
        return;

    bool is_protected_task = (lane->task == TASK_UNLOAD || lane->task == TASK_AUTOLOAD);

    if (sync_current_sps > 0) {
        if (is_protected_task) {
            lane->current_sps = sync_current_sps;
            lane->target_sps = sync_current_sps;
            motor_set_rate_sps(&lane->m, sync_current_sps);
            motor_enable(&lane->m, true);
        } else if (lane->task != TASK_FEED && lane->fault == FAULT_NONE) {
            lane_start(lane, TASK_FEED, sync_current_sps, true, g_now_ms, 0);
        } else {
            lane->current_sps = sync_current_sps;
            lane->target_sps = sync_current_sps;
            motor_set_rate_sps(&lane->m, sync_current_sps);
            motor_enable(&lane->m, true);
            motor_set_dir(&lane->m, true);
        }
    } else if (lane->task == TASK_FEED) {
        lane_stop(lane);
    }
}

void sync_on_transition(buf_state_t prev, buf_state_t now_state, uint32_t now_ms) {
    if (prev == BUF_TENSION && now_state == BUF_COMPRESSION) {
        sync_fast_brake_until_ms = now_ms + 250u;
    }

    if (now_state == BUF_TENSION) {
        sync_tension_pin_since_ms = now_ms;
        lane_t *lane = lane_ptr(active_lane);
        if (lane && (lane->task == TASK_IDLE || lane->task == TASK_FEED)) {
            g_sync_tension_transitioned = true;
        }
        g_tension_pin_ts[g_tension_pin_ts_idx] = now_ms;
        g_tension_pin_ts_idx = (g_tension_pin_ts_idx + 1) % TENSION_PIN_WINDOW_LEN;
    } else if (prev == BUF_TENSION) {
        sync_tension_pin_since_ms = 0;
        /* Buffer physically departed tension — safe to allow auto-start again. */
        g_bl_autostart_suppressed = false;
    }

    if (!sync_tail_assist_active) {
        if (now_state == BUF_COMPRESSION) {
            g_neutral_creep_sps = 0;
            sync_compression_recovery_active = true;
            sync_continuous_compression_since_ms = 0;
            sync_post_compression_boost_until_ms = 0;
        } else if (prev == BUF_COMPRESSION && now_state == BUF_NEUTRAL) {
            if (sync_compression_recovery_active)
                sync_post_compression_boost_until_ms = now_ms + 300u;
            sync_compression_recovery_active = false;
            sync_continuous_compression_since_ms = 0;
        } else if (now_state == BUF_TENSION) {
            sync_compression_recovery_active = false;
            sync_continuous_compression_since_ms = 0;
            sync_post_compression_boost_until_ms = 0;
        }
    }

    bool fast_brake_active =
        sync_fast_brake_until_ms != 0 && (int32_t)(sync_fast_brake_until_ms - now_ms) > 0;
    if (prev != BUF_NEUTRAL && now_state == BUF_NEUTRAL && sync_enabled && !fast_brake_active &&
        !sync_compression_recovery_active && !sync_guard_active) {
        baseline_update_on_settle(g_buf.dwell_ms, now_ms);
    }
}

static bool sync_tick_gated_checks(lane_t *lane, uint32_t now_ms) {
    if (BUF_SENSOR_TYPE == 1 && sync_enabled) {
        /* Gated to active sync: type-P rests at the tension rail (+1.0) whenever
           unloaded/idle/mid-tube, so this saturation catch must NOT run when the
           sync loop isn't driving — otherwise the resting home position trips
           sync_fault_hold() at idle and breaks a manual UL (the buffer is its own
           normal home, not a starvation fault). Over-tension is only a fault while
           actively syncing a loaded buffer. */
        /* 10.1: Velocity-triggered brake on compression slam. Compression is +
           under the new convention, so a slam toward the compression rail is a
           POSITIVE velocity spike. */
        if (g_vel_norm > CONF_PSF_JUMP_NORM_PER_S) {
            sync_fast_brake_until_ms = now_ms + CONF_PSF_STOP_CONFIRM_MS;
        }

        /* 10.2: Stop/slowdown classification during brake */
        bool fast_brake_active =
            sync_fast_brake_until_ms != 0 && (int32_t)(sync_fast_brake_until_ms - now_ms) > 0;
        if (fast_brake_active) {
            if (g_vel_norm < -0.1f) {
                /* Extruder resumed or buffer recovered (moving back off compression,
                   toward tension): clear brake, resume PD */
                sync_fast_brake_until_ms = 0;
            }
        } else if (sync_fast_brake_until_ms != 0 &&
                   (int32_t)(now_ms - sync_fast_brake_until_ms) >= 0) {
            /* Brake expired: check if pinned */
            sync_fast_brake_until_ms = 0;
            if (g_buf_pos >= 0.99f) {
                sync_relief_pause();
                sync_apply_to_active();
                cmd_event("SYNC", "RELIEF_PAUSE");
                return true;
            }
        }

        /* 10.3: Saturation-sustained relief/fault triggers */
        if (g_buf_analog_saturated_since_ms != 0 &&
            (now_ms - g_buf_analog_saturated_since_ms) >= CONF_PSF_WALL_SAT_MS) {
            if (g_buf_pos >= 0.99f) {
                sync_relief_pause();
                sync_apply_to_active();
                cmd_event("SYNC", "RELIEF_PAUSE");
                return true;
            } else if (g_buf_pos <= -0.99f) {
                sync_fault_hold();
                sync_apply_to_active();
                cmd_event("SYNC", "FAULT_HOLD");
                return true;
            }
        }
    }

    if (g_sync_state == SYNC_FAULT_HOLD) {
        // VERIFY: retune from FAULT_HOLD/FAULT_HOLD_RECOVERY event logs
        if (now_ms - g_sync_fault_hold_entry_ms >= CONF_SYNC_FAULT_HOLD_RECOVERY_MS) {
            /* G1: direct resume to ACTIVE. The dead-reckoned model
             * accumulates a fictional TENSION with feed=0 during hold;
             * waiting for a TENSION event to re-arm either slams (fake
             * tension) or deadlocks neutral-print (sync off + extruder pulling
             * drains to COMPRESSION, never TENSION). Reseed the model to the
             * reserve target and re-enter ACTIVE directly, bootstrapped at
             * the baseline floor (F2b) so there is no overshoot. */
            if (BUF_SENSOR_TYPE == 0)
                g_buf_pos = buf_target_reserve_mm();
            g_buf.state = BUF_NEUTRAL;
            g_buf.entered_ms = now_ms;
            sync_current_sps = sync_bootstrap_sps();
            /* Type-P: clear the saturation timer so the recovered ACTIVE state gets
               a fresh PSF_WALL_SAT_MS window for the refill snap to relieve the
               rail. Without this, a buffer still pinned at -1.0 (extruder kept
               pulling through the hold) carries a stale, long-expired timer and the
               saturation check re-faults on the next tick -> infinite
               FAULT_HOLD <-> RECOVERY <-> AUTO_START loop. */
            g_buf_analog_saturated_since_ms = 0;
            /* Restart the tension-dwell timer so it counts from activation, not a
               stale idle value (see the auto-start note below). */
            sync_tension_pin_since_ms = (g_buf.state == BUF_TENSION) ? now_ms : 0;
            sync_set_state(SYNC_ACTIVE);
            sync_auto_started = true;
            sync_tail_assist_active = !lane_in_present(lane) && lane_out_present(lane);
            sync_idle_since_ms = 0;
            cmd_event("SYNC", "FAULT_HOLD_RECOVERY");
            cmd_event("SYNC", "AUTO_START");
        } else {
            return true;
        }
    } else if (g_sync_state == SYNC_RETRACT_ASSIST || g_sync_state == SYNC_RELIEF_PAUSE) {
        if (g_sync_state == SYNC_RETRACT_ASSIST) {
            if (g_bl_sub_state != BL_IDLE) {
                sync_buffer_lock_tick(lane, now_ms);
            }
            return true;
        } else {
            /* 11.1 Proactive recovery from RELIEF_PAUSE: if debouncing was bypassed or raced,
             * re-arm sync immediately once debounced state is NEUTRAL during active print
             * or TENSION during feed. */
            buf_state_t s = g_buf.state;
            bool relief_rearm =
                (BUF_SENSOR_TYPE == 0)
                    ? (s == BUF_TENSION || (s == BUF_NEUTRAL && lane->task == TASK_FEED))
                    : ((g_buf_pos < -0.6f) && (g_sync_tension_transitioned || g_vel_norm < -0.1f));
            if (relief_rearm) {
                if (BUF_SENSOR_TYPE == 0)
                    g_buf_pos = buf_target_reserve_mm();
                sync_current_sps = sync_bootstrap_sps();
                sync_tension_pin_since_ms = (g_buf.state == BUF_TENSION) ? now_ms : 0;
                sync_set_state(SYNC_ACTIVE);
                sync_auto_started = true;
                sync_tail_assist_active = !lane_in_present(lane) && lane_out_present(lane);
                sync_idle_since_ms = 0;
                cmd_event("SYNC", "AUTO_START");
            } else {
                return true;
            }
        }
    }
    return false;
}

static bool sync_tick_auto_start_stop(lane_t *lane, uint32_t now_ms, buf_state_t s) {
    /* Block auto-start while a manual unload state machine is running (cut path).
       TASK_UNLOAD guards the non-cut path; this covers the TASK_IDLE window inside
       MANUAL_UNLOAD_WAIT_FIRST_CLEAR / WAIT_CUT before the state machine completes. */
    bool auto_start_allowed =
        (lane->task == TASK_IDLE || lane->task == TASK_FEED) && !manual_unload_active();

    /* Do not auto-start sync when both OUT sensors are active: the hub has two
       filaments loaded simultaneously and the system is in manual recovery.
       The guard clears automatically once one lane's OUT sensor drops (i.e.
       after the operator unloads the unwanted lane).
       Also require at least one lane to have filament past its OUT sensor:
       without that, sync engaging at TENSION just spins an empty lane
       (post-unload state with buffer drained to tension and no filament
       past gate to feed). */
    bool l1_out = lane_out_present(&g_lane_l1);
    bool l2_out = lane_out_present(&g_lane_l2);
    bool any_lane_loaded = l1_out || l2_out;
    bool both_loaded = l1_out && l2_out;
    /* Type-P auto-start = buffer high AND under real extruder demand. Demand is
       either a fresh transition into the tension zone OR the buffer actively
       falling toward tension (g_vel_norm < 0, since tension is now the - rail).
       The velocity term is the robust signal: if the buffer rests already inside
       the goal-relative tension zone (goal is compression-side, so even a
       near-neutral rest reads TENSION), no fresh transition fires — but the
       extruder pulling still makes it fall. A static rest at home (-1.0) has
       vel~0, so this stays gated there (D18). */
    bool is_tension_active =
        (BUF_SENSOR_TYPE == 1)
            ? ((g_buf_pos < -0.6f) && (g_sync_tension_transitioned || g_vel_norm < -0.1f))
            : (s == BUF_TENSION);
    if (AUTO_MODE && !sync_enabled && auto_start_allowed && is_tension_active &&
        !g_bl_autostart_suppressed && any_lane_loaded && !both_loaded) {
        /* Auto-correct the active lane to the physically loaded one. The operator
           may have switched the UI selection to an unloaded lane (to inspect or
           eject) and left it there. Tension with filament at the hub (YS) means
           the loaded lane is feeding, so adopt whichever lane has its OUT sensor
           engaged before enabling sync — otherwise we would drive the wrong
           (empty) lane. Double-load is already excluded by the guard above. */
        if (on_al(&g_y_split)) {
            if (lane_out_present(&g_lane_l1) && active_lane != 1) {
                set_active_lane(1);
                lane = lane_ptr(active_lane);
            } else if (lane_out_present(&g_lane_l2) && active_lane != 2) {
                set_active_lane(2);
                lane = lane_ptr(active_lane);
            }
        }
        bool tail_assist = !lane_in_present(lane) && lane_out_present(lane);
        int startup_sps = sync_bootstrap_sps();
        sync_current_sps = startup_sps;
        /* Count tension-dwell from activation, not a stale idle value. Because
           BUF_GOAL is compression-side, the buffer rests in the control TENSION
           zone while idle, so sync_tension_pin_since_ms (set on the NEUTRAL->TENSION
           transition, even while sync is OFF) accumulates a large stale dwell. On
           auto-start the tension-dwell fault would then fire instantly. Restart it. */
        sync_tension_pin_since_ms = (g_buf.state == BUF_TENSION) ? now_ms : 0;
        sync_set_state(SYNC_ACTIVE);
        sync_auto_started = true;
        sync_tail_assist_active = tail_assist;
        sync_idle_since_ms = 0;
        cmd_event("SYNC", "AUTO_START");
    }

    if (!sync_enabled)
        return true;

    if (sync_auto_started) {
        if (sync_tail_assist_active) {
            if (s != BUF_COMPRESSION) {
                sync_idle_since_ms = 0;
            } else {
                if (sync_idle_since_ms == 0)
                    sync_idle_since_ms = now_ms;
                if (SYNC_AUTO_STOP_MS > 0 &&
                    (now_ms - sync_idle_since_ms) > (uint32_t)SYNC_AUTO_STOP_MS) {
                    sync_disable(true);
                    extruder_est_last_update_ms = now_ms;
                    sync_apply_to_active();
                    cmd_event("SYNC", "AUTO_STOP");
                    return true;
                }
            }
        } else {
            sync_idle_since_ms = 0;
        }
    }
    return false;
}

static float sync_apply_drift_correction(buf_state_t s, float thr, float reserve_deadband_mm) {
    float bp_eff = g_buf_pos;
    float drift_correction_mm = 0.0f;
    int drift_min_samples = BUF_DRIFT_MIN_SAMPLES;
    if (drift_min_samples < 1)
        drift_min_samples = 1;
    bool drift_apply_gate = (BUF_DRIFT_APPLY_THR_MM > 0.0f) && (g_bp_drift_samples > 0) &&
                            (fabsf(g_bp_drift_ewma_mm) >= BUF_DRIFT_APPLY_THR_MM) &&
                            (g_buf_signal.confidence >= BUF_DRIFT_APPLY_MIN_CF);
    if (drift_apply_gate) {
        float sample_frac =
            clamp_f((float)g_bp_drift_samples / (float)drift_min_samples, 0.0f, 1.0f);
        drift_correction_mm =
            clamp_f(g_bp_drift_ewma_mm, -BUF_DRIFT_CLAMP_MM, BUF_DRIFT_CLAMP_MM) * sample_frac;
        float wall_taper_mm = reserve_deadband_mm * 2.0f;
        if (wall_taper_mm < 0.5f)
            wall_taper_mm = 0.5f;

        if (drift_correction_mm < 0.0f) {
            float dist_from_tension_mm = g_buf_pos + thr;
            float wall_frac = clamp_f(dist_from_tension_mm / wall_taper_mm, 0.0f, 1.0f);
            drift_correction_mm *= wall_frac;
        } else if (drift_correction_mm > 0.0f) {
            float dist_from_compression_mm = thr - g_buf_pos;
            float wall_frac = clamp_f(dist_from_compression_mm / wall_taper_mm, 0.0f, 1.0f);
            drift_correction_mm *= wall_frac;
        }

        bp_eff = g_buf_pos - drift_correction_mm;

        /* SAFETY: Don't let correction push bp_eff to the opposite side of physical state.
         * If we are physically at a wall, the controller must see it as at or beyond that wall. */
        if (s == BUF_COMPRESSION && bp_eff < thr)
            bp_eff = thr;
        else if (s == BUF_TENSION && bp_eff > -thr)
            bp_eff = -thr;

        /* CONFIDENCE BIAS: If we are uncertain, shift bp_eff toward the TENSION side.
         * This creates a gentle "feed pressure" that ensures we don't under-feed
         * while the model is drifting open-loop. This shift disappears as soon as
         * we hit a switch and restore confidence.
         * When blend is active (BLEND_FRAC > 0), confidence-bias is
         * redundant. Gate it off in that case. */
        if (BUF_VARIANCE_BLEND_FRAC <= 0.0f) {
            float uncertainty_shift_mm = (1.0f - g_buf_signal.confidence) * (thr * 0.8f);
            bp_eff -= uncertainty_shift_mm;
        }

        /* Clamp so correction cannot push bp_eff past the endstop zone boundary */
        bp_eff = clamp_f(bp_eff, -thr, thr);
    }
    g_bp_drift_correction_applied_mm = drift_correction_mm;
    return bp_eff;
}

static int sync_apply_type_d_probe_floor(buf_state_t s, int target_sps) {
    float tick_dt_s = (float)SYNC_TICK_MS / 1000.0f;
    if (s == BUF_TENSION) {
        g_tension_floor_sps += (float)SYNC_TENSION_PROBE_UP_SPS_PER_S * tick_dt_s;
    } else if (s == BUF_COMPRESSION) {
        g_tension_floor_sps -= (float)SYNC_TENSION_PROBE_DOWN_SPS_PER_S * tick_dt_s;
    } else {
        /* NEUTRAL is the uncertain state: the buffer may be drifting to
         * either rail and only a click resolves it. Creep the floor up
         * (gently) so uncertainty always resolves into a COMPRESSION click
         * (safe, drains) rather than a metastable drift into TENSION (a
         * starve). The longer the dwell, the more we lean — uncertainty
         * grows with time since the last crossing. COMPRESSION then backs
         * it off, so this is a bounded, compression-biased sawtooth. */
        g_tension_floor_sps += (float)SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S * tick_dt_s;
    }
    if (g_tension_floor_sps > (float)SYNC_TENSION_PROBE_MAX_SPS)
        g_tension_floor_sps = (float)SYNC_TENSION_PROBE_MAX_SPS;
    if (g_tension_floor_sps < 0.0f)
        g_tension_floor_sps = 0.0f;

    if (s == BUF_NEUTRAL) {
        int floor_sps = (int)g_tension_floor_sps;
        if (floor_sps > SYNC_MIN_SPS && target_sps < floor_sps)
            target_sps = floor_sps;
    }
    return target_sps;
}

static int sync_check_tension_dwell_and_ramp(buf_state_t s, int target_sps, uint32_t now_ms) {
    if (s == BUF_TENSION && sync_tension_pin_since_ms != 0) {
        uint32_t tension_dwell_ms = now_ms - sync_tension_pin_since_ms;
        /* RELAY: TENSION switch contact is the normal "buffer empty, refill"
         * signal, not a fault. Only fault-hold on tension dwell in analog
         * mode; the type-D relay catch-up path refills it. */
        if (BUF_SENSOR_TYPE != 0 && SYNC_TENSION_DWELL_STOP_MS > 0 &&
            tension_dwell_ms >= (uint32_t)SYNC_TENSION_DWELL_STOP_MS) {
            sync_fault_hold();
            extruder_est_last_update_ms = now_ms;
            sync_apply_to_active();
            cmd_event("SYNC", "FAULT_HOLD");
            return -1;
        }
        if (SYNC_TENSION_RAMP_DELAY_MS > 0 &&
            tension_dwell_ms >= (uint32_t)SYNC_TENSION_RAMP_DELAY_MS) {
            int max_sps = sync_clamp_max_sps(SYNC_MAX_SPS);
            if (target_sps < max_sps)
                target_sps = max_sps;
        }
    }
    return target_sps;
}

static int sync_apply_compression_recovery_cap(buf_state_t s, int target_sps, uint32_t now_ms) {
    if (BUF_SENSOR_TYPE == 0 && sync_compression_recovery_active) {
        uint32_t compression_recovery_ms = now_ms - g_buf.entered_ms;
        int compression_floor_sps = sync_compression_floor_sps();
        int kp_window = sync_effective_kp_sps(s);
        int recovery_cap = (int)extruder_est_sps - kp_window;
        if (compression_recovery_ms > SYNC_COMPRESSION_COLLAPSE_DELAY_MS) {
            uint32_t collapse_ms = compression_recovery_ms - SYNC_COMPRESSION_COLLAPSE_DELAY_MS;
            if (collapse_ms > SYNC_COMPRESSION_COLLAPSE_CAP_MS)
                collapse_ms = SYNC_COMPRESSION_COLLAPSE_CAP_MS;
            int extra_trim = (int)(((uint64_t)collapse_ms * (uint64_t)(kp_window + PRE_RAMP_SPS)) /
                                   (uint64_t)SYNC_COMPRESSION_COLLAPSE_CAP_MS);
            recovery_cap -= extra_trim;
        }
        if (recovery_cap < compression_floor_sps)
            recovery_cap = compression_floor_sps;
        /* F1b: collapse braking is for an over-tensioned buffer draining
         * through COMPRESSION; while still in NEUTRAL it must not starve the
         * refill below the learned baseline. */
        if (s == BUF_NEUTRAL) {
            int neutral_floor = baseline_control_floor_sps();
            if (recovery_cap < neutral_floor)
                recovery_cap = neutral_floor;
        }
        if (target_sps > recovery_cap)
            target_sps = recovery_cap;
    }
    return target_sps;
}

static int sync_tick_calculate_target(buf_state_t s, uint32_t now_ms, lane_t *lane) {
    if (lane && (lane->task == TASK_FEED || lane->task == TASK_IDLE) && lane->fault == FAULT_NONE &&
        g_sync_state == SYNC_ACTIVE) {
        if (g_buf.mmu_sps_dwell_samples >= 10000) {
            g_buf.mmu_sps_dwell_sum /= 2;
            g_buf.mmu_sps_dwell_samples /= 2;
        }
        g_buf.mmu_sps_dwell_sum += (uint32_t)lane_motion_sps(lane);
        g_buf.mmu_sps_dwell_samples++;
    }
    float raw_target = buf_target_reserve_mm();
    float reserve_deadband_mm = buf_virtual_deadband_mm();

    g_buf_pos_raw_status = g_buf_pos;
    /* Variance-aware position blend (default OFF) */
    if (BUF_VARIANCE_BLEND_FRAC > 0.0f && g_buf_sigma_mm > 0.0f) {
        float sigma_ref = (BUF_VARIANCE_BLEND_REF_MM > 0.05f) ? BUF_VARIANCE_BLEND_REF_MM : 1.0f;
        float distrust = clamp_f(g_buf_sigma_mm / sigma_ref, 0.0f, 1.0f);
        float blend = distrust * BUF_VARIANCE_BLEND_FRAC;
        g_buf_pos = (1.0f - blend) * g_buf_pos + blend * raw_target;
    }

    float thr = buf_threshold_mm();
    if (thr < 0.001f)
        thr = 0.001f;

    /* Effective buffer position with drift correction (default OFF) */
    float bp_eff = sync_apply_drift_correction(s, thr, reserve_deadband_mm);

    /* Integral reserve centering — active only in BUF_NEUTRAL with adequate confidence */
    bool integral_active = (s == BUF_NEUTRAL) && (SYNC_RESERVE_INTEGRAL_GAIN > 0.0f) &&
                           (g_buf_signal.confidence >= 0.7f);
    if (integral_active) {
        float raw_error = bp_eff - raw_target;
        float dt_s = (float)SYNC_TICK_MS / 1000.0f;
        sync_reserve_integral_mm -= SYNC_RESERVE_INTEGRAL_GAIN * raw_error * dt_s;
        sync_reserve_integral_mm =
            clamp_f(sync_reserve_integral_mm, -SYNC_RESERVE_INTEGRAL_CLAMP_MM,
                    +SYNC_RESERVE_INTEGRAL_CLAMP_MM);
    }
    /* TENSION_DWELL_WARN: integral saturated toward tension side — rate-limited 10 s */
    if (sync_enabled && SYNC_RESERVE_INTEGRAL_GAIN > 0.0f &&
        sync_reserve_integral_mm < -(SYNC_RESERVE_INTEGRAL_CLAMP_MM * 0.5f)) {
        if (g_buf_tension_dwell_warn_emit_ms == 0 ||
            (now_ms - g_buf_tension_dwell_warn_emit_ms) >= 10000u) {
            g_buf_tension_dwell_warn_emit_ms = now_ms;
            cmd_event("SYNC", "TENSION_DWELL_WARN");
        }
    }
    float effective_target = raw_target + sync_reserve_integral_mm;

    float pos_norm = (BUF_SENSOR_TYPE == 1) ? g_buf_pos : (bp_eff / thr);
    float target_norm = (BUF_SENSOR_TYPE == 1) ? psf_goal_norm() : (effective_target / thr);
    float error_norm = pos_norm - target_norm;
    float deadband_norm = (BUF_SENSOR_TYPE == 1) ? 0.1f : (reserve_deadband_mm / thr);

    if (BUF_SENSOR_TYPE == 0) {
        type_d_neutral_feed_sample(s, pos_norm, target_norm, deadband_norm);
        relay_neutral_trim_leak(s, now_ms);
    }

    int target_sps;
    if (BUF_SENSOR_TYPE == 0) {
        target_sps = relay_control_law(s);
    } else {
        target_sps = psf_control_law(error_norm);
    }
    int type_d_neutral_relay_floor_sps =
        (BUF_SENSOR_TYPE == 0 && s == BUF_NEUTRAL && error_norm < -deadband_norm) ? target_sps : 0;

    /* RAMPING BIAS: If we don't know where we are, raise speed a little bit
     * until we touch compression. This probe speed (up to ~150mm/min) ensures
     * we gravitate toward the safe compression wall rather than drifting toward tension. */
    if (g_buf_signal.confidence < 1.0f && s == BUF_NEUTRAL) {
        float uncertainty = 1.0f - g_buf_signal.confidence;
        target_sps += (int)(uncertainty * 6.0f);
    }

    target_sps = sync_check_tension_dwell_and_ramp(s, target_sps, now_ms);
    if (target_sps < 0) {
        return -1;
    }

    /* TENSION-risk density warning (warn-only, default threshold=4) */
    if (TENSION_RISK_THRESHOLD > 0) {
        int tpx = sync_tension_pin_window_count(now_ms);
        if (tpx >= TENSION_RISK_THRESHOLD) {
            if (g_tension_risk_emit_ms == 0 || (now_ms - g_tension_risk_emit_ms) >= 30000u) {
                g_tension_risk_emit_ms = now_ms;
                cmd_event("SYNC", "TENSION_RISK_HIGH");
            }
        }
    }

    target_sps = sync_apply_scaling(target_sps, target_norm, pos_norm);

    if (sync_post_compression_boost_until_ms != 0) {
        if ((int32_t)(sync_post_compression_boost_until_ms - now_ms) > 0)
            target_sps += PRE_RAMP_SPS;
        else
            sync_post_compression_boost_until_ms = 0;
    }

    int neutral_anti_tension_floor_sps = sync_neutral_anti_tension_floor_sps(
        s, lane, error_norm, deadband_norm, type_d_neutral_relay_floor_sps, now_ms);
    if (neutral_anti_tension_floor_sps > 0 && target_sps < neutral_anti_tension_floor_sps) {
        target_sps = neutral_anti_tension_floor_sps;
    }

    target_sps = sync_apply_compression_recovery_cap(s, target_sps, now_ms);

    /* Type-D NEUTRAL relay output is the demand estimate plus the configured
     * lean. Preserve it only while reserve is tension-side; once at/above target,
     * shared shapers must be able to brake before the COMPRESSION switch. */
    if (type_d_neutral_relay_floor_sps > 0 && target_sps < type_d_neutral_relay_floor_sps) {
        target_sps = type_d_neutral_relay_floor_sps;
    }

    /* Type-D tension recovery: symmetric AIMD hunt on a held feed floor.
     * TENSION = still starved -> probe up; COMPRESSION = overfed -> ease down;
     * NEUTRAL = hold the found level. COMPRESSION (not a timeout) is the
     * recovery-complete signal, so no value of any interval is guessed. */
    if (BUF_SENSOR_TYPE == 0) {
        target_sps = sync_apply_type_d_probe_floor(s, target_sps);
    }
    return target_sps;
}

static int sync_apply_type_p_smoothing(int target_sps, float dt_s) {
    /* Type-P distance-based smoothing (Happy-Hare-style). Both the target
       EMA and the slew limit are keyed to filament distance moved this tick,
       not wall-clock — so feed changes scale with flow and go to zero when
       the printer is idle, instead of the old time-ramp (flow-blind, 2-tick
       bang-bang) or the direct apply (snaps to the noisy PD target instantly).
       "move" = extruder flow demand this tick (the system's distance clock). */
    uint8_t idx = (active_lane == 2) ? 1 : 0;
    float move = fabsf(extruder_est_sps) * MM_PER_STEP[idx] * dt_s;

    /* 1. EMA the target over distance: alpha = 1 - exp(-move/L). */
    float L = (SYNC_PSF_FILTER_MM > 0.01f) ? SYNC_PSF_FILTER_MM : 0.01f;
    float alpha = 1.0f - expf(-move / L);
    g_psf_target_filt += alpha * ((float)target_sps - g_psf_target_filt);

    /* 2. Slew-limit the applied rate, clamped so it never overshoots the
          filtered target (overshoot is what made the old ramp oscillate).
          Floor the step so a tiny creep is always allowed off idle. */
    float max_step = SYNC_PSF_SLEW_PER_MM * move;
    if (max_step < 1.0f)
        max_step = 1.0f;
    float cur = (float)sync_current_sps;
    if (g_psf_target_filt > cur + max_step)
        cur += max_step;
    else if (g_psf_target_filt < cur - max_step)
        cur -= max_step;
    else
        cur = g_psf_target_filt;

    /* Overfeed guard: the distance clock (move) freezes the filter when the
       extruder stops (move -> 0 => alpha, max_step -> 0), holding the feed at
       the pre-stop rate so the MMU keeps pushing into a stopped extruder and
       slams COMPRESSION. Feed *drops* are always safe (worst case is a brief
       tension excursion, which has room), so additionally let the feed fall
       toward the raw target on wall-clock, independent of flow. Take whichever
       path is lower and pin the filtered target so it can't re-drive the feed
       back up while demand stays low. UP is untouched (purely flow-keyed). */
    if (SYNC_PSF_DECAY_SPS_PER_S > 0.0f) {
        float wall_cur = cur - SYNC_PSF_DECAY_SPS_PER_S * dt_s;
        if (wall_cur < (float)target_sps)
            wall_cur = (float)target_sps;
        if (wall_cur < cur) {
            cur = wall_cur;
            if (g_psf_target_filt > cur)
                g_psf_target_filt = cur;
        }
    }
    return (int)(cur + 0.5f);
}

static bool sync_check_continuous_compression(buf_state_t s, uint32_t now_ms) {
    if (sync_auto_started && !sync_tail_assist_active) {
        if (s == BUF_COMPRESSION) {
            // Start or maintain the continuous physical dwell timer
            if (sync_continuous_compression_since_ms == 0) {
                sync_continuous_compression_since_ms = now_ms;
            }

            uint32_t compression_dwell_ms = now_ms - sync_continuous_compression_since_ms;

            // Define a widened floor threshold to ignore PID hunting/noise
            int effective_floor_sps = sync_compression_floor_sps() + PRE_RAMP_SPS;

            uint32_t floor_timeout_ms = (uint32_t)SYNC_AUTO_STOP_MS;

            if (floor_timeout_ms > 0 && compression_dwell_ms > floor_timeout_ms) {
                if (sync_current_sps <= effective_floor_sps) {
                    sync_relief_pause();
                    extruder_est_last_update_ms = now_ms;
                    sync_apply_to_active();
                    cmd_event("SYNC", "RELIEF_PAUSE");
                    return true;
                }
            }
        } else {
            // ONLY reset the timer when the arm physically leaves the compression switch
            sync_continuous_compression_since_ms = 0;
        }
    }
    return false;
}

static void sync_tick_apply_rate(int target_sps, buf_state_t s, uint32_t now_ms, lane_t *lane) {
    bool compression_wall_critical = false;
    if (BUF_SENSOR_TYPE == 0 && s == BUF_COMPRESSION) {
        float compression_push_mm_s = sync_compression_wall_velocity_mm_s(lane);
        float compression_wall_ms = sync_compression_wall_time_ms(lane);
        compression_wall_critical = compression_push_mm_s > SYNC_COMPRESSION_HARD_PUSH_MM_S &&
                                    compression_wall_ms < SYNC_COMPRESSION_HARD_WALL_MS;
    }
    /* RELAY: hitting the COMPRESSION switch is the normal "buffer full,
     * back off" control signal, not a jam. This critical-fault only ever
     * fired in type-D mode and was manufacturing the FAULT_HOLD ->
     * recovery-slam cycle. Suppress it in relay mode; the relay stop state
     * drains the full buffer off the compression wall. */
    if (compression_wall_critical && BUF_SENSOR_TYPE != 0) {
        sync_fault_hold();
        extruder_est_last_update_ms = now_ms;
        sync_apply_to_active();
        cmd_event("SYNC", "FAULT_HOLD");
        return;
    }

    bool fast_brake_active =
        sync_fast_brake_until_ms != 0 && (int32_t)(sync_fast_brake_until_ms - now_ms) > 0;
    if (!fast_brake_active && sync_fast_brake_until_ms != 0 &&
        (int32_t)(now_ms - sync_fast_brake_until_ms) >= 0)
        sync_fast_brake_until_ms = 0;

    int max_sps = sync_clamp_max_sps(SYNC_MAX_SPS);
    if (fast_brake_active)
        target_sps = 0;
    /* Type-D COMPRESSION drain: keep this branch above the SYNC_MIN clamp.
     * A zero drain fraction is the legacy hard-stop A/B guard; active draw may
     * feed a bounded fraction of demand, strictly below demand, so the buffer
     * still drains off the compression rail. Idle/end-of-feed remains a true
     * zero to preserve purge no-grind behavior. */
    else if (BUF_SENSOR_TYPE == 0 && s == BUF_COMPRESSION) {
        int demand_sps = (int)extruder_est_sps;
        int idle_threshold_sps = SYNC_MIN_SPS;
        if (idle_threshold_sps < 1)
            idle_threshold_sps = 1;
        if (lane && lane->task == TASK_FEED && demand_sps > idle_threshold_sps &&
            SYNC_COMPRESSION_DRAIN_FRAC > 0.0f &&
            g_sync_relieve_effort_mm < SYNC_COMPRESSION_DRAIN_BUDGET_MM) {
            int drain_sps = (int)((float)demand_sps * SYNC_COMPRESSION_DRAIN_FRAC);
            if (drain_sps < 0)
                drain_sps = 0;
            if (drain_sps > max_sps)
                drain_sps = max_sps;
            if (drain_sps >= demand_sps)
                drain_sps = demand_sps - 1;
            target_sps = drain_sps;
        } else {
            target_sps = 0;
        }
    } else
        target_sps = clamp_i(target_sps, SYNC_MIN_SPS, max_sps);

    int ramp_dn_sps = SYNC_RAMP_DN_SPS;
    if (BUF_SENSOR_TYPE == 0 && !fast_brake_active && sync_compression_recovery_active &&
        s == BUF_COMPRESSION) {
        uint32_t compression_recovery_ms = now_ms - g_buf.entered_ms;
        if (compression_recovery_ms > SYNC_COMPRESSION_COLLAPSE_DELAY_MS) {
            ramp_dn_sps *= SYNC_COMPRESSION_COLLAPSE_RAMP_MULT;
        }
    }

    if (fast_brake_active) {
        sync_current_sps = 0;
        g_psf_target_filt = 0.0f;
    } else if (BUF_SENSOR_TYPE == 1 && buf_pos_norm() < -CONF_PSF_SOFT_WALL_START &&
               target_sps > sync_current_sps) {
        /* Urgent refill: the buffer is starved into the TENSION soft-wall zone and
           the distance-EMA below is far too slow to ramp feed before it slams the
           rail (cannot_refill). Feed-up into tension is the safe+urgent direction
           (worst case is a brief overfeed once recovered, which COMPRESSION-side
           smoothing handles), so snap straight to the soft-wall target. Once the
           buffer climbs back out of the wall, the smoothing path resumes. */
        sync_current_sps = target_sps;
        /* Seed the smoothing target at DEMAND, not the wall's max_sps: once the
           buffer climbs out of the wall the smoothing resumes from here, so feed
           eases to the extruder rate instead of staying pinned at max and
           overshooting into COMPRESSION. */
        g_psf_target_filt = extruder_est_sps;
    } else if (BUF_SENSOR_TYPE == 1) {
        float dt_s = (float)SYNC_TICK_MS / 1000.0f;
        sync_current_sps = sync_apply_type_p_smoothing(target_sps, dt_s);
    } else if (sync_current_sps > target_sps) {
        sync_current_sps -= ramp_dn_sps;
        if (sync_current_sps < target_sps)
            sync_current_sps = target_sps;
    } else if (sync_current_sps < target_sps) {
        sync_current_sps += SYNC_RAMP_UP_SPS;
        if (sync_current_sps > target_sps)
            sync_current_sps = target_sps;
    }

    sync_current_sps = clamp_i(sync_current_sps, 0, max_sps);

    if (sync_check_continuous_compression(s, now_ms)) {
        return;
    }

    sync_apply_to_active();

    if ((now_ms - sync_last_evt_ms) >= 500u) {
        sync_last_evt_ms = now_ms;
        char ev[48];
        snprintf(ev, sizeof(ev), "%s,%.1f,%.2f", buf_state_name(s),
                 (double)sps_to_mm_per_min(sync_current_sps), (double)g_buf_pos);
        cmd_event("BS", ev);
    }
}

void sync_tick(uint32_t now_ms) {
    lane_t *lane = lane_ptr(active_lane);
    if (!lane || tc_state() != TC_IDLE || g_boot_stabilizing)
        return;

    if (sync_tick_gated_checks(lane, now_ms))
        return;

    buf_state_t s = g_buf.state;

    if (sync_tick_auto_start_stop(lane, now_ms, s))
        return;

    if ((now_ms - sync_last_tick_ms) < (uint32_t)SYNC_TICK_MS)
        return;

    sync_last_tick_ms = now_ms;

    if (s == BUF_FAULT) {
        sync_current_sps = 0;
        sync_apply_to_active();
        cmd_event("BS", "FAULT,0");
        return;
    }

    int target_sps = sync_tick_calculate_target(s, now_ms, lane);
    if (target_sps < 0)
        return;

    sync_tick_apply_rate(target_sps, s, now_ms, lane);
}

bool sync_is_positive_relaunch_damped(void) {
    if (sync_tail_assist_active)
        return false;
    // Never damp while the buffer is empty — refill must be unrestricted.
    if (g_buf.state == BUF_TENSION)
        return false;
    if (sync_recent_negative_until_ms == 0)
        return false;

    uint32_t now_ms = g_now_ms;
    // Window has expired naturally.
    if ((int32_t)(now_ms - sync_recent_negative_until_ms) >= 0)
        return false;

    // Release damping 300 ms before the window expires to avoid a sharp step.
    uint32_t window_start_ms = sync_recent_negative_until_ms - SYNC_RECENT_NEGATIVE_HOLD_MS;
    uint32_t elapsed_in_window = now_ms - window_start_ms;
    if (elapsed_in_window >= (uint32_t)(SYNC_RECENT_NEGATIVE_HOLD_MS - 300u))
        return false;

    // If reserve error is clearly positive the buffer is already refilling —
    // stop damping so the correction can run at full strength.
    if (sync_reserve_error_mm() < -buf_virtual_deadband_mm() * 0.5f)
        return false;

    return true;
}

bool sync_is_tension_predicted(void) {
    return !sync_is_positive_relaunch_damped() && predict_tension_coming();
}

uint32_t sync_tension_dwell_ms(uint32_t now_ms) {
    if (sync_tension_pin_since_ms == 0 || g_buf.state != BUF_TENSION)
        return 0;
    return now_ms - sync_tension_pin_since_ms;
}

uint32_t sync_est_age_ms(uint32_t now_ms) {
    if (extruder_est_last_update_ms == 0)
        return 0;
    return now_ms - extruder_est_last_update_ms;
}
