# psf-type-p-sensor Specification

## Purpose
Captures behavior for the proportional Sync-Feedback Sensor path, including recovery, refill, and soft-wall control decisions.
## Requirements
### Requirement: Type-P Relief-Pause Auto-Recovery

The firmware SHALL re-arm type-P sync from `SYNC_RELIEF_PAUSE` when the buffer is
under genuine extruder demand, via two complementary paths:

1. **Polled predicate** (unchanged): in `sync_tick_gated_checks`, re-arm when
   `g_buf_pos < -0.6` AND (`g_sync_tension_transitioned` OR `g_vel_norm < -0.1`).
2. **Transition-driven** (new): on a debounced buffer transition into the type-P
   tension zone, the firmware SHALL re-arm immediately — mirroring the type-D
   rearm-on-transition path so recovery does not depend solely on a non-zero
   `g_vel_norm` that vanishes when the buffer is pinned at the rail.

On re-arm the controller SHALL enter `SYNC_ACTIVE`, bootstrap the feed via
`sync_bootstrap_sps()`, set `sync_auto_started`, and emit `SYNC:AUTO_START`. The
type-P re-arm SHALL NOT reseed `g_buf_pos` (it measures position directly).

The firmware SHALL NOT start idle or negative-sync buffer-stabilize while
`g_sync_state == SYNC_RELIEF_PAUSE`. Relief-pause is a sync-recovery state, not an
idle state: starting stabilize sets `g_boot_stabilizing`, which blacks out
`sync_tick` (and thus the relief re-arm) and can `buf_force_stable_state(BUF_NEUTRAL)`
the buffer — zeroing `g_buf_pos` and bypassing `sync_on_transition` — stranding
the controller. A returning fast feature SHALL be able to re-arm sync directly
from relief-pause without waiting for a stabilize to finish.

A static rest at the home tension rail (`g_buf_pos` near `-1.0`, `g_vel_norm`
near 0, no armed tension transition, no fresh tension transition) SHALL NOT
re-arm, so `SYNC_RELIEF_PAUSE` is not defeated by the buffer's resting position.

#### Scenario: Relief-pause recovers under demand

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, and the
  buffer is actively falling toward tension (`g_buf_pos < -0.6` with
  `g_vel_norm < -0.1` or an armed tension transition)
- **THEN** sync re-arms to `SYNC_ACTIVE` and emits `SYNC:AUTO_START`
- **AND** `g_buf_pos` is not reseeded from `buf_target_reserve_mm()`

#### Scenario: Relief-pause recovers on tension transition while pinned

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, and the
  debounced buffer state transitions into the type-P tension zone while
  `g_vel_norm` is near 0 (buffer pinned at the rail under a fast feature)
- **THEN** the transition-driven path re-arms sync to `SYNC_ACTIVE` and emits
  `SYNC:AUTO_START` without waiting for a non-zero velocity reading

#### Scenario: No stabilize starts during relief-pause

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, and the
  buffer rests in compression (the condition that would otherwise start
  negative-sync stabilize)
- **THEN** no `BUF_STAB:START` is emitted and `g_boot_stabilizing` stays false
- **AND** `sync_tick` continues to run its relief re-arm check each pass

#### Scenario: Relief-pause holds at static home rest

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, the buffer
  rests at the home tension rail with `g_vel_norm` near 0 and no armed or fresh
  tension transition
- **THEN** sync stays in `SYNC_RELIEF_PAUSE` and does not auto-restart

#### Scenario: Type-D relief recovery unchanged

- **WHEN** `BUF_SENSOR_TYPE == 0` and `g_sync_state == SYNC_RELIEF_PAUSE`
- **THEN** re-arm fires on `BUF_TENSION` or (`BUF_NEUTRAL` and `TASK_FEED`) as
  before, including the `g_buf_pos = buf_target_reserve_mm()` reseed

### Requirement: Type-P Stabilize Rail Breakaway

The firmware SHALL allow type-P idle/boot buffer-stabilize to drive the buffer
off a saturated rail to goal. While the analog signal is saturated
(`g_buf_analog_saturated_since_ms != 0`), the stagnation guard SHALL NOT abort on
the short-window position-change test; it SHALL keep driving and re-baseline the
stagnation reference position, aborting only if the buffer remains saturated past
`PSF_STAB_RAIL_BREAK_MS` measured from stabilize start. Once the signal
desaturates, the firmware SHALL apply the standard dry-spin stagnation check
(`PSF_STAB_STAGNANT_MS` / `PSF_STAB_STAGNANT_NORM`) with its window measured from
desaturation, not from stabilize start. Type-D stabilize is unchanged.

