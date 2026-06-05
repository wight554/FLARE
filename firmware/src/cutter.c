/// @file cutter.c
/// @brief Filament cutter actuation: servo/blade drive sequencing and the
///        feed-wait/ramp handling around a cut.
/// @details Driven by toolchange.c during the unload-cut phase. See spec
///          toolchange-orchestration and cutter-feed-timeout.

#include "cutter.h"

#include <stdio.h>
#include <string.h>

#include "hardware/pwm.h"

#include "motion.h"
#include "protocol.h"
#include "toolchange.h"

typedef enum {
    CUT_IDLE,
    CUT_BOOT_PARK,
    CUT_TEST,
    CUT_OPENING,
    CUT_OPEN_WAIT,
    CUT_FEEDING,
    CUT_FEED_WAIT,
    CUT_CLOSING,
    CUT_CLOSE_WAIT,
    CUT_REOPENING,
    CUT_REOPEN_WAIT,
    CUT_REPEAT_CHECK,
    CUT_DONE
} cutter_state_t;

typedef struct {
    cutter_state_t state;
    lane_t *lane;
    uint32_t phase_start_ms;
    uint32_t feed_initial_ms;
    uint32_t feed_repeat_ms;
    uint32_t feed_active_ms;
    int repeats_done;
    int current_sps;
    uint32_t ramp_last_ms;
    bool failed;
} cutter_ctx_t;

cutter_ctx_t g_cut;

static uint g_servo_slice = 0;
static uint g_servo_chan = 0;

#define SERVO_PWM_CLKDIV 125.0f
#define SERVO_PWM_PERIOD_US 20000u
#define SERVO_MIN_PULSE_US 400u
#define SERVO_MAX_PULSE_US 2700u
#define CUTTER_WATCHDOG_SLACK_MS 1000u

// Hobby-servo PWM: standard 50 Hz frame (20 ms), position set by pulse width
// (~0.4-2.7 ms here). clkdiv 125 turns the 125 MHz sysclk into a 1 MHz counter,
// so 1 count = 1 us: wrap = 20000 gives the 20 ms frame and the channel level is
// the pulse width in microseconds directly (see servo_set_us).
static void servo_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    g_servo_slice = pwm_gpio_to_slice_num(pin);
    g_servo_chan = pwm_gpio_to_channel(pin);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, SERVO_PWM_CLKDIV);
    pwm_config_set_wrap(&config, SERVO_PWM_PERIOD_US - 1);
    pwm_init(g_servo_slice, &config, false);
}

static void servo_set_us(uint pin, uint pulse_us) {
    (void)pin;
    if (pulse_us < SERVO_MIN_PULSE_US)
        pulse_us = SERVO_MIN_PULSE_US;
    if (pulse_us > SERVO_MAX_PULSE_US)
        pulse_us = SERVO_MAX_PULSE_US;
    pwm_set_chan_level(g_servo_slice, g_servo_chan, (uint16_t)pulse_us);
    pwm_set_enabled(g_servo_slice, true);
}

static void servo_idle(uint pin) {
    (void)pin;
    pwm_set_enabled(g_servo_slice, false);
}

bool cutter_busy(void) {
    return g_cut.state != CUT_IDLE;
}

bool cutter_failed(void) {
    return g_cut.failed;
}

static uint32_t cut_feed_ms_for_mm(int mm, int idx) {
    float secs = (float)mm / ((float)CUT_FEED_SPS * MM_PER_STEP[idx]);
    if (secs < 0.0f)
        secs = 0.0f;
    return (uint32_t)(secs * MS_PER_SECOND_F);
}

uint32_t cutter_expected_ms(lane_t *lane, bool enable_feed) {
    int repeats = CUT_AMOUNT;
    if (repeats < 1)
        repeats = 1;

    uint32_t feed_ms = 0;
    if (lane && enable_feed) {
        int idx = lane->lane_id - 1;
        if (idx < 0 || idx >= NUM_LANES)
            idx = 0;
        feed_ms = cut_feed_ms_for_mm(CUT_FEED_MM, idx);
        if (repeats > 1) {
            feed_ms += (uint32_t)(repeats - 1) * cut_feed_ms_for_mm(CUT_LENGTH_MM, idx);
        }
    }

    uint32_t settle_ms = (uint32_t)SERVO_SETTLE_MS * (uint32_t)((2 * repeats) + 2);
    return feed_ms + settle_ms + CUTTER_WATCHDOG_SLACK_MS;
}

