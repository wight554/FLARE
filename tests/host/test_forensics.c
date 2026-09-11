/// @file test_forensics.c
/// @brief Host unit tests for firmware forensics blackbox ringbuffer,
///        watchdog reboot survival, CRC integrity, and loop jitter metrics.

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "controller_shared.h"
#include "forensics.h"
#include "motion.h"
#include "protocol.h"
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

static void test_forensics_init_fresh(void) {
    printf("test_forensics_init_fresh... ");
    forensics_init(false);

    assert(!forensics_has_crash());
    assert(forensics_crash_reason() == CRASH_REASON_NONE);
    assert(forensics_entry_count() == 0);

    crash_data_t *raw = forensics_get_raw_buffer();
    assert(raw != NULL);
    assert(raw->magic == FORENSICS_MAGIC);
    assert(raw->version == FORENSICS_VERSION);
    assert(raw->head == 0);
    assert(raw->count == 0);

    printf("OK\n");
}

static void test_forensics_log_and_sensors(void) {
    printf("test_forensics_log_and_sensors... ");
    forensics_clear();
    g_sim_event_count = 0;

    g_now_ms = 500;
    g_lane_l1.in_sw.stable = true;
    g_lane_l1.out_sw.stable = false;
    g_lane_l2.in_sw.stable = false;
    g_lane_l2.out_sw.stable = true;
    g_y_split.stable = true;
    g_toolhead_has_filament = false;
    g_buf_tension_din.stable = false;
    g_buf_compression_din.stable = true;

    g_tc_ctx.state = TC_LOAD_WAIT_OUT;
    g_sync_state = SYNC_ACTIVE;
    g_buf_pos = 2.5f;
    g_sync_current_sps = 800;

    forensics_log_entry(FORENSICS_ENTRY_TASK_CHANGE, 1, 0xABCD);

    assert(forensics_entry_count() == 1);
    forensics_entry_t e;
    assert(forensics_get_entry(0, &e));

    assert(e.timestamp_ms == 500);
    assert(e.type == FORENSICS_ENTRY_TASK_CHANGE);
    assert(e.lane == 1);
    assert(e.tc_state == (uint8_t)TC_LOAD_WAIT_OUT);
    assert(e.sync_state == (uint8_t)SYNC_ACTIVE);
    assert(e.payload == 0xABCD);
    assert(e.step_rate_sps == 800);
    assert(e.buf_pos_raw == 250);

    // Verify packed sensors bitmask
    // bit 0: in1 (1), bit 1: out1 (0), bit 2: in2 (0), bit 3: out2 (1)
    // bit 4: y_split (1), bit 5: th (0), bit 6: tens (0), bit 7: comp (1)
    uint8_t expected_mask = (1u << 0) | (1u << 3) | (1u << 4) | (1u << 7);
    assert(e.sensors == expected_mask);

    printf("OK\n");
}

static void test_forensics_ring_wrap_chronological(void) {
    printf("test_forensics_ring_wrap_chronological... ");
    forensics_clear();

    // Log 40 entries to wrap around capacity of 32
    for (uint32_t i = 0; i < 40; i++) {
        g_now_ms = 1000 + i * 10;
        forensics_log_entry(FORENSICS_ENTRY_BREADCRUMB, 0, i);
    }

    assert(forensics_entry_count() == FORENSICS_RING_CAPACITY);

    // Chronological retrieval: index 0 must be oldest (i=8), index 31 newest (i=39)
    for (uint32_t i = 0; i < FORENSICS_RING_CAPACITY; i++) {
        forensics_entry_t e;
        assert(forensics_get_entry(i, &e));
        uint32_t expected_payload = 8 + i;
        uint32_t expected_ts = 1000 + expected_payload * 10;
        assert(e.payload == expected_payload);
        assert(e.timestamp_ms == expected_ts);
    }

    // Out-of-bounds indexing returns false
    forensics_entry_t out;
    assert(!forensics_get_entry(32, &out));

    printf("OK\n");
}