#### Scenario: Loaded buffer breaks off the tension rail

- **WHEN** `BUF_SENSOR_TYPE == 1`, filament present, the buffer rests saturated at
  the home/tension rail, and stabilize (`BS` or boot) starts
- **THEN** the motor drives toward goal until the buffer leaves saturation and
  reaches goal, emitting `BUF_STAB:DONE`
- **AND** no `BUF_STAB:STAGNANT_TIMEOUT` is emitted while still within
  `PSF_STAB_RAIL_BREAK_MS`

#### Scenario: Stuck/uncoupled buffer aborts at the breakaway cap

- **WHEN** `BUF_SENSOR_TYPE == 1` and the buffer stays saturated at the rail past
  `PSF_STAB_RAIL_BREAK_MS` (jammed or not coupled)
- **THEN** stabilize emits `BUF_STAB:STAGNANT_TIMEOUT` and stops
- **AND** it does not run to the 10 s deadline

#### Scenario: Off-rail dry-spin still aborts fast

- **WHEN** `BUF_SENSOR_TYPE == 1`, the signal is not saturated, and the buffer
  position changes less than `PSF_STAB_STAGNANT_NORM` within
  `PSF_STAB_STAGNANT_MS` after desaturation
- **THEN** stabilize emits `BUF_STAB:STAGNANT_TIMEOUT` and stops

### Requirement: Type-P Tension Refill Snap

The firmware SHALL bypass the type-P distance-based feed smoothing on the
tension/refill side so a starved buffer is refilled without the EMA ramp lag.
When `BUF_SENSOR_TYPE == 1`, the buffer is in the tension soft-wall zone
(`buf_pos_norm() < -PSF_SOFT_WALL_START`), and the control target exceeds the
current feed (`target_sps > sync_current_sps`), the applied feed SHALL be set
directly to the soft-wall target (`max_sps`). On that snap the smoothing filter
SHALL be seeded at the demand estimate (`extruder_est_sps`), so that once the
buffer leaves the wall the feed eases to the extruder rate rather than remaining
at max and overshooting into compression. Outside the tension wall, and on the
compression/neutral side, the existing distance-EMA + wall-clock-decay smoothing
is unchanged. Type-D feed application is unchanged.

#### Scenario: Fast move does not starve the buffer

- **WHEN** `BUF_SENSOR_TYPE == 1` and a fast extruder move pulls the buffer into
  the tension soft-wall zone with `target_sps > sync_current_sps`
- **THEN** `sync_current_sps` is set to the soft-wall target (`max_sps`) that tick
- **AND** the buffer is refilled without raising `SYNC:cannot_refill`

#### Scenario: Feed settles to demand on recovery

- **WHEN** the buffer climbs back out of the tension wall after a refill snap
- **THEN** the smoothing resumes from `g_psf_target_filt = extruder_est_sps`
- **AND** the feed eases to the extruder rate instead of overshooting into
  compression

#### Scenario: Compression side unaffected

- **WHEN** `BUF_SENSOR_TYPE == 1` and the buffer is on the compression/neutral side
- **THEN** the distance-EMA + wall-clock-decay smoothing applies unchanged

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

### Requirement: Type-P Unload Uses No Position-Based Over-Tension Guard
For type-P sensors, `TASK_UNLOAD` SHALL NOT use a position-based over-tension
guard (relief jog or tension-dwell block); it SHALL rely on the `UNLOAD_MAX`
distance limit (`UNLOAD_TIMEOUT`) for the stuck case. The type-D guards (recover
jog + `UNLOAD_TENSION_BLOCK`) SHALL remain unchanged and gated `BUF_SENSOR_TYPE == 0`.

First-rig finding (supersedes earlier position-based design): type-P homes at
tension rail and relaxes back to it throughout ANY normal retract — mid-tube,
deep, or starting from compression — so buffer position is indistinguishable from
real extruder-gripping jam (both sit at rail; only whether OUT eventually
clears differs). Position-based guard false-fired (relief jog stalled unload at
~0 mm; dwell block raised spurious `UNLOAD_BLOCKED`).

#### Scenario: Type-P normal retract is not blocked
- **WHEN** a type-P unload retracts with the buffer resting at the tension rail (mid-tube, deep, or starting from compression)
- **THEN** no relief jog fires and no `UNLOAD_BLOCKED` is emitted
- **AND** the retract proceeds until OUT clears (`UNLOADED`) or the distance limit (`UNLOAD_TIMEOUT`)