static void cut_begin_feed(uint32_t now_ms, uint32_t window_ms) {
    g_cut.feed_active_ms = window_ms;
    if (g_cut.lane && window_ms > 0) {
        motor_enable(&g_cut.lane->m, true);
        motor_set_dir(&g_cut.lane->m, true);
        g_cut.current_sps = RAMP_STEP_SPS;
        motor_set_rate_sps(&g_cut.lane->m, g_cut.current_sps);
        g_cut.ramp_last_ms = now_ms;
    } else {
        g_cut.current_sps = 0;
    }
    g_cut.phase_start_ms = now_ms;
    g_cut.state = CUT_FEED_WAIT;
}

void cutter_init(void) {
    servo_init(PIN_SERVO);
    servo_set_us(PIN_SERVO, SERVO_BLOCK_US);
    g_cut.state = CUT_BOOT_PARK;
    g_cut.phase_start_ms = to_ms_since_boot(get_absolute_time());
}

void cutter_start(lane_t *lane, bool enable_feed, uint32_t now_ms) {
    if (g_cut.state != CUT_IDLE && g_cut.state != CUT_BOOT_PARK)
        return;

    g_cut.lane = lane;
    g_cut.repeats_done = 0;
    g_cut.failed = false;

    if (lane && enable_feed) {
        int idx = lane->lane_id - 1;
        g_cut.feed_initial_ms = cut_feed_ms_for_mm(CUT_FEED_MM, idx);
        g_cut.feed_repeat_ms = cut_feed_ms_for_mm(CUT_LENGTH_MM, idx);
    } else {
        g_cut.feed_initial_ms = 0;
        g_cut.feed_repeat_ms = 0;
    }

    g_cut.phase_start_ms = now_ms;
    g_cut.state = CUT_OPENING;

    if (lane) {
        char lane_s[2];
        lane_id_str(lane_s, lane->lane_id);
        cmd_event("TC:CUTTING", lane_s);
    } else {
        cmd_event("TC:CUTTING", "BARE");
    }
}

void cutter_abort(void) {
    if (g_cut.state == CUT_IDLE)
        return;
    g_cut.failed = true;
    servo_set_us(PIN_SERVO, SERVO_BLOCK_US);
    if (g_cut.lane)
        motor_stop(&g_cut.lane->m);
    g_cut.state = CUT_IDLE;
    cmd_event("CUT:ERROR", "ABORTED");
}

static void cutter_fail(const char *reason) {
    if (g_cut.state == CUT_IDLE)
        return;
    g_cut.failed = true;
    servo_set_us(PIN_SERVO, SERVO_BLOCK_US);
    if (g_cut.lane)
        motor_stop(&g_cut.lane->m);
    g_cut.state = CUT_IDLE;
    cmd_event("CUT:ERROR", reason);
}

void cutter_test_us(uint32_t us) {
    if (g_cut.lane)
        motor_stop(&g_cut.lane->m);
    servo_set_us(PIN_SERVO, us);
    g_cut.state = CUT_TEST;
    g_cut.phase_start_ms = to_ms_since_boot(get_absolute_time());
}

static void cutter_tick_feed_wait(uint32_t now_ms, uint32_t age) {
    if (g_cut.lane && g_cut.current_sps < CUT_FEED_SPS) {
        if ((int32_t)(now_ms - g_cut.ramp_last_ms) >= RAMP_TICK_MS) {
            g_cut.current_sps += RAMP_STEP_SPS;
            if (g_cut.current_sps > CUT_FEED_SPS)
                g_cut.current_sps = CUT_FEED_SPS;
            motor_set_rate_sps(&g_cut.lane->m, g_cut.current_sps);
            g_cut.ramp_last_ms = now_ms;
        }
    }
    if (age >= g_cut.feed_active_ms) {
        if (g_cut.lane && g_cut.feed_active_ms > 0) {
            motor_stop(&g_cut.lane->m);
        }
        g_cut.phase_start_ms = now_ms;
        g_cut.state = CUT_CLOSING;
    } else if (age > (uint32_t)CUT_TIMEOUT_FEED_MS) {
        cutter_fail("FEED_TIMEOUT");
    }
}

