# Sync Refactor Specification

## Purpose
Durable contract for FLARE sync, tuning, tracking, and analyzer behavioral requirements and historical rationale.

## Requirements

### Requirement: Standalone Sync
FLARE SHALL run sync, toolchange, and RELOAD without host after calibration flash.

#### Scenario: Host Detached
- **WHEN** calibration reviewed + flashed
- **THEN** FLARE runs from firmware/runtime ONLY
- **AND** host tuner not required during print

#### Scenario: Live Debug
- **WHEN** `flare_live_tuner.py` runs without writes
- **THEN** tuner observes and persists state
- **AND** NO `SET`/`SV` commands sent to firmware

### Requirement: Observe-Only Calibration
The system SHALL collect markers, buckets, and patches without mutation unless explicit opt-in.

#### Scenario: Default Tuner
- **WHEN** markers arrive at tuner
- **THEN** state and CSV update
- **AND** NO firmware writes occur

#### Scenario: Review Patch
- **WHEN** `flare_analyze.py` emits a patch
- **THEN** patch is for review only
- **AND** operator MUST copy accepted values to `config.ini`

### Requirement: Sidecar + UDS Tracking
The system SHALL prefer sidecar JSON + Klipper UDS over shell markers when available.

#### Scenario: Sidecar Active
- **WHEN** sidecar + UDS provided
- **THEN** tuner synthesizes `FLARE_TUNE` events
- **AND** `on_m118` contract remains stable

#### Scenario: Fallback
- **WHEN** UDS or sidecar fail
- **THEN** tuner falls back to legacy marker-file or shell flow
- **AND** bucket learning semantics remain consistent

### Requirement: Durable/Migratable State
The tuner MUST persist buckets and migrate schema without data loss across versions.

#### Scenario: Schema 3 -> 4
- **WHEN** tuner loads schema 3 database
- **THEN** state migrates to schema 4
- **AND** estimates, locks, and `_meta` remain intact

#### Scenario: Future Refusal
- **WHEN** state file has schema version newer than tuner
- **THEN** tuner refuses to load
- **AND** NO auto-mutation occurs

### Requirement: Chatter Resistance
Buckets SHALL become LOCKED on evidence/noise pass and UNLOCK only on strong mismatch.

#### Scenario: Low Evidence
- **WHEN** bucket has insufficient samples or runs
- **THEN** bucket stays TRACKING or STABLE

#### Scenario: Moderate Outlier
- **WHEN** LOCKED bucket sees single moderate residual outlier
- **THEN** bucket remains LOCKED
- **AND** sample credited to locked dwell

#### Scenario: Catastrophic/Streak/Drift
- **WHEN** catastrophic, streak, or drift threshold hit
- **THEN** bucket unlocks to TRACKING

### Requirement: Relative Noise Gate
The tuner SHALL use `sigma/x` ratio, not absolute variance, for lock decisions.

#### Scenario: High Flow
- **WHEN** bucket `sigma/x` <= threshold
- **THEN** noise gate allows lock

#### Scenario: Low Flow
- **WHEN** bucket `sigma/x` > threshold after warmup
- **THEN** bucket remains STABLE
- **AND** `state-info` reports noise wait reason

### Requirement: State-Aware Recommendations
The analyzer SHALL weight by precision/count, not raw CSV clusters, during recommendation.

#### Scenario: LOCKED Exists
- **WHEN** LOCKED buckets exist in state
- **THEN** analyzer uses ONLY LOCKED set for recommendations

#### Scenario: Safe Mode (Zero Locked)
- **WHEN** `--mode safe` runs with zero locked buckets
- **THEN** analyzer refuses to emit learned values
- **AND** process exits non-zero

#### Scenario: Precision Weighting
- **WHEN** multiple qualifying buckets exist
- **THEN** analyzer weights by `n/sigma²`
- **AND** 5/95 tails are trimmed

### Requirement: Comparable Run Consistency
The gate SHALL use recommendation path consistency, filtered to mature runs.

#### Scenario: Recommendation Stable
- **WHEN** raw medians vary across runs but recommendation path stable
- **THEN** acceptance gate passes consistency check

#### Scenario: Immature Run
- **WHEN** run has few rows or low confidence
- **THEN** run is skipped from consistency reduction
- **AND** reason reported in patch diagnostics

### Requirement: FAIL vs WARN Separation
The gate SHALL FAIL only on unreliable recommendations or pathological scatter.

#### Scenario: Stale Variance Reference
- **WHEN** BP sigma p95 > current reference but < ceiling
- **THEN** gate warns about stale reference
- **AND** emits corrective recommendation

#### Scenario: Gray Mass
- **WHEN** mass above floor but below target
- **THEN** gate passes with mass warning

#### Scenario: Pathological Scatter
- **WHEN** BP sigma p95 > ceiling
- **THEN** gate fails and reports hardware failure

### Requirement: Bidirectional Drift Observation
The residual drift observer SHALL measure errors on both `ADVANCE` and `TRAILING` boundaries to prevent directional blindness.

#### Scenario: Flow Overestimation
- **WHEN** the model overestimates extruder flow
- **THEN** virtual position drifts to the right
- **AND** the physical arm hits `TRAILING`
- **AND** the observer records the positive residual to apply a corrective offset

### Requirement: Double-Integrator Avoidance
The feedforward velocity estimator SHALL NOT bleed towards the PI controller output in the safe zone.