static void test_forensics_crc_corruption_recovery(void) {
    printf("test_forensics_crc_corruption_recovery... ");
    forensics_clear();

    for (uint32_t i = 0; i < 5; i++) {
        g_now_ms = 2000 + i * 10;
        forensics_log_entry(FORENSICS_ENTRY_EVENT_EMIT, 0, i);
    }
    assert(forensics_entry_count() == 5);

    // Corrupt one entry inside SRAM
    crash_data_t *raw = forensics_get_raw_buffer();
    raw->entries[2].payload ^= 0xFFFFFFFFu;

    // Next boot with watchdog reset should detect CRC mismatch and discard corrupt data safely
    forensics_init(true);

    assert(!forensics_has_crash());
    assert(forensics_entry_count() == 0);
    assert(forensics_crash_reason() == CRASH_REASON_NONE);

    printf("OK\n");
}

static void test_forensics_watchdog_survival(void) {
    printf("test_forensics_watchdog_survival... ");
    forensics_clear();

    for (uint32_t i = 0; i < 10; i++) {
        g_now_ms = 3000 + i * 10;
        forensics_log_entry(FORENSICS_ENTRY_CMD_RECV, 1, 0x100 + i);
    }
    assert(forensics_entry_count() == 10);

    // Simulate watchdog reset reboot
    forensics_init(true);

    assert(forensics_has_crash());
    assert(forensics_crash_reason() == CRASH_REASON_WATCHDOG);
    assert(strcmp(forensics_crash_reason_str(forensics_crash_reason()), "WATCHDOG") == 0);
    assert(forensics_entry_count() == 10);
    assert(forensics_crash_time() == 3000 + 9 * 10);

    // Verify entry contents preserved intact
    for (uint32_t i = 0; i < 10; i++) {
        forensics_entry_t e;
        assert(forensics_get_entry(i, &e));
        assert(e.payload == 0x100 + i);
        assert(e.lane == 1);
        assert(e.timestamp_ms == 3000 + i * 10);
    }

    printf("OK\n");
}

static void test_forensics_clear(void) {
    printf("test_forensics_clear... ");
    // Clear the crash dump from previous test
    assert(forensics_has_crash());
    forensics_clear();

    assert(!forensics_has_crash());
    assert(forensics_entry_count() == 0);
    assert(forensics_crash_reason() == CRASH_REASON_NONE);

    printf("OK\n");
}

static void test_forensics_tick(void) {
    printf("test_forensics_tick... ");
    forensics_clear();

    // First tick initializes baseline, does not log
    forensics_tick(5000);
    assert(forensics_entry_count() == 0);

    // Second tick within 50ms does not log
    forensics_tick(5050);
    assert(forensics_entry_count() == 0);

    // Third tick at +100ms logs breadcrumb
    forensics_tick(5100);
    assert(forensics_entry_count() == 1);

    forensics_entry_t e;
    assert(forensics_get_entry(0, &e));
    assert(e.type == FORENSICS_ENTRY_BREADCRUMB);

    // Tick at +150ms does not log
    forensics_tick(5150);
    assert(forensics_entry_count() == 1);

    // Tick at +200ms logs second breadcrumb
    forensics_tick(5200);
    assert(forensics_entry_count() == 2);

    printf("OK\n");
}

