## ADDED Requirements

### Requirement: PSF Endpoint Calibration
The firmware SHALL support runtime calibration of PSF sensor endpoints via
`CAL:PSF_COMP`, `CAL:PSF_TENS`, and `CAL:PSF_NEUT` commands. Each command
SHALL sample the ADC at the moment of invocation, store the result in the
corresponding runtime variable, and persist it to NVM.

#### Scenario: Calibrate compression extreme
- **WHEN** operator pushes buffer to physical compression extreme and sends `CAL:PSF_COMP`
- **THEN** firmware samples ADC, stores result as `BUF_PSF_MAX_COMP`, persists to NVM
- **AND** `GET BUF_PSF_MAX_COMP` returns the newly stored value

#### Scenario: Calibrate tension extreme
- **WHEN** operator pulls buffer to physical tension extreme and sends `CAL:PSF_TENS`
- **THEN** firmware samples ADC, stores result as `BUF_PSF_MAX_TENS`, persists to NVM

#### Scenario: Calibrate neutral
- **WHEN** operator releases buffer to natural rest and sends `CAL:PSF_NEUT`
- **THEN** firmware samples ADC, stores result as `BUF_PSF_NEUTRAL`, persists to NVM

### Requirement: Asymmetric Normalization with Auto-Polarity
The firmware SHALL normalize raw PSF ADC readings to [-1,1] using asymmetric
endpoint calibration. Polarity SHALL be auto-detected from calibration values:
if `BUF_PSF_MAX_COMP < BUF_PSF_MAX_TENS`, compression is the lower raw value
(reversed=true); otherwise compression is the higher raw value. No explicit
invert flag SHALL be required.

#### Scenario: Normal polarity (compression = low raw)
- **WHEN** `BUF_PSF_MAX_COMP < BUF_PSF_MAX_TENS` and ADC reads near `BUF_PSF_MAX_COMP`
- **THEN** normalized `g_buf_pos` is negative (toward -1)
- **AND** `buf_state_raw()` returns `BUF_COMPRESSION`

#### Scenario: Inverted polarity (compression = high raw)
- **WHEN** `BUF_PSF_MAX_COMP > BUF_PSF_MAX_TENS` and ADC reads near `BUF_PSF_MAX_COMP`
- **THEN** normalized `g_buf_pos` is negative (toward -1)
- **AND** `buf_state_raw()` returns `BUF_COMPRESSION`

#### Scenario: Neutral maps to zero
- **WHEN** ADC reads `BUF_PSF_NEUTRAL`
- **THEN** normalized `g_buf_pos` is 0.0

### Requirement: Goal-Relative Zone Boundaries
For type-P sensors, buffer zone boundaries SHALL be derived from `BUF_GOAL`
converted to normalized space (TENSION / NEUTRAL / COMPRESSION), not from a
symmetric `BUF_THR`. NEUTRAL SHALL mean "near goal," not "near raw 0.5."

#### Scenario: Buffer at goal is NEUTRAL
- **WHEN** `g_buf_pos` (normalized) is within `PSF_ZONE_DEADBAND` of `goal_norm`
- **THEN** `buf_state_raw()` returns `BUF_NEUTRAL`

#### Scenario: Buffer beyond goal toward tension is TENSION
- **WHEN** `g_buf_pos > goal_norm + PSF_ZONE_DEADBAND`
- **THEN** `buf_state_raw()` returns `BUF_TENSION`

#### Scenario: Buffer beyond goal toward compression is COMPRESSION
- **WHEN** `g_buf_pos < goal_norm - PSF_ZONE_DEADBAND`
- **THEN** `buf_state_raw()` returns `BUF_COMPRESSION`

### Requirement: BUF_GOAL User Param in Raw ADC Space
`BUF_GOAL` SHALL be settable and gettable via the protocol in raw ADC fraction
[0,1] space — the same space as `BUF_PSF_MAX_COMP`, `BUF_PSF_MAX_TENS`, and
`BUF_PSF_NEUTRAL`. Default SHALL be 0.3 (between neutral 0.5 and compression
extreme 0.0 for normal PSF polarity). It SHALL be persisted in NVM.

