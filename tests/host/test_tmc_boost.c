/// @file test_tmc_boost.c
/// @brief Host unit tests for TMC tension current boost (Phase 16 Plan 01).
///        Covers R1, R2, R3, R4, R6: edge activation, hysteresis release,
///        unconditional exit resets, brownout recovery parity, and safety clamps.

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

static void init_lane_hardware(void) {
    g_lane_l1.lane_id = 1;
    g_lane_l1.tmc = &g_tmc_l1;
    g_lane_l1.task = TASK_IDLE;
    g_lane_l1.fault = FAULT_NONE;

    g_lane_l2.lane_id = 2;
    g_lane_l2.tmc = &g_tmc_l2;
    g_lane_l2.task = TASK_IDLE;
    g_lane_l2.fault = FAULT_NONE;

    set_active_lane(1);
    g_boot_stabilizing = false;
    g_bypass = false;
    g_tc_ctx.state = TC_IDLE;
}

static void test_boost_activation_and_hysteresis(void) {
    printf("test_boost_activation_and_hysteresis... ");
    init_lane_hardware();
    g_sim_event_count = 0;
    sim_tmc_reset_counts();

    g_sync_state = SYNC_ACTIVE;
    g_buf_sensor_type = BUF_SENSOR_TYPE_P;
    g_tmc_run_current_ma[0] = 800;
    g_tmc_hold_current_ma[0] = 400;
    g_sync_tension_boost_irun[0] = 1000;
    g_sync_tension_boost_on = -0.50f;
    g_sync_tension_boost_off = -0.30f;
    g_sync_tension_boost_active[0] = false;

    // Buffer position -0.49f: above activation threshold, no boost
    g_buf_pos = -0.49f;
    sync_check_tension_boost(100);
    assert(!sync_is_tension_boost_active(1));
    assert(sim_tmc_get_write_count(1) == 0);
    assert(sync_get_active_run_current_ma(1) == 800);

    // Buffer position -0.50f: reaches activation threshold, triggers boost
    g_buf_pos = -0.50f;
    sync_check_tension_boost(120);
    assert(sync_is_tension_boost_active(1));
    assert(sim_tmc_get_write_count(1) == 1);
    assert(event_logged("TMC:BOOST"));
    assert(sync_get_active_run_current_ma(1) == 1000);
    assert(sim_tmc_get_run_current_ma(1) == 1000);
    assert(g_shadow_ihold_irun_valid[0]);
    assert(g_shadow_ihold_irun[0] == build_ihold_irun_reg(1000, 400, g_shadow_vsense[0]));

    // Repeated ticks deeper in tension (-0.60f): strict edge detection, zero redundant writes
    for (int t = 140; t <= 200; t += 20) {
        g_buf_pos = -0.60f;
        sync_check_tension_boost((uint32_t)t);
        assert(sync_is_tension_boost_active(1));
        assert(sim_tmc_get_write_count(1) == 1);
    }

    // Moving up into hysteresis zone (-0.35f): boost stays active, no writes
    g_buf_pos = -0.35f;
    sync_check_tension_boost(220);
    assert(sync_is_tension_boost_active(1));
    assert(sim_tmc_get_write_count(1) == 1);

    // Reaching release threshold (-0.30f): releases boost back to baseline
    g_buf_pos = -0.30f;
    sync_check_tension_boost(240);
    assert(!sync_is_tension_boost_active(1));
    assert(sim_tmc_get_write_count(1) == 2);
    assert(event_logged("TMC:NORMAL"));
    assert(sync_get_active_run_current_ma(1) == 800);
    assert(sim_tmc_get_run_current_ma(1) == 800);
    assert(g_shadow_ihold_irun[0] == build_ihold_irun_reg(800, 400, g_shadow_vsense[0]));

    // Remaining in relaxed zone (-0.20f): no further writes
    g_buf_pos = -0.20f;
    sync_check_tension_boost(260);
    assert(!sync_is_tension_boost_active(1));
    assert(sim_tmc_get_write_count(1) == 2);

    printf("OK\n");
}