#### Scenario: Type-P stuck unload falls through to the distance limit
- **WHEN** the extruder grips and OUT never clears
- **THEN** the retract continues to `UNLOAD_MAX` and emits `UNLOAD_TIMEOUT` (no position-based `UNLOAD_BLOCKED`)

#### Scenario: Type-D unload guard unchanged
- **WHEN** `BUF_SENSOR_TYPE == 0` and the buffer stays in `BUF_TENSION` through a retract
- **THEN** the type-D recover jog and `UNLOAD_TENSION_BLOCK` behave as before (`UNLOAD_BLOCKED` after `UNLOAD_TENSION_BLOCK_MS`)

### Requirement: Type-P Fault Timers Scoped to Active Sync

The firmware SHALL scope the type-P tension-dwell and saturation fault timers to
the active-sync window so idle-accumulated state cannot fire a spurious fault on
engagement or deadlock fault recovery. On every type-P sync activation (normal
auto-start, relief-pause re-arm, fault-hold recovery) the firmware SHALL restart
`sync_tension_pin_since_ms` to `now` when the buffer is in `BUF_TENSION` and to `0`
otherwise. On `FAULT_HOLD_RECOVERY` the firmware SHALL reset
`g_buf_analog_saturated_since_ms` so the recovered active state gets a fresh
saturation window. Type-D fault handling is unchanged.

#### Scenario: Normal extrude does not fault on engagement

- **WHEN** `BUF_SENSOR_TYPE == 1`, the buffer has rested idle in the control
  tension zone (goal compression-side), and a normal extrude triggers `AUTO_START`
- **THEN** the tension-dwell fault does not fire from idle-accumulated dwell
- **AND** sync engages and the refill snap recovers the buffer without `FAULT_HOLD`

#### Scenario: Fault recovery does not instantly re-fault

- **WHEN** `BUF_SENSOR_TYPE == 1`, sync enters `FAULT_HOLD_RECOVERY` with the buffer
  still pinned at the tension rail
- **THEN** `g_buf_analog_saturated_since_ms` is cleared so the saturation timer
  restarts
- **AND** the recovered active state gets a full `PSF_WALL_SAT_MS` window for the
  refill snap before any re-fault — no infinite `FAULT_HOLD ↔ RECOVERY` loop

#### Scenario: Genuine sustained starve still faults

- **WHEN** `BUF_SENSOR_TYPE == 1` and the buffer stays pinned at tension during
  active sync past the dwell/saturation window despite max refill
- **THEN** the terminal `fault_hold` still fires (gear protection preserved)

### Requirement: Type-P Feed Quality and Reliable Stabilize

Type-P feed control SHALL track extruder demand on a real print without sustained
buffer hunting or end-of-move overshoot that produces print artifacts, and a manual
`BS` SHALL drive the buffer to goal in a single invocation from any non-saturated
position. Acceptance is measured against a real print, not isolated bench bursts.

#### Scenario: Steady print feed stays near goal

- **WHEN** `BUF_SENSOR_TYPE == 1` and the printer extrudes continuously
- **THEN** the buffer holds a band near goal without a sustained tension↔compression
  limit cycle, and no `SYNC:FAULT_HOLD` / `SYNC:cannot_refill` fires mid-print

#### Scenario: Single-shot BS recovers from mid-tension

- **WHEN** `BUF_SENSOR_TYPE == 1`, the buffer rests in the control tension zone but
  is not saturated (`CF == 1.0`), and a manual `BS` is issued
- **THEN** the buffer is driven to goal on that single `BS` (no silent no-op
  requiring a second invocation)

#### Scenario: BS stabilizes the filament-bearing lane when the active lane is empty

- **WHEN** `BUF_SENSOR_TYPE == 1`, the active lane has no filament on its IN or OUT
  sensor, the other lane has filament present, and a manual `BS` is issued
- **THEN** stabilize drives the filament-bearing lane to goal instead of silently
  replying `OK` without motion

#### Scenario: BS breaks away from a deep saturated rail in one shot

- **WHEN** `BUF_SENSOR_TYPE == 1`, the buffer is saturated at a rail with the piston
  driven past the sensing range, and a manual `BS` is issued
- **THEN** stabilize keeps driving through the sensor-flat breakaway up to
  `PSF_STAB_RAIL_BREAK_MS` (default 3000 ms) and parks at goal in that single `BS`

#### Scenario: MV takes over cleanly from an in-flight stabilize

- **WHEN** a buffer stabilize or relief move is in flight and an `MV:` command is
  issued
- **THEN** the stabilize is cancelled before the move starts, so exactly one
  controller drives the lane motor (no stomped move, no zombie stabilize stop)