#### Scenario: Set and retrieve BUF_GOAL
- **WHEN** operator sends `SET BUF_GOAL:0.25`
- **THEN** `GET BUF_GOAL` returns `0.250`
- **AND** value persists across power cycle

#### Scenario: Default value on factory reset
- **WHEN** NVM is reset to defaults
- **THEN** `BUF_GOAL` is 0.3

### Requirement: Remove BUF_RANGE and BUF_INVERT
`BUF_RANGE` and `BUF_INVERT` SHALL be removed from the protocol and NVM.
Polarity is handled by calibration (D2). `BUF_RANGE` is superseded by
asymmetric endpoint calibration.

#### Scenario: BUF_RANGE no longer accepted
- **WHEN** firmware receives `SET BUF_RANGE:<val>`
- **THEN** firmware returns an error or ignores the command (not silently stored)

#### Scenario: BUF_INVERT no longer accepted
- **WHEN** firmware receives `SET BUF_INVERT:<val>`
- **THEN** firmware returns an error or ignores the command

### Requirement: Continuous Extruder Estimate
For type-P, the firmware SHALL compute buffer velocity from the per-tick
position delta and update the extruder-rate estimate every control tick, using
`extruder_mm_s = mmu_mm_s + arm_vel` where `arm_vel = vel_norm *
half_travel_mm`. It SHALL NOT use crossing-event estimation for type-P.

#### Scenario: Velocity updates every tick
- **WHEN** type-P control tick runs and position has changed
- **THEN** `vel_norm = (g_buf_pos - g_buf_pos_prev) / dt_s` is computed
- **AND** `extruder_est_sps` is updated from the live extruder rate

#### Scenario: Estimator-compensation machinery inactive for type-P
- **WHEN** `BUF_SENSOR_TYPE == 1`
- **THEN** virtual position tick, drift observer, sigma/confidence model,
  model-stall detection, and EST_FALLBACK do NOT run
- **AND** confidence is 1.0 unless the sensor is saturated

### Requirement: Gradual PD Control with Dead Zone
For type-P, `psf_control_law()` SHALL produce a target feed rate as the sum of
a continuous feedforward (`extruder_est_sps`), a proportional term on position
error relative to goal, and a derivative term on filtered buffer velocity. The
proportional term SHALL be suppressed within `PSF_CTRL_DEADBAND` of goal; the
derivative term SHALL remain active regardless of dead zone.

#### Scenario: Steady at goal rejects jitter
- **WHEN** `|error_norm| < PSF_CTRL_DEADBAND` and velocity is near zero
- **THEN** the proportional and derivative terms are ~0
- **AND** target feed holds at the feedforward rate without hunting

#### Scenario: Fast move caught inside dead zone
- **WHEN** buffer velocity spikes while position is still within the dead zone
- **THEN** the derivative term fires immediately
- **AND** target feed responds before position leaves the dead zone

#### Scenario: Gradual response to slow drift
- **WHEN** position drifts slowly toward tension (buffer draining)
- **THEN** the proportional term increases target feed proportionally to error
- **AND** the change is ramp-slew-limited (no step)

### Requirement: Filtered Derivative
The derivative term SHALL operate on a low-pass-filtered velocity computed from
the already-smoothed position, to avoid amplifying ADC noise (derivative kick).

#### Scenario: Velocity is filtered
- **WHEN** velocity is computed for the derivative term
- **THEN** it is taken from the EWMA-smoothed position and further low-pass
  filtered by `PSF_VEL_ALPHA` before use

### Requirement: Soft Walls
For type-P, the control target SHALL progressively blend from the PD output
toward a safety limit as `|pos_norm|` enters `[PSF_SOFT_WALL_START, 1.0]`:
toward maximum feed on the tension side (urgent refill) and toward zero on the
compression side (stop overfeed).

#### Scenario: Approaching tension wall blends to max feed
- **WHEN** `pos_norm` rises above `PSF_SOFT_WALL_START` toward +1
- **THEN** target feed blends progressively toward `max_sps`
- **AND** the blend reaches full safety limit at `pos_norm == 1.0`

