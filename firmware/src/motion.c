#include "motion.h"

#include "sync.h"
#include "toolchange.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "protocol.h"

#define TAIL_TRANSIT_MARGIN_FRAC 1.2f
#define DEBOUNCE_STABLE_US 10000
#define PWM_MAX_WRAP_F 65535.0f
#define PWM_MAX_CLKDIV 255.0f
#define PWM_MIN_WRAP 10u
#define PWM_MAX_WRAP 65535u
#define AUTOLOAD_MIN_RETRACT_S 0.05f
#define MS_PER_SECOND_F 1000.0f
#define PRELOAD_JAM_MARGIN_FRAC 1.5f
#define UNLOAD_TO_IN_JAM_MARGIN_FRAC 2.0f
#define UNLOAD_RETRACT_TIMEOUT_MS 30000u
#define LOAD_BYPASS_HALF_TRAVEL_DIVISOR 2.0f
#define LOAD_BYPASS_DISTANCE_FRAC 0.8f
#define LOAD_RUNOUT_GRACE_MS 1000u
#define LOAD_FULL_SENSOR_TIMEOUT_MS 10000u
#define RUNOUT_DEBOUNCE_MS 1000u
#define RUNOUT_REARM_BLOCK_MS 30000u
#define DRY_SPIN_TIMEOUT_MS 8000u

static bool lane_tail_in_transit(lane_t *lane) {
    return !lane_in_present(lane) && !lane_out_present(lane) &&
           ((lane->task_dist_mm - lane->dist_at_in_clear_mm) <
            ((float)DIST_IN_OUT * TAIL_TRANSIT_MARGIN_FRAC));
}

static bool lane_tail_runout_ready(lane_t *lane) {
    if (!lane_out_present(lane))
        return true;
    return (lane->task_dist_mm - lane->dist_at_in_clear_mm) >=
           ((float)DIST_IN_OUT * TAIL_TRANSIT_MARGIN_FRAC);
}

int motion_clamp_rate_sps(int sps) {
    if (sps <= 0)
        return sps;
    return (sps < GLOBAL_MAX_SPS) ? sps : GLOBAL_MAX_SPS;
}

void motion_limit_runtime_rates(bool refresh_active_motors) {
    FEED_SPS = motion_clamp_rate_sps(FEED_SPS);
    REV_SPS = motion_clamp_rate_sps(REV_SPS);
    AUTO_SPS = motion_clamp_rate_sps(AUTO_SPS);
    BUF_STAB_SPS = motion_clamp_rate_sps(BUF_STAB_SPS);
    JOIN_SPS = motion_clamp_rate_sps(JOIN_SPS);
    PRESS_SPS = motion_clamp_rate_sps(PRESS_SPS);
    COMPRESSION_SPS = motion_clamp_rate_sps(COMPRESSION_SPS);
    RAMP_STEP_SPS = motion_clamp_rate_sps(RAMP_STEP_SPS);
    PRE_RAMP_SPS = motion_clamp_rate_sps(PRE_RAMP_SPS);
    SYNC_MAX_SPS = motion_clamp_rate_sps(SYNC_MAX_SPS);
    SYNC_MIN_SPS = motion_clamp_rate_sps(SYNC_MIN_SPS);
    SYNC_RAMP_UP_SPS = motion_clamp_rate_sps(SYNC_RAMP_UP_SPS);
    SYNC_RAMP_DN_SPS = motion_clamp_rate_sps(SYNC_RAMP_DN_SPS);
    g_baseline_target_sps = motion_clamp_rate_sps(g_baseline_target_sps);
    g_baseline_sps = motion_clamp_rate_sps(g_baseline_sps);
    sync_current_sps = motion_clamp_rate_sps(sync_current_sps);
    g_tc_ctx.reload_current_sps = motion_clamp_rate_sps(g_tc_ctx.reload_current_sps);

    if (SYNC_MIN_SPS > SYNC_MAX_SPS)
        SYNC_MIN_SPS = SYNC_MAX_SPS;

    lane_t *lanes[] = {&g_lane_l1, &g_lane_l2};
    for (size_t i = 0; i < NUM_LANES; i++) {
        lane_t *lane = lanes[i];
        lane->target_sps = motion_clamp_rate_sps(lane->target_sps);
        lane->current_sps = motion_clamp_rate_sps(lane->current_sps);
        if (lane->target_sps > 0 && lane->current_sps > lane->target_sps)
            lane->current_sps = lane->target_sps;
        if (refresh_active_motors && lane->task != TASK_IDLE && lane->current_sps > 0) {
            motor_set_rate_sps(&lane->m, lane->current_sps);
        }
    }
}