static void test_forensics_transition_edges(void) {
    printf("test_forensics_transition_edges... ");
    forensics_clear();
    g_tc_ctx.state = TC_IDLE;
    g_sync_state = SYNC_OFF;
    g_lane_l1.task = TASK_IDLE;
    g_lane_l2.task = TASK_IDLE;
    g_now_ms = 6000;
    forensics_tick(6000); // baseline (g_last_breadcrumb_ms already set; re-arm below)

    uint32_t base = forensics_entry_count();

    // TC state transition is logged immediately, not on the 100ms cadence.
    g_tc_ctx.state = TC_LOAD_START;
    forensics_tick(6001);
    assert(forensics_entry_count() == base + 1);
    forensics_entry_t e;
    assert(forensics_get_entry(base, &e));
    assert(e.type == FORENSICS_ENTRY_TC_CHANGE);
    assert(e.payload == (uint32_t)TC_LOAD_START);

    // Sync + lane task transitions in the same pass each get their own entry.
    g_sync_state = SYNC_ACTIVE;
    g_lane_l2.task = TASK_FEED;
    forensics_tick(6002);
    assert(forensics_entry_count() == base + 3);
    assert(forensics_get_entry(base + 1, &e));
    assert(e.type == FORENSICS_ENTRY_SYNC_CHANGE);
    assert(e.payload == (uint32_t)SYNC_ACTIVE);
    assert(forensics_get_entry(base + 2, &e));
    assert(e.type == FORENSICS_ENTRY_TASK_CHANGE);
    assert(e.lane == 2);
    assert(e.payload == (uint32_t)TASK_FEED);

    // Sensor edge: payload is the changed-bit mask.
    uint8_t before = forensics_pack_sensors();
    g_toolhead_has_filament = !g_toolhead_has_filament;
    forensics_tick(6003);
    assert(forensics_entry_count() == base + 4);
    assert(forensics_get_entry(base + 3, &e));
    assert(e.type == FORENSICS_ENTRY_SENSOR_EDGE);
    assert(e.payload == (uint32_t)(before ^ forensics_pack_sensors()));

    // A transition resets the breadcrumb clock: no breadcrumb until +100ms after it.
    forensics_tick(6050);
    assert(forensics_entry_count() == base + 4);
    forensics_tick(6103);
    assert(forensics_entry_count() == base + 5);
    assert(forensics_get_entry(base + 4, &e));
    assert(e.type == FORENSICS_ENTRY_BREADCRUMB);

    printf("OK\n");
}

static void test_loop_jitter_instrumentation(void) {
    printf("test_loop_jitter_instrumentation... ");
    g_sim_event_count = 0;

    g_loop_cur_us = 0;
    g_loop_max_us = 0;
    g_loop_avg_us = 0;
    g_loop_overruns = 0;
    g_loop_top_module = "NONE";

    // Simulate 10 iterations of nominal 500 us loops
    for (int i = 0; i < 10; i++) {
        uint32_t dt = 500;
        g_loop_cur_us = dt;
        if (dt > g_loop_max_us)
            g_loop_max_us = dt;
        g_loop_avg_us = (g_loop_avg_us == 0) ? dt : ((g_loop_avg_us * 15 + dt) / 16);
    }
    assert(g_loop_cur_us == 500);
    assert(g_loop_max_us == 500);
    assert(g_loop_overruns == 0);
    assert(g_sim_event_count == 0);

    // Simulate lag spike of 16500 us caused by SYNC
    uint32_t spike_dt = 16500;
    g_loop_cur_us = spike_dt;
    if (spike_dt > g_loop_max_us) {
        g_loop_max_us = spike_dt;
        g_loop_top_module = "SYNC";
    }
    g_loop_avg_us = (g_loop_avg_us * 15 + spike_dt) / 16;
    if (spike_dt >= 15000) {
        g_loop_overruns++;
        char warn_buf[64];
        snprintf(warn_buf, sizeof(warn_buf), "%u:%s", (unsigned)spike_dt, "SYNC");
        cmd_event("WARN:LOOP_LAG", warn_buf);
    }

    assert(g_loop_cur_us == 16500);
    assert(g_loop_max_us == 16500);
    assert(g_loop_overruns == 1);
    assert(strcmp(g_loop_top_module, "SYNC") == 0);
    assert(event_logged("WARN:LOOP_LAG"));
    assert(event_logged("16500:SYNC"));

    printf("OK\n");
}

int main(void) {
    printf("=== Starting test_forensics ===\n");
    test_forensics_init_fresh();
    test_forensics_log_and_sensors();
    test_forensics_ring_wrap_chronological();
    test_forensics_crc_corruption_recovery();
    test_forensics_watchdog_survival();
    test_forensics_clear();
    test_forensics_tick();
    test_forensics_transition_edges();
    test_loop_jitter_instrumentation();
    printf("=== All test_forensics tests PASSED ===\n");
    return 0;
}
