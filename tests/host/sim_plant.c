#include "sim_plant.h"

#include "config.h"
#include "controller_shared.h"
#include "sync_internal.h"

#include "hardware/gpio.h"

#include "sim_fakes.h"

void sim_plant_init(sim_plant_t *p, bool stress_enabled, float stress_lag_ms) {
    p->slack_mm = 0.0f;
    p->sat_compression = false;
    p->sat_tension = false;
    p->stress_enabled = stress_enabled;
    p->stress_lag_ms = stress_lag_ms;
    p->achieved_feed_sps = 0.0f;
    p->lagged_feed_mm_s = 0.0f;
}

// Type-P: invert sync_buf.c's buf_analog_update() forward transform so the
// raw ADC counts this tick produce ~`norm` after that transform's own EMA
// (which the sim intentionally does not bypass — see design.md "Sensor
// injection points"). g_sim_adc_counts is read 4 times per real sample with
// no state change between reads, so counts == fraction*4095 exactly (no
// >>2 rounding loss) — see sim_fakes.c ADC section.
static void emit_type_p(float norm) {
    bool reversed = (g_buf_psf_max_comp < g_buf_psf_max_tens);
    float d_comp = reversed ? (g_buf_psf_neutral - g_buf_psf_max_comp)
                             : (g_buf_psf_max_comp - g_buf_psf_neutral);
    float d_tens = reversed ? (g_buf_psf_max_tens - g_buf_psf_neutral)
                             : (g_buf_psf_neutral - g_buf_psf_max_tens);
    if (d_comp < 0.001f)
        d_comp = 0.001f;
    if (d_tens < 0.001f)
        d_tens = 0.001f;

    float span = (norm >= 0.0f) ? d_comp : d_tens;
    float sign = reversed ? -1.0f : 1.0f;
    float fraction = g_buf_psf_neutral + sign * norm * span;
    if (fraction < 0.0f)
        fraction = 0.0f;
    if (fraction > 1.0f)
        fraction = 1.0f;

    int counts = (int)(fraction * 4095.0f + 0.5f);
    if (counts < 0)
        counts = 0;
    if (counts > 4095)
        counts = 4095;
    g_sim_adc_counts = (uint16_t)counts;
}

static void emit_type_d(float slack_mm, const sim_scenario_t *scn, uint32_t t_ms) {
    float threshold = buf_threshold_mm();
    bool tension = slack_mm <= -threshold;
    bool compression = slack_mm >= threshold;

    for (int i = 0; i < scn->sensor_force.count; i++) {
        const sensor_force_event_t *ev = &scn->sensor_force.ev[i];
        if (ev->kind == FORCE_BOTH && t_ms >= ev->t_ms) {
            tension = true;
            compression = true;
        } else if (ev->kind == FORCE_STUCK && t_ms >= ev->t_ms) {
            if (ev->target == SENSOR_TARGET_TENSION)
                tension = ev->value;
            else
                compression = ev->value;
        } else if (ev->kind == FORCE_CHATTER && t_ms == ev->t_ms) {
            if (ev->target == SENSOR_TARGET_TENSION)
                tension = ev->value;
            else
                compression = ev->value;
        }
    }

    g_buf_tension_din.stable = tension;
    g_buf_compression_din.stable = compression;
}