static void test_boost_unconditional_exit_reset(void) {
    printf("test_boost_unconditional_exit_reset... ");
    init_lane_hardware();

    g_sync_state = SYNC_ACTIVE;
    g_buf_sensor_type = BUF_SENSOR_TYPE_P;
    g_tmc_run_current_ma[0] = 800;
    g_tmc_hold_current_ma[0] = 400;
    g_sync_tension_boost_irun[0] = 1000;
    g_sync_tension_boost_on = -0.50f;
    g_sync_tension_boost_off = -0.30f;

    // Helper to engage boost
    #define ENGAGE_BOOST() do { \
        g_sync_state = SYNC_ACTIVE; \
        g_buf_pos = -0.55f; \
        sync_check_tension_boost(100); \
        assert(sync_is_tension_boost_active(1)); \
        assert(sync_get_active_run_current_ma(1) == 1000); \
    } while (0)

    // Case A: Exit to SYNC_OFF
    ENGAGE_BOOST();
    int writes_before = sim_tmc_get_write_count(1);
    g_sim_event_count = 0;
    sync_set_state(SYNC_OFF);
    assert(!sync_is_tension_boost_active(1));
    assert(event_logged("TMC:NORMAL"));
    assert(sim_tmc_get_write_count(1) > writes_before);
    assert(sync_get_active_run_current_ma(1) == 800);
    assert(sim_tmc_get_run_current_ma(1) == 800);

    // Case B: Fault trip to SYNC_FAULT_HOLD (e.g. Phase 13 persistent tension trip)
    ENGAGE_BOOST();
    writes_before = sim_tmc_get_write_count(1);
    g_sim_event_count = 0;
    sync_set_state(SYNC_FAULT_HOLD);
    assert(!sync_is_tension_boost_active(1));
    assert(event_logged("TMC:NORMAL"));
    assert(sim_tmc_get_write_count(1) > writes_before);
    assert(sync_get_active_run_current_ma(1) == 800);
    assert(sim_tmc_get_run_current_ma(1) == 800);

    // Case C: Emergency stop_all()
    ENGAGE_BOOST();
    writes_before = sim_tmc_get_write_count(1);
    g_sim_event_count = 0;
    stop_all();
    assert(!sync_is_tension_boost_active(1));
    assert(event_logged("TMC:NORMAL"));
    assert(sim_tmc_get_write_count(1) > writes_before);
    assert(sync_get_active_run_current_ma(1) == 800);
    assert(sim_tmc_get_run_current_ma(1) == 800);

    // Case D: Explicit sync_disable()
    ENGAGE_BOOST();
    writes_before = sim_tmc_get_write_count(1);
    g_sim_event_count = 0;
    sync_disable(false);
    assert(!sync_is_tension_boost_active(1));
    assert(event_logged("TMC:NORMAL"));
    assert(sim_tmc_get_write_count(1) > writes_before);
    assert(sync_get_active_run_current_ma(1) == 800);
    assert(sim_tmc_get_run_current_ma(1) == 800);

    #undef ENGAGE_BOOST
    printf("OK\n");
}

