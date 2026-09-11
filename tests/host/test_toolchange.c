/// @file test_toolchange.c
/// @brief Host unit tests for toolchange load-park / retry contract (12-SPEC §1).

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "controller_shared.h"
#include "motion.h"
#include "sim_fakes.h"
#include "sync.h"
#include "toolchange.h"

static bool event_logged(const char *expected) {
    for (int i = 0; i < g_sim_event_count; i++) {
        if (strstr(g_sim_events[i].text, expected) != NULL) {
            return true;
        }
    }
    return false;
}

static void reset_tc_at_wait_th(int lane_id, bool load_completed, bool th_present,
                                buf_state_t buf) {
    g_sim_event_count = 0;
    memset(&g_tc_ctx, 0, sizeof(g_tc_ctx));
    g_active_lane = lane_id;
    g_tc_ctx.target_lane = lane_id;
    g_tc_ctx.state = TC_LOAD_WAIT_TH;
    lane_t *lane = lane_ptr(lane_id);
    lane->task = TASK_IDLE;
    lane->fault = FAULT_NONE;
    lane->load_completed = load_completed;
    g_toolhead_has_filament = th_present;
    g_buf.state = buf;
    g_boot_stabilizing = false;
    lane->in_sw.stable = true;
}

/* Buffer-inferred completion (tip already in the gears, buffer at COMPRESSION):
   park must be skipped even when TC_TS_PARK_MM > 0 — a guarded forward move
   would fault immediately and latch FAULT_BUF for the rest of the print. */
static void test_park_skipped_on_buffer_completion(void) {
    printf("test_park_skipped_on_buffer_completion... ");
    g_buf_sensor_type = BUF_SENSOR_TYPE_D;
    g_tc_ts_park_mm = 25.0f;
    reset_tc_at_wait_th(1, true, false, BUF_COMPRESSION);

    tc_tick(1000);
    assert(g_tc_ctx.state == TC_LOAD_DONE);
    assert(g_lane_l1.task == TASK_IDLE);
    assert(g_lane_l1.fault == FAULT_NONE);
    assert(!event_logged("TS_PARKED"));
    printf("OK\n");
}

/* TS-edge completion on type-D: park runs, and a COMPRESSION stop during the
   park is the goal (gears have the filament), not a fault. */
static void test_park_compression_stop_is_success(void) {
    printf("test_park_compression_stop_is_success... ");
    g_buf_sensor_type = BUF_SENSOR_TYPE_D;
    g_tc_ts_park_mm = 25.0f;
    reset_tc_at_wait_th(1, true, true, BUF_NEUTRAL);

    tc_tick(1000);
    assert(g_tc_ctx.state == TC_LOAD_PARK);
    assert(g_lane_l1.task == TASK_MOVE);

    g_buf.state = BUF_COMPRESSION;
    lane_tick(&g_lane_l1, 1001);
    assert(g_lane_l1.task == TASK_IDLE);
    assert(g_lane_l1.fault == FAULT_BUF);

    tc_tick(1002);
    assert(g_tc_ctx.state == TC_LOAD_DONE);
    assert(g_lane_l1.fault == FAULT_NONE);
    assert(event_logged("TS_PARKED"));
    printf("OK\n");
}

/* Type-P has no COMPRESSION guard on TASK_MOVE: a park would push against the
   analog rail, so it is skipped regardless of TC_TS_PARK_MM. */
static void test_park_skipped_on_type_p(void) {
    printf("test_park_skipped_on_type_p... ");
    g_buf_sensor_type = BUF_SENSOR_TYPE_P;
    g_tc_ts_park_mm = 25.0f;
    reset_tc_at_wait_th(1, true, true, BUF_NEUTRAL);

    tc_tick(1000);
    assert(g_tc_ctx.state == TC_LOAD_DONE);
    assert(g_lane_l1.task == TASK_IDLE);
    g_buf_sensor_type = BUF_SENSOR_TYPE_D;
    printf("OK\n");
}

/* Retry exhaustion names the real cause (06-SPEC §4.1). */
static void test_retry_exhaustion_reports_ts_not_hit(void) {
    printf("test_retry_exhaustion_reports_ts_not_hit... ");
    g_tc_ts_retries = 1;
    g_tc_ts_retry_retract_mm = 10.0f;
    reset_tc_at_wait_th(1, false, false, BUF_NEUTRAL);

    tc_tick(1000);
    assert(g_tc_ctx.state == TC_LOAD_RETRY_RETRACT);
    assert(g_tc_ctx.ts_retries == 1);
    g_lane_l1.task = TASK_IDLE;
    tc_tick(1001);
    assert(g_tc_ctx.state == TC_LOAD_WAIT_TH);
    g_lane_l1.task = TASK_IDLE;
    g_lane_l1.load_completed = false;
    tc_tick(1002);
    assert(g_tc_ctx.state == TC_ERROR);
    assert(event_logged("TC:ERROR,TS_NOT_HIT"));
    printf("OK\n");
}

/* An empty lane is never re-advanced: RUNOUT during load is a terminal error. */
static void test_no_retry_on_runout(void) {
    printf("test_no_retry_on_runout... ");
    g_tc_ts_retries = 2;
    reset_tc_at_wait_th(1, false, false, BUF_NEUTRAL);
    g_lane_l1.in_sw.stable = false;

    tc_tick(1000);
    assert(g_tc_ctx.state == TC_ERROR);
    assert(g_tc_ctx.ts_retries == 0);
    assert(event_logged("TC:ERROR,RUNOUT"));
    printf("OK\n");
}

int main(void) {
    printf("=== Starting test_toolchange ===\n");
    test_park_skipped_on_buffer_completion();
    test_park_compression_stop_is_success();
    test_park_skipped_on_type_p();
    test_retry_exhaustion_reports_ts_not_hit();
    test_no_retry_on_runout();
    printf("=== All test_toolchange tests PASSED ===\n");
    return 0;
}
