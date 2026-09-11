/// @file test_cutter.c
/// @brief Host unit tests for cutter abort/fail servo settle (12-SPEC §2).

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "controller_shared.h"
#include "cutter.h"
#include "motion.h"
#include "sim_fakes.h"

#define SERVO_SLICE (PIN_SERVO % 8u)

static bool event_logged(const char *expected) {
    for (int i = 0; i < g_sim_event_count; i++) {
        if (strstr(g_sim_events[i].text, expected) != NULL) {
            return true;
        }
    }
    return false;
}

/* cutter_abort() mid-stroke must drive the blade to block and keep the servo
   energized for servo_settle_ms before idling — never PWM-off on the same tick. */
static void test_abort_holds_block_until_settled(void) {
    printf("test_abort_holds_block_until_settled... ");
    g_sim_event_count = 0;
    g_now_ms = 1000;
    cutter_init();
    cutter_tick(g_now_ms + (uint32_t)g_servo_settle_ms + 1); // leave BOOT_PARK
    assert(!cutter_busy());

    cutter_start(&g_lane_l1, false, 2000);
    assert(cutter_busy());
    cutter_tick(2001);
    assert(sim_pwm_level(SERVO_SLICE) == (uint16_t)g_servo_open_us);

    g_now_ms = 2100;
    cutter_abort();
    assert(event_logged("CUT:ERROR,ABORTED"));
    assert(cutter_failed());
    assert(cutter_busy());                // settling counts as busy
    assert(sim_pwm_enabled(SERVO_SLICE)); // still energized
    assert(sim_pwm_level(SERVO_SLICE) == (uint16_t)g_servo_block_us);

    cutter_tick(2100 + (uint32_t)g_servo_settle_ms - 1);
    assert(cutter_busy());
    assert(sim_pwm_enabled(SERVO_SLICE));

    cutter_tick(2100 + (uint32_t)g_servo_settle_ms);
    assert(!cutter_busy());
    assert(!sim_pwm_enabled(SERVO_SLICE));
    assert(cutter_failed());
    printf("OK\n");
}

/* A second abort during settle is a no-op (no double event, no clock restart). */
static void test_abort_during_settle_is_idempotent(void) {
    printf("test_abort_during_settle_is_idempotent... ");
    g_sim_event_count = 0;
    g_now_ms = 5000;
    cutter_start(&g_lane_l1, false, 5000);
    cutter_tick(5001);
    cutter_abort();
    int events = g_sim_event_count;
    g_now_ms = 5050;
    cutter_abort();
    assert(g_sim_event_count == events);
    cutter_tick(5000 + (uint32_t)g_servo_settle_ms + 1);
    assert(!cutter_busy());
    printf("OK\n");
}

int main(void) {
    printf("=== Starting test_cutter ===\n");
    test_abort_holds_block_until_settled();
    test_abort_during_settle_is_idempotent();
    printf("=== All test_cutter tests PASSED ===\n");
    return 0;
}
