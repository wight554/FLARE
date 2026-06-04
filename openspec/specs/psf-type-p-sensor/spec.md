# psf-type-p-sensor Specification

## Purpose
TBD - created by archiving change psf-relief-pause-rearm. Update Purpose after archive.
## Requirements
### Requirement: Type-P Relief-Pause Auto-Recovery

The firmware SHALL re-arm type-P sync from `SYNC_RELIEF_PAUSE` when the buffer is
under genuine extruder demand, using the same demand discriminator as cold
auto-start (D18): `g_buf_pos < -0.6` AND (`g_sync_tension_transitioned` OR
`g_vel_norm < -0.1`). On re-arm the controller SHALL enter `SYNC_ACTIVE`,
bootstrap the feed via `sync_bootstrap_sps()`, set `sync_auto_started`, and emit
`SYNC:AUTO_START`. The type-P re-arm SHALL NOT reseed `g_buf_pos` (it measures
position directly).

A static rest at the home tension rail (`g_buf_pos` near `-1.0`, `g_vel_norm`
near 0, no armed tension transition) SHALL NOT re-arm, so `SYNC_RELIEF_PAUSE` is
not defeated by the buffer's resting position.

#### Scenario: Relief-pause recovers under demand

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, and the
  buffer is actively falling toward tension (`g_buf_pos < -0.6` with
  `g_vel_norm < -0.1` or an armed tension transition)
- **THEN** sync re-arms to `SYNC_ACTIVE` and emits `SYNC:AUTO_START`
- **AND** `g_buf_pos` is not reseeded from `buf_target_reserve_mm()`

#### Scenario: Relief-pause holds at static home rest

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, the buffer
  rests at the home tension rail with `g_vel_norm` near 0 and no armed tension
  transition
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