void sim_plant_tick(sim_plant_t *p, const sim_scenario_t *scn, uint32_t t_ms, uint32_t dt_ms,
                    int sensor_type) {
    float dt_s = (float)dt_ms / 1000.0f;

    // Sum both lanes' commanded feed in mm/s, converting each with its own
    // gearing, rather than trusting the scenario's initial active_lane: RELOAD
    // scenarios (audit-reliability-fixes H4/H5/H6) call set_active_lane() mid-run
    // to swap which physical lane drives the shared buffer, and only one lane is
    // ever actually feeding (task != TASK_IDLE) at a time, so the sum is exact.
    // feed_gain is applied per-lane, only to the scenario's originally-active
    // lane: it models that lane's own jam (e.g. a runout), not a global fault —
    // a freshly RELOAD-swapped-in lane is a different physical reel and must
    // not inherit the original lane's fault. Found by running a RELOAD
    // scenario against real toolchange.c/sync.c: applying feed_gain to the
    // summed total silently zeroed the new lane's approach feed too.
    float commanded_mm_s = 0.0f;
    {
        lane_t *lanes[2] = {&g_lane_l1, &g_lane_l2};
        float feed_gain_now = gain_schedule_eval(&scn->feed_gain, t_ms);
        for (int i = 0; i < 2; i++) {
            lane_t *lane = lanes[i];
            float mm_s;
            if (lane->task != TASK_IDLE) {
                mm_s = (float)lane->current_sps * g_mm_per_step[i];
                if (!lane->task_forward)
                    mm_s = -mm_s;
            } else {
                // BL prime/lock/catch (sync.c) drives motor_set_rate_sps()/
                // motor_set_dir() directly and never sets lane->task away from
                // TASK_IDLE — decode the commanded rate from the PWM fake
                // instead. See sim_fakes.c sim_motor_rate_sps() and
                // memories/repo/host-sync-sim.md.
                float sps = sim_motor_rate_sps(lane->m.slice);
                mm_s = sps * g_mm_per_step[i];
                if (!gpio_get(lane->m.dir_pin))
                    mm_s = -mm_s;
            }
            if ((i + 1) == scn->active_lane)
                mm_s *= feed_gain_now;
            commanded_mm_s += mm_s;
        }
    }

    float feed_mm_s;
    if (p->stress_enabled) {
        // Stress mode: motor slew ceiling (independent of firmware's own
        // command ramp — see design.md "Slew ceiling has provenance the lag
        // lacks") then a first-order transport-lag filter, additive to the
        // sensing EWMA already running inside sync_buf.c:413. Slew ceiling
        // converted to mm/s via lane 0's gearing — both lanes share nominally
        // identical hardware, and this is a robustness margin, not a fidelity
        // claim (design.md "Optional Robustness Stress Mode").
        float max_step_mm_s =
            (float)g_ramp_step_sps * g_mm_per_step[0] * ((float)dt_ms / (float)g_ramp_tick_ms);
        float delta = commanded_mm_s - p->achieved_feed_sps;
        if (delta > max_step_mm_s)
            delta = max_step_mm_s;
        if (delta < -max_step_mm_s)
            delta = -max_step_mm_s;
        p->achieved_feed_sps += delta; // mm/s despite the field name (stress shadow state)

        if (p->stress_lag_ms > 0.0f) {
            float tau_s = p->stress_lag_ms / 1000.0f;
            float k = dt_s / (tau_s + dt_s);
            p->lagged_feed_mm_s += (p->achieved_feed_sps - p->lagged_feed_mm_s) * k;
        } else {
            p->lagged_feed_mm_s = p->achieved_feed_sps;
        }
        feed_mm_s = p->lagged_feed_mm_s;
    } else {
        feed_mm_s = commanded_mm_s;
    }

    float demand_raw = demand_profile_eval(&scn->demand, t_ms);
    float demand_mm_s;
    if (demand_raw >= 0.0f)
        demand_mm_s = demand_raw * gain_schedule_eval(&scn->demand_gain, t_ms);
    else
        demand_mm_s = demand_raw * gain_schedule_eval(&scn->retract_gain, t_ms);

    p->last_feed_mm_s = feed_mm_s;
    p->last_demand_mm_s = demand_mm_s;
    p->slack_mm += (feed_mm_s - demand_mm_s) * dt_s;

    float half_travel = buf_physical_half_travel_mm();
    p->sat_compression = false;
    p->sat_tension = false;
    if (p->slack_mm > half_travel) {
        p->slack_mm = half_travel;
        p->sat_compression = true;
    } else if (p->slack_mm < -half_travel) {
        p->slack_mm = -half_travel;
        p->sat_tension = true;
    }

    if (sensor_type == BUF_SENSOR_TYPE_P) {
        float norm = p->slack_mm / half_travel;
        if (norm > 1.0f)
            norm = 1.0f;
        if (norm < -1.0f)
            norm = -1.0f;
        emit_type_p(norm);
    } else {
        emit_type_d(p->slack_mm, scn, t_ms);
    }
}
