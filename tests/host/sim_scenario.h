#pragma once
/// @brief Scenario declaration table: demand profile, time-scheduled fault
///        injection, and switch scripting. See design.md "Demand profiles"
///        and "Fault Injection API".

#include <stdbool.h>
#include <stdint.h>

#define SIM_MAX_BREAKPOINTS 8
#define SIM_MAX_SWITCH_EVENTS 8

typedef enum {
    DEMAND_STEADY,      // constant level_mm_s
    DEMAND_STEP_UP,     // level_mm_s until t1_ms, then level2_mm_s
    DEMAND_BURST,       // 0, then level_mm_s during [t1_ms, t2_ms), then 0
    DEMAND_IDLE_ZERO,   // level_mm_s until t1_ms, then 0 (abrupt extruder stop)
    DEMAND_RETRACT,     // 0, then -level_mm_s during [t1_ms, t2_ms), then 0
    DEMAND_LONG_RETRACT, // same shape as DEMAND_RETRACT; scenario picks a
                        // level_mm_s/duration whose integral exceeds half travel
    DEMAND_PAUSE_RESUME // level_mm_s until t1_ms, 0 during [t1_ms, t2_ms)
                        // (print paused/idle), then level2_mm_s from t2_ms
                        // onward (print resumes) — for testing recovery from
                        // a paused sync lifecycle state, not just entry into one
} demand_kind_t;

typedef struct {
    demand_kind_t kind;
    float level_mm_s;
    float level2_mm_s;
    uint32_t t1_ms;
    uint32_t t2_ms;
} demand_profile_t;

typedef struct {
    uint32_t t_ms;
    float value; // 0..1
} gain_bp_t;

typedef struct {
    gain_bp_t bp[SIM_MAX_BREAKPOINTS];
    int count;
} gain_schedule_t;

typedef enum {
    SENSOR_TARGET_TENSION,
    SENSOR_TARGET_COMPRESSION,
} sensor_target_t;

typedef enum {
    FORCE_NONE = 0,
    FORCE_STUCK,   // latch target.stable = value from t_ms onward
    FORCE_CHATTER, // target.stable = value for exactly this one tick
    FORCE_BOTH,    // latch both tension_din and compression_din true from t_ms (type-D only)
} sensor_force_kind_t;

typedef struct {
    uint32_t t_ms;
    sensor_force_kind_t kind;
    sensor_target_t target; // used by STUCK / CHATTER
    bool value;
} sensor_force_event_t;

typedef struct {
    sensor_force_event_t ev[SIM_MAX_BREAKPOINTS];
    int count;
} sensor_force_schedule_t;

typedef enum {
    SWITCH_L1_IN,
    SWITCH_L1_OUT,
    SWITCH_L2_IN,
    SWITCH_L2_OUT,
    SWITCH_Y,
} switch_target_t;

typedef struct {
    uint32_t t_ms;
    switch_target_t target;
    bool value;
} switch_event_t;

typedef struct {
    switch_event_t ev[SIM_MAX_SWITCH_EVENTS];
    int count;
} switch_script_t;

typedef struct {
    const char *name;
    const char *tick_ceiling_reason; // non-NULL required iff tick_ceiling > default
    demand_profile_t demand;
    gain_schedule_t feed_gain;
    gain_schedule_t demand_gain;
    gain_schedule_t retract_gain;
    sensor_force_schedule_t sensor_force;
    switch_script_t switch_script;
    uint32_t tick_ceiling; // 0 => use SIM_DEFAULT_TICK_CEILING
    int active_lane;       // 1 or 2
    bool auto_mode;        // seeds g_auto_mode
    bool start_sync_active; // seeds g_sync_state = SYNC_ACTIVE, g_toolhead_has_filament = true
    bool type_specific;     // true => only run under the sensor type named in `name`

    // RELOAD-path scenario support (audit-reliability-fixes H4/H5/H6 sim coverage).
    bool reload_mode;          // seeds g_reload_mode
    bool force_no_consumer;    // once tc_state() != TC_IDLE, force g_extruder_est_sps
                               // to 0 every tick — models a paused/idle print (no
                               // organic estimator path within one scenario's tick
                               // budget; documented override, not modeled physics)
    uint32_t manual_reload_at_ms; // 0 = never; else call tc_manual_reload() at this tick

    // buffer-state-lock (BL:) scenario support.
    uint32_t bl_arm_at_ms;       // 0 = never; else call sync_buffer_lock_arm() at this tick
    int bl_arm_target;           // buf_state_t: BUF_TENSION(1) or BUF_COMPRESSION(2)
    float bl_arm_follow_mm;      // 0 = no follow-on (BL:T / BL:C); >half-travel arms follow-on
    float bl_arm_follow_rate_mmpm;
    uint32_t bl_clear_at_ms;     // 0 = never; else call sync_retract_assist_set(false) (BS) at this tick

    // cutter-feed-timeout scenario support.
    uint32_t cutter_start_at_ms; // 0 = never; else call cutter_start() at this tick
    bool cutter_enable_feed;
    int cut_feed_mm_override;         // 0 = use settings_defaults(); else overrides g_cut_feed_mm
    int cut_timeout_feed_ms_override; // 0 = default; else overrides g_cut_timeout_feed_ms
    int servo_settle_ms_override;         // 0 = default; else overrides g_servo_settle_ms
    int cut_timeout_settle_ms_override;   // 0 = default; else overrides g_cut_timeout_settle_ms

    // persistence-contract scenario support. When set, sim_main.c calls the
    // real settings_load() instead of settings_defaults() at boot (fresh/
    // erased flash — g_sim_flash starts zeroed every process, matching an
    // invalid-magic fresh board) and emits a PERSIST event reporting whether
    // the flash sector was actually written back.
    bool test_settings_load_fresh_board;

    // toolchange-orchestration scenario support.
    uint32_t tc_start_at_ms; // 0 = never; else call tc_start(tc_target_lane) at this tick
    int tc_target_lane;      // 1 or 2

    // psf-type-p-sensor scenario support.
    uint32_t bs_request_at_ms; // 0 = never; else call buffer_stabilize_request() (BS) at this tick
    uint32_t ul_start_at_ms;   // 0 = never; else call lane_start(..., TASK_UNLOAD, ...) at this
                               // tick (mirrors protocol.c's manual UL command)
    int ul_target_lane;        // 1 or 2
} sim_scenario_t;

extern const sim_scenario_t g_sim_scenarios[];
extern const int g_sim_scenario_count;

const sim_scenario_t *sim_scenario_find(const char *name);

float demand_profile_eval(const demand_profile_t *p, uint32_t t_ms);
float gain_schedule_eval(const gain_schedule_t *s, uint32_t t_ms);