static void test_boost_heartbeat_brownout_recovery(void) {
    printf("test_boost_heartbeat_brownout_recovery... ");
    init_lane_hardware();
    g_sync_state = SYNC_ACTIVE;
    g_buf_sensor_type = BUF_SENSOR_TYPE_P;
    g_tmc_run_current_ma[0] = 800;
    g_tmc_hold_current_ma[0] = 400;
    g_sync_tension_boost_irun[0] = 1000;
    g_sync_tension_boost_on = -0.50f;
    g_sync_tension_boost_off = -0.30f;

    // Engage boost
    g_buf_pos = -0.55f;
    sync_check_tension_boost(100);
    assert(sync_is_tension_boost_active(1));
    assert(sync_get_active_run_current_ma(1) == 1000);

    // Simulate TMC heartbeat brownout re-apply while boosted
    sim_tmc_reset_counts();
    sync_tmc_settings(1);
    // Verified that sync_tmc_settings re-applied boosted current (1000 mA), not base current (800 mA)
    assert(sim_tmc_get_run_current_ma(1) == 1000);
    assert(g_shadow_ihold_irun[0] == build_ihold_irun_reg(1000, 400, g_shadow_vsense[0]));

    // Release boost
    sync_tension_boost_reset_lane(1);
    assert(!sync_is_tension_boost_active(1));
    assert(sync_get_active_run_current_ma(1) == 800);

    // Re-apply settings after release
    sync_tmc_settings(1);
    assert(sim_tmc_get_run_current_ma(1) == 800);
    assert(g_shadow_ihold_irun[0] == build_ihold_irun_reg(800, 400, g_shadow_vsense[0]));

    printf("OK\n");
}

static void test_boost_prohibitions_and_clamps(void) {
    printf("test_boost_prohibitions_and_clamps... ");
    init_lane_hardware();
    g_sim_event_count = 0;
    sim_tmc_reset_counts();

    g_sync_state = SYNC_ACTIVE;
    g_tmc_run_current_ma[0] = 800;
    g_tmc_hold_current_ma[0] = 400;
    g_sync_tension_boost_on = -0.50f;
    g_sync_tension_boost_off = -0.30f;

    // 1. Clamping test: Target 1500 mA must clamp to 1200 mA hardware safety limit
    g_buf_sensor_type = BUF_SENSOR_TYPE_P;
    g_sync_tension_boost_irun[0] = 1500;
    g_buf_pos = -0.60f;
    sync_check_tension_boost(100);
    assert(sync_is_tension_boost_active(1));
    assert(sync_get_active_run_current_ma(1) == 1200);
    assert(sim_tmc_get_run_current_ma(1) == 1200);
    assert(g_shadow_ihold_irun[0] == build_ihold_irun_reg(1200, 400, g_shadow_vsense[0]));

    sync_tension_boost_reset_lane(1);
    assert(!sync_is_tension_boost_active(1));
    sim_tmc_reset_counts();

    // 2. Type-D Prohibition: Type-D microswitch buffers must NEVER trigger boost
    g_buf_sensor_type = BUF_SENSOR_TYPE_D;
    g_sync_tension_boost_irun[0] = 1000;
    g_buf_pos = -0.80f;
    sync_check_tension_boost(120);
    assert(!sync_is_tension_boost_active(1));
    assert(sim_tmc_get_write_count(1) == 0);

    // 3. Compression Prohibition: Buffer position in compression (+0.50f) must NEVER trigger boost
    g_buf_sensor_type = BUF_SENSOR_TYPE_P;
    g_buf_pos = 0.50f;
    sync_check_tension_boost(140);
    assert(!sync_is_tension_boost_active(1));
    assert(sim_tmc_get_write_count(1) == 0);

    // 4. Zero-boost / disabled: Setting boost to 0 or <= baseline must never trigger boost
    g_sync_tension_boost_irun[0] = 0;
    g_buf_pos = -0.80f;
    sync_check_tension_boost(160);
    assert(!sync_is_tension_boost_active(1));
    assert(sim_tmc_get_write_count(1) == 0);

    g_sync_tension_boost_irun[0] = 700; // less than base (800)
    sync_check_tension_boost(180);
    assert(!sync_is_tension_boost_active(1));
    assert(sim_tmc_get_write_count(1) == 0);

    printf("OK\n");
}

int main(void) {
    printf("=== TMC Tension Current Boost Unit Tests ===\n");
    test_boost_activation_and_hysteresis();
    test_boost_unconditional_exit_reset();
    test_boost_heartbeat_brownout_recovery();
    test_boost_prohibitions_and_clamps();
    printf("All TMC Tension Boost tests passed\n");
    return 0;
}