void debounced_input_init(debounced_input_t *d, uint pin) {
    d->pin = pin;
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);

    bool raw = gpio_get(pin);
    d->stable = raw;
    d->last_raw = raw;
    d->last_edge = get_absolute_time();
}

void debounced_input_update(debounced_input_t *d) {
    absolute_time_t now = get_absolute_time();
    bool raw = gpio_get(d->pin);

    if (raw != d->last_raw) {
        d->last_raw = raw;
        d->last_edge = now;
    }

    if (raw != d->stable) {
        if (absolute_time_diff_us(d->last_edge, now) >= DEBOUNCE_STABLE_US) {
            d->stable = raw;
        }
    }
}

void motor_init(motor_t *motor, uint en_pin, uint dir_pin, uint step_pin, bool dir_invert) {
    motor->en_pin = en_pin;
    motor->dir_pin = dir_pin;
    motor->step_pin = step_pin;
    motor->dir_invert = dir_invert;

    gpio_init(motor->en_pin);
    gpio_set_dir(motor->en_pin, GPIO_OUT);

    gpio_init(motor->dir_pin);
    gpio_set_dir(motor->dir_pin, GPIO_OUT);

    if (EN_ACTIVE_LOW)
        gpio_put(motor->en_pin, 1);
    else
        gpio_put(motor->en_pin, 0);

    gpio_put(motor->dir_pin, 0);

    gpio_set_function(motor->step_pin, GPIO_FUNC_PWM);
    motor->slice = pwm_gpio_to_slice_num(motor->step_pin);
    motor->chan = pwm_gpio_to_channel(motor->step_pin);

    pwm_config cfg = pwm_get_default_config();
    pwm_init(motor->slice, &cfg, false);
    pwm_set_enabled(motor->slice, false);
}

void motor_enable(motor_t *motor, bool on) {
    if (EN_ACTIVE_LOW)
        gpio_put(motor->en_pin, on ? 0 : 1);
    else
        gpio_put(motor->en_pin, on ? 1 : 0);
}

void motor_set_dir(motor_t *motor, bool forward) {
    bool d = forward ^ motor->dir_invert;
    gpio_put(motor->dir_pin, d ? 1 : 0);
}

void motor_set_rate_sps(motor_t *motor, int sps) {
    sps = motion_clamp_rate_sps(sps);
    if (sps <= 0) {
        pwm_set_enabled(motor->slice, false);
        return;
    }

    uint32_t sys = clock_get_hz(clk_sys);
    float target = (float)sps;

    float div = (float)sys / (target * PWM_MAX_WRAP_F);
    if (div < 1.0f)
        div = 1.0f;
    if (div > PWM_MAX_CLKDIV)
        div = PWM_MAX_CLKDIV;

    uint32_t wrap = (uint32_t)((float)sys / (div * target) - 1.0f);
    if (wrap < PWM_MIN_WRAP)
        wrap = PWM_MIN_WRAP;
    if (wrap > PWM_MAX_WRAP)
        wrap = PWM_MAX_WRAP;

    pwm_set_clkdiv(motor->slice, div);
    pwm_set_wrap(motor->slice, wrap);
    pwm_set_chan_level(motor->slice, motor->chan, (uint16_t)(wrap / 2));
    pwm_set_enabled(motor->slice, true);
}

void motor_stop(motor_t *motor) {
    pwm_set_enabled(motor->slice, false);
    motor_enable(motor, false);
}

