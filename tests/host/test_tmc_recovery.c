/// @file test_tmc_recovery.c
/// @brief Host unit tests for TMC2209 register heartbeat, motion lockout,
///        brownout auto-recovery, and persistent fault escalation.

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "controller_shared.h"
#include "motion.h"
#include "settings_store.h"
#include "sim_fakes.h"
#include "sync.h"
#include "tmc2209.h"
#include "toolchange.h"

static bool event_logged(const char *expected) {
    for (int i = 0; i < g_sim_event_count; i++) {
        if (strstr(g_sim_events[i].text, expected) != NULL) {
            return true;
        }
    }
    return false;
}

static void test_idle_heartbeat_healthy(void) {
    printf("test_idle_heartbeat_healthy... ");
    g_sim_event_count = 0;
    sim_tmc_reset_counts();

    g_lane_l1.task = TASK_IDLE;
    g_lane_l2.task = TASK_IDLE;
    g_tc_ctx.state = TC_IDLE;
    g_boot_stabilizing = false;

    sync_tmc_settings(1);
    sync_tmc_settings(2);

    uint32_t t = 1000;
    // First call initializes last_ms
    tmc_heartbeat_tick(t);
    assert(sim_tmc_get_read_count(1) == 0);
    assert(sim_tmc_get_read_count(2) == 0);

    // After 1000ms: polls Lane 1
    t += 1000;
    tmc_heartbeat_tick(t);
    assert(sim_tmc_get_read_count(1) == 1);
    assert(sim_tmc_get_read_count(2) == 0);
    assert(g_tmc_health[0] == 1);

    // After another 1000ms: polls Lane 2
    t += 1000;
    tmc_heartbeat_tick(t);
    assert(sim_tmc_get_read_count(1) == 1);
    assert(sim_tmc_get_read_count(2) == 1);
    assert(g_tmc_health[1] == 1);

    // Zero error/restored events emitted during normal healthy operation
    assert(g_sim_event_count == 0);
    printf("OK\n");
}

static void test_motion_lockout(void) {
    printf("test_motion_lockout... ");
    g_sim_event_count = 0;

    uint32_t t = 10000;
    // Active motion on lane 1
    g_lane_l1.task = TASK_FEED;
    assert(controller_activity_in_progress() == true);
    sim_tmc_reset_counts();

    // Advance 5 seconds during motion
    for (int i = 0; i < 5; i++) {
        t += 1000;
        tmc_heartbeat_tick(t);
    }
    // Strict lockout: zero UART reads attempted
    assert(sim_tmc_get_read_count(1) == 0);
    assert(sim_tmc_get_read_count(2) == 0);

    // Motion ends
    g_lane_l1.task = TASK_IDLE;
    assert(controller_activity_in_progress() == false);

    // Now heartbeat should proceed on next tick
    t += 1000;
    tmc_heartbeat_tick(t);
    assert(sim_tmc_get_read_count(1) + sim_tmc_get_read_count(2) == 1);
    printf("OK\n");
}

static void test_brownout_auto_recovery(void) {
    printf("test_brownout_auto_recovery... ");
    g_sim_event_count = 0;
    sim_tmc_reset_counts();

    sync_tmc_settings(1);
    sync_tmc_settings(2);

    uint32_t t = 20000;
    tmc_heartbeat_tick(t);

    // Simulate 24V brownout on Lane 1: CHOPCONF resets to silicon default
    sim_tmc_inject_brownout(1);

    int writes_before = sim_tmc_get_write_count(1);

    // Advance time to next tick (which will inspect Lane 1 or Lane 2)
    t += 1000;
    tmc_heartbeat_tick(t);
    t += 1000;
    tmc_heartbeat_tick(t);

    // Lane 1 was inspected, mismatch was detected, and re-applied!
    int writes_after = sim_tmc_get_write_count(1);
    assert(writes_after > writes_before);
    assert(g_tmc_health[0] == 1);

    // Wire event EV:TMC:RESTORED:1 was emitted
    assert(event_logged("TMC:RESTORED,1") || event_logged("TMC:RESTORED"));
    printf("OK\n");
}