// cutter_state_t transition map. One cut = open the blade, feed a little filament
// through, close (the cut), reopen, and repeat CUT_AMOUNT times. Every servo move
// is a "do it" state that arms the servo, followed by a "_WAIT" state that holds
// until SERVO_SETTLE_MS (failing on a settle/feed timeout). Any timeout calls
// cutter_fail() -> sets `failed`, parks the blade, returns to CUT_IDLE.
//
//   BOOT_PARK/TEST  settle elapsed                 -> IDLE (one-shot servo park)
//   OPENING         arm open                       -> OPEN_WAIT
//   OPEN_WAIT       settled                        -> FEEDING        (timeout -> fail)
//   FEEDING         start feed motor               -> FEED_WAIT
//   FEED_WAIT       feed window elapsed            -> CLOSING        (feed timeout -> fail)
//   CLOSING         arm close (the cut)            -> CLOSE_WAIT
//   CLOSE_WAIT      settled                        -> REOPENING      (timeout -> fail)
//   REOPENING       arm open                       -> REOPEN_WAIT
//   REOPEN_WAIT     settled                        -> REPEAT_CHECK   (timeout -> fail)
//   REPEAT_CHECK    more repeats? -> FEEDING : DONE
//   DONE            park blade, settle             -> IDLE (emit CUT:DONE)
void cutter_tick(uint32_t now_ms) {
    uint32_t age = now_ms - g_cut.phase_start_ms;

    switch (g_cut.state) {
    case CUT_IDLE:
        return;

    case CUT_BOOT_PARK:
    case CUT_TEST:
        if (age >= (uint32_t)SERVO_SETTLE_MS) {
            servo_idle(PIN_SERVO);
            g_cut.state = CUT_IDLE;
        }
        break;

    case CUT_OPENING:
        servo_set_us(PIN_SERVO, SERVO_OPEN_US);
        g_cut.phase_start_ms = now_ms;
        g_cut.state = CUT_OPEN_WAIT;
        break;

    case CUT_OPEN_WAIT:
        if (age > (uint32_t)CUT_TIMEOUT_SETTLE_MS) {
            cutter_fail("OPEN_TIMEOUT");
        } else if (age >= (uint32_t)SERVO_SETTLE_MS) {
            g_cut.phase_start_ms = now_ms;
            g_cut.state = CUT_FEEDING;
        }
        break;

    case CUT_FEEDING:
        cmd_event("CUT:FEEDING", NULL);
        cut_begin_feed(now_ms,
                       g_cut.repeats_done == 0 ? g_cut.feed_initial_ms : g_cut.feed_repeat_ms);
        break;

    case CUT_FEED_WAIT:
        cutter_tick_feed_wait(now_ms, age);
        break;

    case CUT_CLOSING:
        servo_set_us(PIN_SERVO, SERVO_CLOSE_US);
        g_cut.phase_start_ms = now_ms;
        g_cut.state = CUT_CLOSE_WAIT;
        break;

    case CUT_CLOSE_WAIT:
        if (age > (uint32_t)CUT_TIMEOUT_SETTLE_MS) {
            cutter_fail("CLOSE_TIMEOUT");
        } else if (age >= (uint32_t)SERVO_SETTLE_MS) {
            g_cut.phase_start_ms = now_ms;
            g_cut.state = CUT_REOPENING;
        }
        break;

    case CUT_REOPENING:
        servo_set_us(PIN_SERVO, SERVO_OPEN_US);
        g_cut.phase_start_ms = now_ms;
        g_cut.state = CUT_REOPEN_WAIT;
        break;

    case CUT_REOPEN_WAIT:
        if (age > (uint32_t)CUT_TIMEOUT_SETTLE_MS) {
            cutter_fail("REOPEN_TIMEOUT");
        } else if (age >= (uint32_t)SERVO_SETTLE_MS) {
            g_cut.state = CUT_REPEAT_CHECK;
        }
        break;

    case CUT_REPEAT_CHECK:
        if (g_cut.repeats_done < CUT_AMOUNT - 1) {
            g_cut.repeats_done++;
            g_cut.phase_start_ms = now_ms;
            g_cut.state = CUT_FEEDING;
        } else {
            g_cut.phase_start_ms = now_ms;
            g_cut.state = CUT_DONE;
        }
        break;

    case CUT_DONE:
        servo_set_us(PIN_SERVO, SERVO_BLOCK_US);
        if (age >= (uint32_t)SERVO_SETTLE_MS) {
            servo_idle(PIN_SERVO);
            g_cut.state = CUT_IDLE;
            cmd_event("CUT:DONE", NULL);
        }
        break;
    }
}