void lane_setup(lane_t *lane, uint pin_in, uint pin_out, motor_t m, int lane_id, tmc_t *tmc) {
    debounced_input_init(&lane->in_sw, pin_in);
    debounced_input_init(&lane->out_sw, pin_out);
    lane->m = m;
    lane->task = TASK_IDLE;
    lane->task_limit_mm = 0.0f;
    lane->target_sps = 0;
    lane->current_sps = 0;
    lane->ramp_last_tick_ms = 0;
    lane->fault = FAULT_NONE;
    lane->tmc = tmc;
    lane->motion_started_ms = 0;
    lane->task_started_ms = 0;
    lane->dry_spin_ms = 0;
    lane->fault = FAULT_NONE;
    lane->lane_id = lane_id;
    lane->runout_block_until_ms = 0;
    lane->retract_deadline_ms = 0;
    lane->unload_sensor_latch = false;
    lane->buf_tension_since_ms = 0;
    lane->dist_at_in_clear_mm = 0.0f;
    lane->task_forward = false;
    lane->prev_in = false;
    lane->unload_to_in = false;
    lane->suppress_unloaded_event = false;
    lane->load_completed = false;
    lane->move_ignore_buffer = false;
}

void lane_stop(lane_t *lane) {
    lane->task = TASK_IDLE;
    lane->task_started_ms = 0;
    lane->dry_spin_ms = 0;
    lane->unload_sensor_latch = false;
    lane->unload_buf_recover_done = false;
    lane->retract_deadline_ms = 0;
    lane->buf_tension_since_ms = 0;
    lane->reload_tail_ms = 0;
    lane->suppress_unloaded_event = false;
    lane->task_forward = false;
    lane->move_ignore_buffer = false;
    lane->current_sps = 0;
    lane->target_sps = 0;
    motor_stop(&lane->m);
    int idx = lane->lane_id - 1;
    tmc_set_stealthchop_sps(lane->tmc, TMC_STEALTHCHOP_SPS[idx], TMC_MICROSTEPS[idx]);
}

void lane_start(lane_t *lane, task_t t, int sps, bool forward, uint32_t now_ms, float limit_mm) {
    lane->task = t;
    lane->fault = FAULT_NONE;
    lane->last_dist_tick_ms = now_ms;
    lane->task_dist_mm = 0.0f;
    lane->dist_at_out_mm = 0.0f;
    lane->unload_sensor_latch = false;
    lane->retract_deadline_ms = 0;
    lane->dist_at_in_clear_mm = 0.0f;
    lane->suppress_unloaded_event = false;
    lane->load_completed = false;
    lane->task_forward = forward;
    lane->move_ignore_buffer = false;

    lane->task_limit_mm = limit_mm;

    lane->target_sps = motion_clamp_rate_sps(sps);
    lane->current_sps = motion_clamp_rate_sps(RAMP_STEP_SPS);
    if (lane->current_sps > lane->target_sps)
        lane->current_sps = lane->target_sps;
    lane->ramp_last_tick_ms = now_ms;
    lane->motion_started_ms = now_ms;
    if (lane->task_started_ms == 0)
        lane->task_started_ms = now_ms;

    motor_enable(&lane->m, true);
    motor_set_dir(&lane->m, forward);
    motor_set_rate_sps(&lane->m, lane->current_sps);
    int idx = lane->lane_id - 1;
    tmc_set_stealthchop_sps(lane->tmc, TMC_STEALTHCHOP_SPS[idx], TMC_MICROSTEPS[idx]);
    tmc_set_run_current_ma(lane->tmc, TMC_RUN_CURRENT_MA[idx], TMC_HOLD_CURRENT_MA[idx]);
}