static void test_persistent_comm_fault_escalation(void) {
    printf("test_persistent_comm_fault_escalation... ");
    g_sim_event_count = 0;
    sim_tmc_reset_counts();

    sync_tmc_settings(1);
    sync_tmc_settings(2);

    uint32_t t = 30000;
    tmc_heartbeat_tick(t);

    // Inject complete communication failure on Lane 2
    sim_tmc_set_comm_fail(2, true);
    g_sim_sleep_ms_total = 0;

    // Run ticks until Lane 2 is inspected; the first re-apply happens on the
    // probe tick, the remaining TMC_RECOVERY_RETRIES-1 on a 50 ms cadence.
    t += 1000;
    tmc_heartbeat_tick(t);
    t += 1000;
    tmc_heartbeat_tick(t);
    assert(g_tmc_health[1] == 1); // not yet escalated: backoff is non-blocking
    for (int i = 0; i < 4; i++) {
        t += 50;
        tmc_heartbeat_tick(t);
    }

    // Health flag drops to 0 (fault), without a single blocking sleep
    assert(g_tmc_health[1] == 0);
    assert(g_sim_sleep_ms_total == 0);

    // Fault event EV:TMC:FAULT:2:COMM_FAIL was emitted exactly once
    assert(event_logged("TMC:FAULT,2:COMM_FAIL"));
    int fault_events = 0;
    for (int i = 0; i < g_sim_event_count; i++)
        if (strstr(g_sim_events[i].text, "TMC:FAULT") != NULL)
            fault_events++;
    assert(fault_events == 1);

    // Still unreachable: the lane is re-probed each slot but the fault is
    // latched — no second TMC:FAULT (no stop_all storm) for 10 s.
    for (int i = 0; i < 10; i++) {
        t += 1000;
        tmc_heartbeat_tick(t);
        for (int j = 0; j < 4; j++) {
            t += 50;
            tmc_heartbeat_tick(t);
        }
    }
    fault_events = 0;
    for (int i = 0; i < g_sim_event_count; i++)
        if (strstr(g_sim_events[i].text, "TMC:FAULT") != NULL)
            fault_events++;
    assert(fault_events == 1);
    assert(g_tmc_health[1] == 0);

    // Driver comes back: next probe restores health and emits TMC:RESTORED
    sim_tmc_set_comm_fail(2, false);
    g_sim_event_count = 0;
    for (int i = 0; i < 2; i++) {
        t += 1000;
        tmc_heartbeat_tick(t);
    }
    assert(g_tmc_health[1] == 1);
    assert(event_logged("TMC:RESTORED,2"));
    printf("OK\n");
}

/* 12-SPEC §5.1: sync-owned motor authority (held buffer lock, SYNC_ACTIVE at
   zero rate, relief pause, fault hold) locks the heartbeat out even though
   lane->task is IDLE — a 100 ms UART stall there would miss a lock-break. */
static void test_sync_state_lockout(void) {
    printf("test_sync_state_lockout... ");
    g_sim_event_count = 0;
    g_lane_l1.task = TASK_IDLE;
    g_lane_l2.task = TASK_IDLE;
    g_tc_ctx.state = TC_IDLE;
    g_boot_stabilizing = false;
    sync_state_t states[] = {SYNC_ACTIVE, SYNC_RETRACT_ASSIST, SYNC_RELIEF_PAUSE, SYNC_FAULT_HOLD};
    uint32_t t = 50000;
    for (size_t k = 0; k < sizeof(states) / sizeof(states[0]); k++) {
        g_sync_state = states[k];
        assert(controller_activity_in_progress() == false);
        sim_tmc_reset_counts();
        for (int i = 0; i < 5; i++) {
            t += 1000;
            tmc_heartbeat_tick(t);
        }
        assert(sim_tmc_get_read_count(1) == 0);
        assert(sim_tmc_get_read_count(2) == 0);
    }
    g_sync_state = SYNC_OFF;
    t += 1000;
    tmc_heartbeat_tick(t);
    t += 1000;
    tmc_heartbeat_tick(t);
    assert(sim_tmc_get_read_count(1) + sim_tmc_get_read_count(2) >= 1);
    printf("OK\n");
}

int main(void) {
    printf("=== TMC2209 Heartbeat & Auto-Recovery Tests ===\n");
    test_idle_heartbeat_healthy();
    test_motion_lockout();
    test_brownout_auto_recovery();
    test_persistent_comm_fault_escalation();
    test_sync_state_lockout();
    printf("=== All TMC Recovery tests passed ===\n");
    return 0;
}
