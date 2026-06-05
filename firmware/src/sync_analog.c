#include "config.h"
#include "controller_shared.h"
#include "motion.h"
#include "sync_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

float g_psf_target_filt = 0.0f;
float sync_reserve_integral_mm = 0.0f;
float g_buf_pos_sigma_accum_mm = 0.0f;
float g_buf_sigma_mm = 0.0f;
uint32_t g_buf_est_low_cf_emit_ms = 0;
uint32_t g_buf_tension_dwell_warn_emit_ms = 0;
int g_neutral_creep_sps = 0;
uint32_t g_neutral_creep_last_tension_ms = 0;
bool g_buf_est_fallback_emitted = false;

int psf_control_law(float error_norm) {
    int max_sps = sync_clamp_max_sps(SYNC_MAX_SPS);
    int kp_window = sync_effective_kp_sps(g_buf.state);

    /* Convention: g_buf_pos +compression / -tension. error_norm = pos - goal,
       so +error = compression-side (overfed) → must FEED LESS, -error = tension
       (starved) → FEED MORE. Negate so the proportional correction drives toward
       goal instead of away (positive feedback). Same for the velocity damping. */
    float p_err = -error_norm;
    if (fabsf(p_err) < CONF_PSF_CTRL_DEADBAND) {
        p_err = 0.0f;
    }

    int ff = (int)extruder_est_sps;
    int p = (int)(p_err * (float)kp_window);
    int d = (int)(-g_vel_norm_f * KD_PSF);

    int target = ff + p + d;

    /* Layer 2 Soft Walls */
    float pos_norm = buf_pos_norm();
    float abs_pos = fabsf(pos_norm);
    if (abs_pos > CONF_PSF_SOFT_WALL_START) {
        float wall = (abs_pos - CONF_PSF_SOFT_WALL_START) / (1.0f - CONF_PSF_SOFT_WALL_START);
        if (wall > 1.0f)
            wall = 1.0f;
        if (wall < 0.0f)
            wall = 0.0f;
        if (pos_norm > 0.0f) {
            /* COMPRESSION (+): blend toward 0 SPS to prevent overfeed */
            target = (int)((float)target * (1.0f - wall));
        } else {
            /* TENSION (-): blend toward max_sps to urge refill */
            target = (int)((float)target + (float)(max_sps - target) * wall);
        }
    }

    return clamp_i(target, 0, max_sps);
}

float sync_compression_wall_velocity_mm_s(lane_t *lane) {
    if (!lane || g_buf_signal.kind != BUF_SRC_VIRTUAL_ENDSTOP)
        return 0.0f;
    float toward_compression = lane_motion_mm_s(lane) - extruder_motion_mm_s(lane);
    return toward_compression > 0.0f ? toward_compression : 0.0f;
}

float sync_compression_wall_time_ms(lane_t *lane) {
    float toward_compression = sync_compression_wall_velocity_mm_s(lane);
    if (!lane || g_buf_signal.kind != BUF_SRC_VIRTUAL_ENDSTOP || toward_compression < 0.05f)
        return 1000000000.0f;
    return (sync_compression_wall_remaining_mm() / toward_compression) * 1000.0f;
}

float sync_reserve_error_mm(void) {
    return g_buf_pos - buf_target_reserve_mm();
}

float sync_reserve_target_mm(void) {
    return buf_target_reserve_mm();
}

float sync_reserve_deadband_mm(void) {
    return buf_virtual_deadband_mm();
}

float sync_reserve_integral_get_mm(void) {
    return sync_reserve_integral_mm;
}

float sync_buf_sigma_mm(void) {
    return g_buf_sigma_mm;
}

float sync_bp_residual_last_mm(void) {
    return g_bp_residual_last_mm;
}

float sync_bp_drift_ewma_mm(void) {
    return g_bp_drift_ewma_mm;
}

int sync_bp_drift_samples(void) {
    return (int)g_bp_drift_samples;
}

int sync_tension_pin_window_count(uint32_t now_ms) {
    uint32_t window_ms = (TENSION_RISK_WINDOW_MS > 0) ? (uint32_t)TENSION_RISK_WINDOW_MS : 60000u;
    int count = 0;
    for (int i = 0; i < TENSION_PIN_WINDOW_LEN; i++) {
        if (g_tension_pin_ts[i] != 0 && (now_ms - g_tension_pin_ts[i]) < window_ms) {
            count++;
        }
    }
    return count;
}

float sync_bp_drift_correction_applied_mm(void) {
    return g_bp_drift_correction_applied_mm;
}

int sync_neutral_creep_sps(void) {
    return g_neutral_creep_sps;
}