static void lane_tick_autoload(lane_t *lane, uint32_t now_ms, const char *lane_s) {
    if (lane_out_present(lane)) {
        if (AUTOLOAD_RETRACT_MM > 0) {
            float secs =
                (float)AUTOLOAD_RETRACT_MM / ((float)REV_SPS * MM_PER_STEP[lane->lane_id - 1]);
            if (secs < AUTOLOAD_MIN_RETRACT_S)
                secs = AUTOLOAD_MIN_RETRACT_S;
            lane->dist_at_out_mm = lane->task_dist_mm;
            lane->retract_deadline_ms = now_ms + (uint32_t)(secs * MS_PER_SECOND_F);
            lane->task = TASK_UNLOAD;
            motor_set_dir(&lane->m, false);
            lane->target_sps = REV_SPS;
            lane->current_sps = RAMP_STEP_SPS;
            lane->ramp_last_tick_ms = now_ms;
            motor_set_rate_sps(&lane->m, lane->current_sps);
        } else {
            lane_stop(lane);
        }
    } else if (lane->task_dist_mm > (float)DIST_IN_OUT * PRELOAD_JAM_MARGIN_FRAC) {
        lane_stop(lane);
        tc_enter_error("PRELOAD_JAM");
    } else if (lane->task_limit_mm > 0 && lane->task_dist_mm >= lane->task_limit_mm) {
        lane_stop(lane);
    }
}

static void lane_tick_unload(lane_t *lane, uint32_t now_ms, const char *lane_s) {
    if (lane->retract_deadline_ms == 0) {
        /* Type-D recover: buffer hits TENSION mid-retract -> brief forward jog
           to relieve, then resume. Type-D ONLY. Type-P homes at the tension
           rail and relaxes back to it throughout ANY normal retract (mid-tube,
           deep, or compression-start), indistinguishable from a real jam by
           position alone — so type-P uses no position-based relief/block and
           relies on the UNLOAD_MAX distance limit (UNLOAD_TIMEOUT) below for
           the stuck case. */
        bool buf_recover_due = (BUF_SENSOR_TYPE == 0 && g_buf.state == BUF_TENSION);

        if (lane->unload_sensor_latch && !lane->unload_to_in) {
            float moved_mm = lane->task_dist_mm - lane->dist_at_out_mm;
            if (moved_mm >= BUF_SWITCH_SPAN_HALF_MM) {
                lane->task_dist_mm = lane->dist_at_out_mm;
                lane->unload_sensor_latch = false;

                motor_stop(&lane->m);
                lane->target_sps = REV_SPS;
                lane->current_sps = REV_SPS;
                lane->ramp_last_tick_ms = now_ms;
                motor_enable(&lane->m, true);
                motor_set_dir(&lane->m, false);
                motor_set_rate_sps(&lane->m, lane->current_sps);
            }
        } else if (!lane->unload_to_in && !lane->unload_buf_recover_done && buf_recover_due) {
            motor_stop(&lane->m);
            lane->unload_buf_recover_done = true;
            lane->unload_sensor_latch = true;
            lane->dist_at_out_mm = lane->task_dist_mm;

            lane->target_sps = BUF_STAB_SPS;
            lane->current_sps = BUF_STAB_SPS;
            lane->ramp_last_tick_ms = now_ms;
            motor_enable(&lane->m, true);
            motor_set_dir(&lane->m, true);
            motor_set_rate_sps(&lane->m, lane->current_sps);
        } else if (lane_out_present(lane)) {
        } else if (lane->unload_to_in && lane_in_present(lane)) {
            if (!lane->unload_sensor_latch) {
                lane->unload_sensor_latch = true;
                lane->dist_at_out_mm = lane->task_dist_mm;
            }
            float dist_since_out = lane->task_dist_mm - lane->dist_at_out_mm;
            if (dist_since_out > (float)DIST_IN_OUT * UNLOAD_TO_IN_JAM_MARGIN_FRAC) {
                lane_stop(lane);
                tc_enter_error("UNLOAD_JAM");
            }
        } else {
            if (lane->unload_to_in) {
                lane_stop(lane);
                cmd_event("UNLOADED", lane_s);
            } else {
                lane->dist_at_out_mm = lane->task_dist_mm;
                lane->retract_deadline_ms = now_ms + UNLOAD_RETRACT_TIMEOUT_MS;
            }
        }

        if (lane->task_limit_mm > 0 && lane->task_dist_mm >= lane->task_limit_mm) {
            lane_stop(lane);
            cmd_event("UNLOAD_TIMEOUT", NULL);
        }

        /* UNLOAD_TENSION_BLOCK: if the buffer stays in TENSION throughout
           a retract it usually means the printer extruder is gripping and
           pulling forward faster than the MMU can retract — a real block.
           Exception: when BOTH OUT sensors are active (double-load recovery)
           the buffer tension is caused by the OTHER lane being printed, not
           by the printer blocking THIS lane.  Skip the check in that case. */
        if (BUF_SENSOR_TYPE == 0 && UNLOAD_TENSION_BLOCK_MS > 0 && !lane->unload_to_in &&
            !(lane_out_present(&g_lane_l1) && lane_out_present(&g_lane_l2))) {
            if (g_buf.state == BUF_TENSION) {
                if (lane->buf_tension_since_ms == 0)
                    lane->buf_tension_since_ms = now_ms;
                else if ((int32_t)(now_ms - lane->buf_tension_since_ms) >=
                         UNLOAD_TENSION_BLOCK_MS) {
                    lane_stop(lane);
                    cmd_event("UNLOAD_BLOCKED", NULL);
                }
            } else {
                lane->buf_tension_since_ms = 0;
            }
        }
    } else {
        bool done;
        if (!lane->unload_to_in) {
            float retracted = lane->task_dist_mm - lane->dist_at_out_mm;
            done = retracted >= (float)AUTOLOAD_RETRACT_MM ||
                   (int32_t)(now_ms - lane->retract_deadline_ms) >= 0;
        } else {
            done = (int32_t)(now_ms - lane->retract_deadline_ms) >= 0;
        }
        if (done) {
            bool suppress_event = lane->suppress_unloaded_event;
            lane_stop(lane);
            if (!suppress_event)
                cmd_event("UNLOADED", lane_s);
        }
    }
}

