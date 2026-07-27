#pragma once
/// @brief Kinematic buffer plant: single integrated slack_mm state, sign
///        convention matching g_buf_pos (+compression, -tension). See
///        design.md "Plant Model".

#include <stdbool.h>
#include <stdint.h>
#include "sim_scenario.h"

typedef struct {
    float slack_mm;
    bool sat_compression; // latched true once the compression rail is touched this tick
    bool sat_tension;
    bool stress_enabled;
    float stress_lag_ms;     // valid only when stress_enabled; 0 = slew ceiling only
    float achieved_feed_sps; // stress-mode motor slew shadow state
    float lagged_feed_mm_s;  // stress-mode transport-lag filter state
    float last_feed_mm_s;    // for trace/CSV
    float last_demand_mm_s;
} sim_plant_t;

void sim_plant_init(sim_plant_t *p, bool stress_enabled, float stress_lag_ms);

// Advances the plant by dt_ms: reads commanded feed from the active lane
// (real firmware state, not a fake), applies the scenario's demand profile
// and fault gains, integrates slack, clamps to the physical half-travel, and
// writes the sensor signal for `sensor_type` (BUF_SENSOR_TYPE_D/_P).
void sim_plant_tick(sim_plant_t *p, const sim_scenario_t *scn, uint32_t t_ms, uint32_t dt_ms,
                    int sensor_type);
