#include "controller_shared.h"
#include "protocol.h"
#include "protocol_internal.h"
#include "sync.h"
#include "toolchange.h"
#include <stdio.h>
#include <string.h>

static const char *buf_status_label(void) {
    /* Type-P: report the PHYSICAL buffer state (fixed thresholds about the
       mechanical centre), not the control classification — the latter is
       centred on the active goal (which the BL override can park at a rail),
       so it would otherwise mislabel a physically-tensioned buffer. Type-D
       uses switches, already physical. */
    if (BUF_SENSOR_TYPE == 1) {
        buf_state_t phys = (g_buf_pos < -0.1f)  ? BUF_TENSION
                           : (g_buf_pos > 0.1f) ? BUF_COMPRESSION
                                                : BUF_NEUTRAL;
        return buf_state_name(phys);
    }
    return buf_state_name(g_buf.state);
}

void cmd_handle_status_dump(void) {
    int idx = (active_lane == 2) ? 1 : 0;
    flow_param_t active_flow_param = flow_param((int)extruder_est_sps);

    char b[768];
    int blen = snprintf(
        b, sizeof(b),
        "LN:%d,TC:%s,L1T:%s,L2T:%s,"
        "I1:%d,O1:%d,I2:%d,O2:%d,"
        "TH:%d,YS:%d,BUF:%s,MM:%.1f,BF:%.1f,BP:%.2f,SM:%d,BL:%s,ST:%d,TPR:%d,CU:%d,RELOAD:%d,UC:%d,"
        "BST:%d,"
        "EST:%.1f,RE:%.2f,AV:%.2f,SC:%.1f",
        active_lane, tc_state_name(g_tc_ctx.state), task_name(g_lane_l1.task),
        task_name(g_lane_l2.task), lane_in_present(&g_lane_l1) ? 1 : 0,
        lane_out_present(&g_lane_l1) ? 1 : 0, lane_in_present(&g_lane_l2) ? 1 : 0,
        lane_out_present(&g_lane_l2) ? 1 : 0, toolhead_has_filament ? 1 : 0,
        on_al(&g_y_split) ? 1 : 0, buf_status_label(), (double)sps_to_mm_per_min(sync_current_sps),
        (double)sps_to_mm_per_min(active_flow_param.baseline_sps), (double)g_buf_pos,
        sync_enabled ? 1 : 0, sync_buffer_lock_arm_str(), (int)g_sync_state, AUTO_PRELOAD ? 1 : 0,
        ENABLE_CUTTER ? 1 : 0, RELOAD_MODE, UNLOAD_CUT ? 1 : 0, BUF_SENSOR_TYPE,
        (double)sps_to_mm_per_min((int)extruder_est_sps), (double)sync_reserve_error_mm(),
        (double)g_buf.arm_vel_mm_s, (double)sps_to_mm_per_min_idx(TMC_STEALTHCHOP_SPS[idx], idx));

    if (blen > 0 && blen < (int)sizeof(b)) {
        uint32_t now_ms = g_now_ms;
        uint32_t ad_ms = sync_tension_dwell_ms(now_ms);
        uint32_t td_ms = (g_buf.state == BUF_COMPRESSION && g_buf.entered_ms > 0)
                             ? (now_ms - g_buf.entered_ms)
                             : 0;
        snprintf(b + blen, sizeof(b) - (size_t)blen,
                 ",RT:%.2f,TT:%u,CT:%u,SK:%u,CF:%.2f,ES:%.2f"
                 ",TPX:%d,CB:%d,BPV:%d,MK:%u:%s"
                 ",SYNC_REFILL_MM:%d,SYNC_RELIEVE_MM:%d,TF:%.1f,FL_RATE:%.1f,UL_RATE:%.1f",
                 (double)sync_reserve_target_mm(), (unsigned)ad_ms, (unsigned)td_ms,
                 (unsigned)g_buf_signal.kind, (double)g_buf_signal.confidence,
                 (double)sync_buf_sigma_mm(), sync_tension_pin_window_count(now_ms),
                 (active_flow_param.bias_milli + 5) / 10, (int)(g_buf_pos * 100.0f), g_marker_seq,
                 g_marker_tag, (int)g_sync_refill_effort_mm, (int)g_sync_relieve_effort_mm,
                 (double)g_sync_mmu_total_mm, (double)sps_to_mm_per_min_idx(FEED_SPS, idx),
                 (double)sps_to_mm_per_min_idx(REV_SPS, idx));
    }

    cmd_reply("OK", b);
}