static void lane_tick_load_full(lane_t *lane, uint32_t now_ms, const char *lane_s) {
    if (lane_out_present(lane) && !lane->unload_sensor_latch) {
        lane->unload_sensor_latch = true;
        lane->dist_at_out_mm = lane->task_dist_mm;
    }

    if (TS_BUF_FALLBACK_MS > 0 && lane->unload_sensor_latch) {
        if (g_buf.state == BUF_COMPRESSION) {
            if (lane->buf_tension_since_ms == 0)
                lane->buf_tension_since_ms = now_ms;
            else if ((int32_t)(now_ms - lane->buf_tension_since_ms) >= TS_BUF_FALLBACK_MS)
                set_toolhead_filament(true);
        } else {
            lane->buf_tension_since_ms = 0;
        }
    }

    bool buf_tension_sane = (BUF_SENSOR_TYPE == 0 && g_buf.state == BUF_TENSION);
    bool buf_compression_sane = false;
    if (lane->unload_sensor_latch) {
        float dist_since_out = lane->task_dist_mm - lane->dist_at_out_mm;
        float threshold = (float)DIST_OUT_Y + (float)DIST_Y_BUF +
                          (float)BUF_MAX_TRAVEL_MM / LOAD_BYPASS_HALF_TRAVEL_DIVISOR;
        bool distance_passed = (dist_since_out >= threshold * LOAD_BYPASS_DISTANCE_FRAC);
        /* For Type-P, we do not require the distance bypass gate because we have continuous
           analog tracking and the buffer won't deviate from home (1.0) until filament hits the
           gears. */
        if (BUF_SENSOR_TYPE == 1) {
            distance_passed = true;
        }

        if (!distance_passed) {
            buf_tension_sane = false;
        } else {
            if (BUF_SENSOR_TYPE == 1) {
                if (g_buf_pos > PSF_LOAD_CONTACT_THRESHOLD_NORM) {
                    buf_compression_sane = true;
                }
            } else if (g_buf.state == BUF_COMPRESSION) {
                buf_compression_sane = true;
            }
        }
    } else {
        buf_tension_sane = false;
    }

    bool loaded = toolhead_has_filament || buf_tension_sane || buf_compression_sane;

    if (loaded) {
        lane->load_completed = true;
        lane_stop(lane);
        cmd_event("LOADED", lane_s);
        if (AUTO_MODE && BUF_SENSOR_TYPE == 0) {
            /* Type-D: engage sync immediately on load completion. Type-P: do
               NOT force SYNC_ACTIVE here — the buffer is at COMPRESSION right
               after the tip hits the gears, and the continuous controller would
               overfeed into them. The D18 auto-start (tension transition +
               g_buf_pos > 0.6) engages type-P sync when the extruder demands. */
            sync_set_state(SYNC_ACTIVE);
            sync_auto_started = true;
            sync_idle_since_ms = 0;
        } else if (BUF_SENSOR_TYPE == 1) {
            /* Type-P: confidently loaded now (gear contact / toolhead), so park
               the buffer at goal. Presence-gated; self-aborts (~200 ms,
               BUF_STAB:STAGNANT_TIMEOUT) if the buffer doesn't track the MMU —
               i.e. not actually coupled, so we stop rather than dry-spin. */
            buffer_stabilize_request(now_ms);
        }
    } else if (!lane_in_present(lane) &&
               (int32_t)(now_ms - lane->task_started_ms) >= LOAD_RUNOUT_GRACE_MS) {
        if (lane_out_present(lane)) {
            lane->reload_tail_ms = now_ms;
        } else if (lane_tail_in_transit(lane)) {
        } else {
            lane_stop(lane);
            cmd_event("RUNOUT", lane_s);
            if (RELOAD_MODE && tc_state() == TC_IDLE)
                reload_trigger(lane->lane_id, now_ms);
        }
    } else if (!lane->unload_sensor_latch &&
               (int32_t)(now_ms - lane->motion_started_ms) >= LOAD_FULL_SENSOR_TIMEOUT_MS) {
        lane_stop(lane);
        cmd_event("RUNOUT", lane_s);
    } else if (lane->task_limit_mm > 0 && lane->task_dist_mm >= lane->task_limit_mm) {
        lane_stop(lane);
        cmd_event("LOAD_TIMEOUT", lane_s);
    }
}

