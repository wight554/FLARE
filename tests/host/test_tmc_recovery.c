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

    // Run ticks until Lane 2 is inspected
    t += 1000;
    tmc_heartbeat_tick(t);
    t += 1000;
    tmc_heartbeat_tick(t);

    // Health flag drops to 0 (fault)
    assert(g_tmc_health[1] == 0);

    // Fault event EV:TMC:FAULT:2:COMM_FAIL was emitted
    assert(event_logged("TMC:FAULT,2:COMM_FAIL") || event_logged("COMM_FAIL"));

    // Reset comm fail
    sim_tmc_set_comm_fail(2, false);
    printf("OK\n");
}

int main(void) {
    printf("=== TMC2209 Heartbeat & Auto-Recovery Tests ===\n");
    test_idle_heartbeat_healthy();
    test_motion_lockout();
    test_brownout_auto_recovery();
    test_persistent_comm_fault_escalation();
    printf("=== All TMC Recovery tests passed ===\n");
    return 0;
}
