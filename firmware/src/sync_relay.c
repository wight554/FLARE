/// @file sync_relay.c
/// @brief Type-D (dual-endstop) feed law: a two-level/hysteretic relay controller
///        matched to the COMPRESSION/TENSION microswitches, plus neutral-band feed
///        sampling for the demand estimator.
/// @details A continuous PI limit-cycles here because there is no feedback between
///          crossings; the relay law is the correct controller. Polarity: TENSION =
///          starved -> feed fast; COMPRESSION = reserve -> feed slow. See spec type-d-dynamic-flow.

#include "config.h"
#include "controller_shared.h"
#include "motion.h"
#include "sync_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint16_t TYPE_D_SAMPLE_ROLLOVER_COUNT = 10000u;
static const uint32_t TYPE_D_SAMPLE_DECAY_DIVISOR = 2u;
static const float RELAY_TRIM_LEAK_MIN_SPS_S = 25.0f;
static const float MS_PER_SECOND_F = 1000.0f;

float g_tension_floor_sps = 0.0f;
float g_relay_flip_travel_since_mm = 0.0f;
float g_relay_neutral_trim_sps = 0.0f;
uint32_t g_relay_trim_last_leak_ms = 0;
uint32_t g_type_d_neutral_feed_sps_sum = 0;
uint16_t g_type_d_neutral_feed_samples = 0;
uint32_t g_type_d_neutral_flat_feed_sps_sum = 0;
uint16_t g_type_d_neutral_flat_feed_samples = 0;

void relay_on_transition(buf_state_t new_state, uint32_t now_ms) {
    (void)new_state;
    (void)now_ms;
    if (BUF_SENSOR_TYPE != BUF_SENSOR_TYPE_D)
        return;
    g_relay_flip_travel_since_mm = 0.0f;
}

void type_d_neutral_feed_reset(void) {
    g_type_d_neutral_feed_sps_sum = 0;
    g_type_d_neutral_feed_samples = 0;
    g_type_d_neutral_flat_feed_sps_sum = 0;
    g_type_d_neutral_flat_feed_samples = 0;
}

void type_d_neutral_feed_sample(buf_state_t s, float pos_norm, float target_norm,
                                float deadband_norm) {
    if (BUF_SENSOR_TYPE != BUF_SENSOR_TYPE_D || s != BUF_NEUTRAL || g_sync_state != SYNC_ACTIVE)
        return;
    if (sync_current_sps <= 0)
        return;
    if (g_type_d_neutral_feed_samples >= TYPE_D_SAMPLE_ROLLOVER_COUNT) {
        g_type_d_neutral_feed_sps_sum /= TYPE_D_SAMPLE_DECAY_DIVISOR;
        g_type_d_neutral_feed_samples /= TYPE_D_SAMPLE_DECAY_DIVISOR;
    }
    g_type_d_neutral_feed_sps_sum += (uint32_t)sync_current_sps;
    g_type_d_neutral_feed_samples++;

    if (pos_norm <= (target_norm + deadband_norm)) {
        if (g_type_d_neutral_flat_feed_samples >= TYPE_D_SAMPLE_ROLLOVER_COUNT) {
            g_type_d_neutral_flat_feed_sps_sum /= TYPE_D_SAMPLE_DECAY_DIVISOR;
            g_type_d_neutral_flat_feed_samples /= TYPE_D_SAMPLE_DECAY_DIVISOR;
        }
        g_type_d_neutral_flat_feed_sps_sum += (uint32_t)sync_current_sps;
        g_type_d_neutral_flat_feed_samples++;
    }
}

bool type_d_neutral_feed_avg_sps(float *avg_sps) {
    if (!avg_sps || g_type_d_neutral_feed_samples == 0)
        return false;
    if (g_type_d_neutral_flat_feed_samples >= SYNC_NEUTRAL_FILL_MIN_FEED_SAMPLES) {
        *avg_sps =
            (float)g_type_d_neutral_flat_feed_sps_sum / (float)g_type_d_neutral_flat_feed_samples;
    } else {
        *avg_sps = (float)g_type_d_neutral_feed_sps_sum / (float)g_type_d_neutral_feed_samples;
    }
    return *avg_sps > 0.0f;
}

bool type_d_sample_demand_bounds(float *demand_sps) {
    if (!demand_sps)
        return false;
    int floor_sps = SYNC_MIN_SPS;
    int cap_sps = baseline_control_floor_sps();
    if (cap_sps < floor_sps)
        cap_sps = floor_sps;
    if (*demand_sps <= 0.0f)
        return false;
    *demand_sps = clamp_f(*demand_sps, (float)floor_sps, (float)cap_sps);
    return true;
}

void relay_neutral_trim_clamp(void) {
    if (SYNC_RELAY_TRIM_CLAMP_SPS <= 0) {
        g_relay_neutral_trim_sps = 0.0f;
        return;
    }
    g_relay_neutral_trim_sps = clamp_f(g_relay_neutral_trim_sps, -(float)SYNC_RELAY_TRIM_CLAMP_SPS,
                                       (float)SYNC_RELAY_TRIM_CLAMP_SPS);
}

void relay_neutral_trim_leak(buf_state_t s, uint32_t now_ms) {
    if (BUF_SENSOR_TYPE != BUF_SENSOR_TYPE_D || s != BUF_NEUTRAL || g_sync_state != SYNC_ACTIVE ||
        g_relay_neutral_trim_sps == 0.0f) {
        g_relay_trim_last_leak_ms = now_ms;
        return;
    }
    if ((now_ms - g_buf.entered_ms) < SYNC_RELAY_TRIM_LEAK_DELAY_MS) {
        g_relay_trim_last_leak_ms = now_ms;
        return;
    }
    if (g_relay_trim_last_leak_ms == 0) {
        g_relay_trim_last_leak_ms = now_ms;
        return;
    }

    uint32_t dt_ms = now_ms - g_relay_trim_last_leak_ms;
    g_relay_trim_last_leak_ms = now_ms;
    if (dt_ms == 0)
        return;

    float leak_rate_sps_s = fmaxf((float)SYNC_RELAY_TRIM_STEP_SPS * SYNC_RELAY_TRIM_LEAK_RATE_FRAC,
                                  RELAY_TRIM_LEAK_MIN_SPS_S);
    float leak_sps = leak_rate_sps_s * ((float)dt_ms / MS_PER_SECOND_F);
    if (g_relay_neutral_trim_sps > 0.0f) {
        g_relay_neutral_trim_sps =
            (g_relay_neutral_trim_sps > leak_sps) ? (g_relay_neutral_trim_sps - leak_sps) : 0.0f;
    } else {
        g_relay_neutral_trim_sps =
            (-g_relay_neutral_trim_sps > leak_sps) ? (g_relay_neutral_trim_sps + leak_sps) : 0.0f;
    }
}

int relay_control_law(buf_state_t s) {
    int relay_base = baseline_control_floor_sps();
    if (s == BUF_TENSION) {
        return (int)((float)relay_base * RELAY_CATCHUP_FRAC);
    } else if (s == BUF_COMPRESSION) {
        return 0;
    } else {
        int demand_sps = (int)extruder_est_sps;
        int neutral = (int)((float)demand_sps * RELAY_NEUTRAL_FRAC + g_relay_neutral_trim_sps);
        if (neutral < SYNC_MIN_SPS)
            neutral = SYNC_MIN_SPS;
        if (neutral > relay_base)
            neutral = relay_base;
        return neutral;
    }
}
