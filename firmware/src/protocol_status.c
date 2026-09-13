/// @file protocol_status.c
/// @brief Status-dump formatting: builds the single-line ST: telemetry response
///        (lane state, buffer, sync, faults) consumed by host tooling.
/// @details Read-only snapshot of runtime state; no actuation. Field set must stay
///          in sync with MANUAL.md and scripts/flare_cmd.py --dump.

#include "controller_shared.h"
#include "protocol.h"
#include "protocol_internal.h"
#include "sync.h"
#include "toolchange.h"
#include <stdio.h>
#include <string.h>

enum {
    STATUS_LINE_MAX = CMD_LINE_MAX - 8,
    BIAS_MILLI_PER_PERCENT = 10,
    BIAS_MILLI_ROUND_TO_PERCENT = 5,
};

static const float BUF_PHYSICAL_THRESHOLD_MM = 0.1f;

static const char *buf_status_label(void) {
    /* Type-P: report the PHYSICAL buffer state (fixed thresholds about the
       mechanical centre), not the control classification — the latter is
       centred on the active goal (which the BL override can park at a rail),
       so it would otherwise mislabel a physically-tensioned buffer. Type-D
       uses switches, already physical. */
    if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
        buf_state_t phys = (g_buf_pos < -BUF_PHYSICAL_THRESHOLD_MM)  ? BUF_TENSION
                           : (g_buf_pos > BUF_PHYSICAL_THRESHOLD_MM) ? BUF_COMPRESSION
                                                                     : BUF_NEUTRAL;
        return buf_state_name(phys);
    }
    return buf_state_name(g_buf.state);
}

void cmd_handle_status_dump(void) {
    int idx = (g_active_lane == 2) ? 1 : 0;
    flow_param_t active_flow_param = flow_param((int)g_extruder_est_sps);

    char b[STATUS_LINE_MAX];
    int blen = snprintf(
        b, sizeof(b),
        "LN:%d,TC:%s,L1T:%s,L2T:%s,"
        "I1:%d,O1:%d,I2:%d,O2:%d,"
        "TH:%d,YS:%d,BUF:%s,MM:%.1f,BF:%.1f,BP:%.2f,SM:%d,BL:%s,ST:%d,TPR:%d,CU:%d,RELOAD:%d,UC:%d,"
        "BST:%d,BY:%d,TMC:%d%d,"
        "EST:%.1f,RE:%.2f,AV:%.2f,SC:%.1f",
        g_active_lane, tc_state_name(g_tc_ctx.state), task_name(g_lane_l1.task),
        task_name(g_lane_l2.task), lane_in_present(&g_lane_l1) ? 1 : 0,
        lane_out_present(&g_lane_l1) ? 1 : 0, lane_in_present(&g_lane_l2) ? 1 : 0,
        lane_out_present(&g_lane_l2) ? 1 : 0, g_toolhead_has_filament ? 1 : 0,
        on_al(&g_y_split) ? 1 : 0, buf_status_label(), (double)sps_to_mm_per_min(g_sync_current_sps),
        (double)sps_to_mm_per_min(active_flow_param.baseline_sps), (double)g_buf_pos,
        sync_enabled ? 1 : 0, sync_buffer_lock_arm_str(), (int)g_sync_state, g_auto_preload ? 1 : 0,
        g_enable_cutter ? 1 : 0, g_reload_mode, g_unload_cut ? 1 : 0, g_buf_sensor_type,
        g_bypass ? 1 : 0, g_tmc_health[0], g_tmc_health[1],
        (double)sps_to_mm_per_min((int)g_extruder_est_sps), (double)sync_reserve_error_mm(),
        (double)g_buf.arm_vel_mm_s, (double)sps_to_mm_per_min_idx(g_tmc_stealthchop_sps[idx], idx));

    if (blen > 0 && blen < (int)sizeof(b)) {
        uint32_t now_ms = g_now_ms;
        uint32_t ad_ms = sync_tension_dwell_ms(now_ms);
        uint32_t td_ms = (g_buf.state == BUF_COMPRESSION && g_buf.entered_ms > 0)
                             ? (now_ms - g_buf.entered_ms)
                             : 0;
        /* Phase 13 Plan 02 Task 2 (D-11): TM:/ARM: appended next to TT:. TM:
           is what the TRIP sees (0 while unarmed/held), not the raw
           accumulator already on the wire as SYNC_REFILL_MM: -- rendered
           %d, same int-truncation convention as that sibling field (both
           read the same underlying float). ARM: is rendered %c, not %d: it
           can only ever be the single character '0' or '1' by construction
           (the ternary below), so the worst-case-line-budget test can
           charge it a structurally-provable 1 char instead of the blanket
           11-char budget every other %d/%u conversion in this line carries
           -- the line does not fit STATUS_LINE_MAX at the blanket rate; see
           scripts/test_status_line_budget.py. Capture the return value: a
           truncated tail silently drops every field after the cut
           (13-RESEARCH.md Pitfall 3) with no compile/runtime error
           otherwise, so latch one ST_TRUNC event on overflow (guarded so it
           cannot repeat every poll). */
        int tail_len = snprintf(
            b + blen, sizeof(b) - (size_t)blen,
            ",RT:%.2f,TT:%u,TM:%d,ARM:%c,CT:%u,SK:%u,CF:%.2f,ES:%.2f"
            ",TPX:%d,CB:%d,BPV:%d,MK:%u:%s"
            ",SYNC_REFILL_MM:%d,SYNC_RELIEVE_MM:%d,TF:%.1f,FL_RATE:%.1f,UL_RATE:%.1f",
            (double)sync_reserve_target_mm(), (unsigned)ad_ms, (int)sync_tension_stop_trip_mm(),
            g_sync_trip_armed ? '1' : '0', (unsigned)td_ms, (unsigned)g_buf_signal.kind,
            (double)g_buf_signal.confidence, (double)sync_buf_sigma_mm(),
            sync_tension_pin_window_count(now_ms),
            (active_flow_param.bias_milli + BIAS_MILLI_ROUND_TO_PERCENT) /
                BIAS_MILLI_PER_PERCENT,
            (int)(g_buf_pos * 100.0f), g_marker_seq, g_marker_tag,
            (int)g_sync_refill_effort_mm, (int)g_sync_relieve_effort_mm,
            (double)g_sync_mmu_total_mm, (double)sps_to_mm_per_min_idx(g_feed_sps, idx),
            (double)sps_to_mm_per_min_idx(g_rev_sps, idx));
        if (tail_len < 0 || tail_len >= (int)(sizeof(b) - (size_t)blen)) {
            static bool st_trunc_latched = false;
            if (!st_trunc_latched) {
                st_trunc_latched = true;
                cmd_event("SYS", "ST_TRUNC");
            }
        }
    }

    cmd_reply("OK", b);
}
