#pragma once

#include "buf_signal.h"
#include "controller_shared.h"

typedef enum {
    SYNC_OFF = 0,
    SYNC_ACTIVE,
    SYNC_RETRACT_ASSIST,
    SYNC_RELIEF_PAUSE,
    SYNC_FAULT_HOLD
} sync_state_t;

extern sync_state_t g_sync_state;
extern bool g_sync_auto_started;

#define sync_enabled (g_sync_state == SYNC_ACTIVE)
#define sync_guard_active (g_sync_state == SYNC_RETRACT_ASSIST || g_sync_state == SYNC_FAULT_HOLD)

typedef struct {
    int baseline_sps;
    int bias_milli;
} flow_param_t;

void sync_init(uint32_t now_ms);
void sync_set_state(sync_state_t new_state);
void sync_retract_assist_set(bool enabled);
void sync_retract_assist_release(uint32_t now_ms);
void sync_bl_clear_autostart_suppress(void);
bool sync_retract_assist_enabled(void);
void sync_buffer_lock_arm(buf_state_t target, float follow_mm, float follow_rate_mmpm,
                          uint32_t now_ms, uint32_t timeout_ms);
const char *sync_buffer_lock_arm_str(void);
bool sync_buffer_lock_motor_moving(void);
void sync_relief_pause(void);
void sync_fault_hold(void);
void flow_schedule_reset_runtime(void);
void flow_schedule_refresh_scalar(void);
flow_param_t flow_param(int flow_sps);
void buf_analog_update(uint32_t elapsed_ms);

const char *buf_state_name(buf_state_t s);
buf_state_t buf_state_raw(void);
bool buffer_stabilize_request(uint32_t now_ms);
void buffer_stabilize_cancel(void);
void buffer_stabilize_tick(uint32_t now_ms);
int sync_clamp_max_sps(int requested_sps);
void sync_disable(bool reset_estimator);
float sync_compression_wall_velocity_mm_s(lane_t *lane);
float sync_compression_wall_time_ms(lane_t *lane);

void boot_stabilize_start(uint32_t now_ms);
bool boot_stabilize_settled(void);
void buf_sensor_tick(uint32_t now_ms);
void sync_tick(uint32_t now_ms);
float sync_reserve_error_mm(void);
float sync_reserve_target_mm(void);
float sync_reserve_deadband_mm(void);
uint32_t sync_tension_dwell_ms(uint32_t now_ms);
uint32_t sync_est_age_ms(uint32_t now_ms);
bool sync_is_positive_relaunch_damped(void);
bool sync_is_tension_predicted(void);
float sync_reserve_integral_get_mm(void);
float sync_buf_sigma_mm(void);
float sync_bp_residual_last_mm(void);
float sync_bp_drift_ewma_mm(void);
int sync_bp_drift_samples(void);
int sync_tension_pin_window_count(uint32_t now_ms);
float sync_bp_drift_correction_applied_mm(void);

extern float g_sync_refill_effort_mm;
extern float g_sync_relieve_effort_mm;
extern float g_sync_mmu_total_mm;

/* Phase 13 Plan 02 (D-08/D-09/D-10/D-12): armed only after a real buffer-state
   transition is observed while sync has genuinely auto-started; cleared on
   sync_disable()/sync_rearm_active()/the AUTO_START path (mirroring
   g_sync_tension_extreme's reset sites) and on the falling edge of a
   deliberate rail hold (sync_trip_track_hold_edge(), sync.c). Gates both the
   mm distance trip and the pre-existing ms dwell trip so they arm/disarm in
   lockstep and can never drift apart. */
extern bool g_sync_trip_armed;
/* TM: telemetry accessor (protocol_status.c, Task 2) -- reports what the
   TRIP sees: 0 while unarmed or while a deliberate hold is in progress,
   otherwise the accumulated tension travel g_sync_refill_effort_mm holds.
   Deliberately NOT the raw accumulator (already on the wire as
   SYNC_REFILL_MM:) since that keeps counting through a suppressed hold. */
float sync_tension_stop_trip_mm(void);

/* Phase 13 Plan 03 (D-15/D-16/D-19): type-P feed probe. PR: telemetry
   accessor -- 0=none, 1=running, 2=CONSUMER, 3=NO_CONSUMER (D-19 encoding),
   latched for the pinned episode. */
int sync_type_p_probe_state(void);
/* PROBE: bench command entry point (protocol.c, Task 2) -- forces a probe
   window to start immediately. Caller must have already validated
   preconditions (type-P sensor, sync active, no deliberate hold). */
void sync_type_p_probe_force_start(void);