#### Scenario: Approaching compression wall blends to stop
- **WHEN** `pos_norm` falls below `-PSF_SOFT_WALL_START` toward -1
- **THEN** target feed blends progressively toward 0

### Requirement: Hard Catch and Print-Stop Detection
For type-P, a rapid velocity spike toward compression SHALL trigger a
reversible fast brake. The firmware SHALL then disambiguate a transient
slowdown from a real print stop by observing subsequent buffer motion: if the
buffer drifts back toward tension within `PSF_STOP_CONFIRM_MS`, control SHALL
resume; if the buffer stays pinned at the compression wall, the controller
SHALL enter relief pause.

#### Scenario: Rapid compression jump triggers reversible brake
- **WHEN** `|vel_norm|` toward compression exceeds `PSF_JUMP_NORM_PER_S`
- **THEN** `sync_fast_brake` engages (feed → 0) and is reversible

#### Scenario: Slowdown recovers
- **WHEN** after a brake, velocity turns positive (toward tension) within `PSF_STOP_CONFIRM_MS`
- **THEN** the controller resumes PD control without stopping the print

#### Scenario: Real stop confirmed
- **WHEN** after a brake, the buffer stays pinned at compression past `PSF_STOP_CONFIRM_MS`
- **THEN** the controller enters `sync_relief_pause()`

#### Scenario: Sustained compression saturation
- **WHEN** `pos_norm <= -0.99` for `PSF_WALL_SAT_MS`
- **THEN** the controller enters `sync_relief_pause()`

#### Scenario: Sustained tension saturation
- **WHEN** `pos_norm >= +0.99` for `PSF_WALL_SAT_MS`
- **THEN** the controller enters `sync_fault_hold()`

### Requirement: Type-P Unload Over-Tension Guard
During `TASK_UNLOAD`, for type-P sensors the firmware SHALL protect against
over-tension caused by the printer extruder out-pulling the MMU retract. While
filament is still present in the active-retract phase (the same scope as the
type-D unload guards: `retract_deadline_ms == 0`, `!unload_to_in`, outside the
double-load exception), a buffer pinned at the tension rail
(`g_buf_pos >= PSF_TENSION_PIN_NORM`) SHALL be treated as a fault and a buffer
deflected below the rail SHALL be treated as healthy. A relaxation back to the
home tension position after the OUT sensor clears SHALL NOT be treated as a
fault. A sustained pin SHALL escalate to `UNLOAD_BLOCKED`, reusing
`UNLOAD_TENSION_BLOCK_MS`. A velocity spike toward tension SHALL trigger a
reversible brake of the MMU retract within one control tick.

#### Scenario: Healthy unload deflects below rail
- **WHEN** filament is present and the MMU is retracting
- **THEN** `g_buf_pos` is below `PSF_TENSION_PIN_NORM`
- **AND** `buf_tension_since_ms` is reset (no block accumulates)

#### Scenario: Relaxation to home after clear is not a fault
- **WHEN** filament clears and `g_buf_pos` returns to ~+1.0 after the OUT sensor clears
- **THEN** the unload guard branch is inactive (`retract_deadline_ms` set)
- **AND** no `UNLOAD_BLOCKED` is emitted

#### Scenario: Tug-of-war pins the rail and blocks
- **WHEN** the extruder grips and the buffer stays pinned at `>= PSF_TENSION_PIN_NORM` while filament is present
- **THEN** `buf_tension_since_ms` accumulates
- **AND** after `UNLOAD_TENSION_BLOCK_MS` the lane stops and `UNLOAD_BLOCKED` is emitted

#### Scenario: Velocity spike toward tension brakes immediately
- **WHEN** buffer velocity spikes toward tension during retract
- **THEN** the MMU reverse is braked within one control tick (reversible)

#### Scenario: Relief jog skipped during toolchange unload
- **WHEN** the unload is toolchange-driven (`unload_buf_recover_done` preset true)
- **THEN** the one-shot relief jog is skipped
- **AND** the block-abort tier remains active