#### Scenario: Buffer Mid-Range
- **WHEN** the physical arm is floating safely in `BUF_MID`
- **THEN** the feedforward estimator holds its last known measurement
- **AND** normal P/I terms handle local position errors without corrupting the feedforward baseline

### Requirement: Bias Accumulation
The analyzer and tuner SHALL accumulate position error offsets onto the current configuration value, avoiding fixed setpoint anchors.

#### Scenario: Steady-State Offset
- **WHEN** physical forces cause a steady-state offset `bp > rt`
- **THEN** the tooling incrementally updates the current configuration bias
- **AND** subsequent runs accumulate further corrections until the physical arm tracks the target

### Requirement: Disciplined Live Baseline Learner
The firmware live baseline learner SHALL update only in `SYNC_ACTIVE`, require
multi-cycle agreement, reject high-variance observations, and enforce a
time-and-distance cooldown. It SHALL remain non-persistent and up-only; the
offline analyzer remains the sole persistent baseline/bias authority.

#### Scenario: No learning in non-active states
- **WHEN** the controller is in `SYNC_OFF`, `SYNC_HOLD`,
  `SYNC_RELIEF_PAUSE`, or `SYNC_FAULT_HOLD`, or trailing-recovery / fast-brake
  is active
- **THEN** the live baseline value is not updated

#### Scenario: Multi-cycle and variance gating
- **WHEN** a single settle into MID occurs
- **THEN** the baseline does not move on that observation alone
- **AND** it moves only after N comparable settles within the variance
  threshold and after the cooldown (time AND commanded-MMU distance) elapses

#### Scenario: Live drift self-heals
- **WHEN** the firmware reboots or reloads settings
- **THEN** the live baseline resets to the offline/config authority value
- **AND** no live-learned value is persisted

### Requirement: Non-Destructive Lifecycle Preserves Standalone Operation
Replacing destructive disable with explicit non-destructive states SHALL NOT
require host involvement and SHALL keep standalone post-flash operation
intact.

#### Scenario: Recovery without host
- **WHEN** the controller enters `SYNC_RELIEF_PAUSE` or `SYNC_FAULT_HOLD`
- **THEN** resumption/recovery occurs from firmware logic alone
- **AND** no host tuner or command is required during print

### Requirement: Baseline and bias sourced from flow schedule

The sync controller SHALL obtain its baseline and trailing-bias by
evaluating the flow-keyed schedule at the live `extruder_est_sps`,
replacing the single-scalar read, with a length-1 schedule as the exact
degenerate fallback. This SHALL NOT alter the full-bias invariant: the
schedule only supplies inputs that `SYNC_ACTIVE` reserve-target control
already consumes; reserve target, `reserve_correction`, `zone_bias`,
soft-wall trim, and collapse ramp SHALL be unchanged.

#### Scenario: Active params follow flow

- **WHEN** live flow moves across schedule segments during `SYNC_ACTIVE`
- **THEN** the baseline/bias inputs update per the schedule while
  reserve-target control logic is unchanged

#### Scenario: Full-bias invariant preserved

- **WHEN** the schedule is evaluated for any flow
- **THEN** reserve target / `reserve_correction` / `zone_bias` / soft-wall
  trim / collapse ramp behavior is identical to before this change

### Requirement: Live learner ratchets within the active flow segment

The disciplined live baseline learner SHALL remain ephemeral, up-only,
non-persistent, and disciplined (multi-cycle, variance-reject, cooldown,
`SYNC_ACTIVE`-gated) but SHALL ratchet the baseline of the currently
active flow segment rather than a global scalar. On reboot or settings
reload the schedule SHALL reload from config (offline authority); the
live segment delta SHALL be lost.

#### Scenario: Segment-scoped ratchet

- **WHEN** the disciplined learner accepts an update while flow is in a
  given segment
- **THEN** only that segment's baseline ratchets up, not a global scalar

#### Scenario: Ephemeral on reboot

- **WHEN** the device reboots or reloads settings
- **THEN** the schedule is restored from config and any live segment
  ratchet is discarded

## Historical Design Decisions (Traceability)
- **D1 (PSF)**: Generic adapter until hardware land.
- **D2 (Advance Dwell)**: Default 6000 ms (400 ms start).
- **D3 (Versioning)**: Bump `SETTINGS_VERSION` ONLY on `settings_t` struct change.
- **D4 (Hot-swap)**: `BUF_SENSOR_TYPE` swap ONLY when IDLE.
- **D5 (Follow)**: Reload follow logic = baseline. Telemetry only.
- **D6 (Overshoot)**: `SYNC_OVERSHOOT_PCT` default OFF.
- **D7 (Status)**: CDC strings additive-at-tail. FROZEN order.
- **D2.5-A (Integral)**: 0.0 gain default. 0.6 mm clamp.
- **D2.5-B (Confidence)**: Physics-based sigma growth.

## Frozen Interfaces and Regression Constraints
- **on_m118 Ingress**: marker parsing stable. Additive only.
- **Motion Tracker**: `klipper_motion_tracker.py`, `gcode_marker.py` logic frozen.
- **Learning Loop**: KF update in tuner frozen. Rec logic stays in analyzer.
- **Data Safety**: State JSON / CSV runs must remain usable. Non-destructive migration.
- **UDS Contract**: Subscription fields frozen.