static void lane_tick_move(lane_t *lane, uint32_t now_ms, const char *lane_s) {
    if (!lane->move_ignore_buffer && BUF_SENSOR_TYPE == 0) {
        if (lane->task_forward && g_buf.state == BUF_COMPRESSION) {
            lane_stop(lane);
            lane->fault = FAULT_BUF;
            cmd_event("FAULT:MOVE_COMPRESSION", lane_s);
        } else if (!lane->task_forward && g_buf.state == BUF_TENSION) {
            lane_stop(lane);
            lane->fault = FAULT_BUF;
            cmd_event("FAULT:MOVE_TENSION", lane_s);
        }
    }

    if (lane->task == TASK_MOVE && lane->task_limit_mm > 0 &&
        lane->task_dist_mm >= lane->task_limit_mm) {
        lane_stop(lane);
        cmd_event("MOVE_DONE", lane_s);
    }
}

static void lane_tick_feed_autoload(lane_t *lane, uint32_t now_ms, const char *lane_s) {
    if ((int32_t)(now_ms - lane->task_started_ms) >= RUNOUT_DEBOUNCE_MS &&
        (int32_t)(now_ms - lane->runout_block_until_ms) >= 0) {
        if (lane_out_present(lane)) {
            lane->reload_tail_ms = now_ms;
            lane->runout_block_until_ms = now_ms + RUNOUT_REARM_BLOCK_MS;
        } else if (lane_tail_in_transit(lane)) {
        } else {
            cmd_event("RUNOUT", lane_s);
            lane->runout_block_until_ms = now_ms + (uint32_t)RUNOUT_COOLDOWN_MS;
            if (lane->task == TASK_FEED)
                set_toolhead_filament(false);
            lane_stop(lane);
            if (RELOAD_MODE && lane->task == TASK_FEED && tc_state() == TC_IDLE)
                reload_trigger(lane->lane_id, now_ms);
        }
    }
}

static void lane_tick_dry_spin_tail(lane_t *lane, uint32_t now_ms, const char *lane_s) {
    if (lane->task != TASK_IDLE && !lane_in_present(lane) && !lane_out_present(lane) &&
        g_buf.state != BUF_TENSION) {
        if (lane->dry_spin_ms == 0)
            lane->dry_spin_ms = now_ms;
        if ((int32_t)(now_ms - lane->dry_spin_ms) > DRY_SPIN_TIMEOUT_MS) {
            lane_stop(lane);
            lane->fault = FAULT_DRY_SPIN;
            cmd_event("FAULT:DRY_SPIN", lane_s);
        }
    } else {
        lane->dry_spin_ms = 0;
    }

    if (lane->reload_tail_ms != 0 &&
        (lane->task == TASK_FEED || lane->task == TASK_LOAD_FULL || lane->task == TASK_AUTOLOAD)) {
        if (lane_tail_runout_ready(lane)) {
            bool tail_assist_finished = sync_tail_assist_active && lane->task == TASK_FEED;
            if (tail_assist_finished)
                sync_disable(true);
            lane->reload_tail_ms = 0;
            lane->runout_block_until_ms = now_ms + (uint32_t)RUNOUT_COOLDOWN_MS;
            cmd_event("RUNOUT", lane_s);
            if (lane->task == TASK_FEED)
                set_toolhead_filament(false);
            bool was_reload = (RELOAD_MODE && lane->task == TASK_FEED && tc_state() == TC_IDLE);
            lane_stop(lane);
            if (was_reload)
                reload_trigger(lane->lane_id, now_ms);
        }
    }
}

void lane_tick(lane_t *lane, uint32_t now_ms) {
    char lane_s[2] = {(char)('0' + lane->lane_id), 0};

    bool in_p = lane_in_present(lane);
    if (lane->prev_in && !in_p) {
        lane->dist_at_in_clear_mm = lane->task_dist_mm;
    }
    lane->prev_in = in_p;

    if (lane->task != TASK_IDLE && lane->current_sps < lane->target_sps) {
        if ((int32_t)(now_ms - lane->ramp_last_tick_ms) >= RAMP_TICK_MS) {
            lane->ramp_last_tick_ms = now_ms;
            lane->current_sps += RAMP_STEP_SPS;
            if (lane->current_sps > lane->target_sps)
                lane->current_sps = lane->target_sps;
            motor_set_rate_sps(&lane->m, lane->current_sps);
        }
    }

    uint32_t dt_ms = now_ms - lane->last_dist_tick_ms;
    if (dt_ms > 0) {
        int idx = lane_to_idx(lane->lane_id);
        lane->task_dist_mm +=
            (float)lane->current_sps * ((float)dt_ms / MS_PER_SECOND_F) * MM_PER_STEP[idx];
        lane->last_dist_tick_ms = now_ms;
    }

    if (lane->task == TASK_AUTOLOAD) {
        lane_tick_autoload(lane, now_ms, lane_s);
    }

    if (lane->task == TASK_UNLOAD) {
        lane_tick_unload(lane, now_ms, lane_s);
    }

    if (lane->task == TASK_LOAD_FULL) {
        lane_tick_load_full(lane, now_ms, lane_s);
    }

    if (lane->task == TASK_MOVE) {
        lane_tick_move(lane, now_ms, lane_s);
    }

    if ((lane->task == TASK_FEED || lane->task == TASK_AUTOLOAD) && !lane_in_present(lane)) {
        lane_tick_feed_autoload(lane, now_ms, lane_s);
    }

    lane_tick_dry_spin_tail(lane, now_ms, lane_s);
}

void stop_all(void) {
    lane_stop(&g_lane_l1);
    lane_stop(&g_lane_l2);
}

void lane_fault(lane_t *lane, fault_t f) {
    motor_stop(&lane->m);
    lane->task = TASK_IDLE;
    lane->current_sps = 0;
    lane->target_sps = 0;
    lane->fault = f;
}
