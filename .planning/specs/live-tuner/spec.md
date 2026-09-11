# Live Tuner Specification

## Purpose
Capture the OpenSpec-native contract for the calibration tuner and
chatter resistance behavioral requirements.

## Requirements

### Requirement: Per-Feature Velocity Buckets
The tuner SHALL aggregate telemetry into feature + velocity buckets (rate + bias).

#### Scenario: Marker Active
- **WHEN** tune samples arrive while marker active
- **THEN** rounded velocity bucket is updated
- **AND** rate, uncertainty, bias, N, layers, and motion time are updated

### Requirement: Machine-Scoped Persistence
The system SHALL persist bucket state in a machine-scoped JSON file.

#### Scenario: Tuner Restart
- **WHEN** previous state file exists for machine
- **THEN** tuner loads existing evidence
- **AND** new samples extend evidence without zero-start

### Requirement: Observe-Only Default
The tuner SHALL NOT perform firmware writes without explicit permission flags.

#### Scenario: No Write Flags
- **WHEN** tuner runs without allow-write flags
- **THEN** tuner records and reports buckets
- **AND** NO `SET` or `SV` commands sent to firmware

### Requirement: Review-Only Workflow
The calibration workflow SHALL prefer analyzer review patches over blind tuning.

#### Scenario: Calibration Data Ready
- **WHEN** calibration data is available
- **THEN** analyzer emits review patch
- **AND** operator reviews and flashes settings

### Requirement: Diagnostics
The tuner MUST explain bucket states (TRACKING, STABLE, LOCKED) in state output.

#### Scenario: state-info
- **WHEN** `--state-info` invoked
- **THEN** output includes state, counts, and wait reason
- **AND** reason identifies noise-gated buckets

### Requirement: Sync State And Relief-Effort Diagnostics
Status and diagnostics output SHALL expose the current sync lifecycle state
and warn-only relief-effort counters (accumulated in commanded-MMU mm) so
operators and the offline analyzer can observe relief/fault behavior.

#### Scenario: State visible in status
- **WHEN** status is queried
- **THEN** the current state (`OFF`/`ACTIVE`/`RETRACT_ASSIST`/`RELIEF_PAUSE`/
  `FAULT_HOLD`) is reported

#### Scenario: Cannot-refill warning
- **WHEN** TENSION persists past the refill-effort threshold
- **THEN** a warn-only `SYNC cannot_refill` event is emitted
- **AND** no control behavior changes solely due to the counter

#### Scenario: Cannot-relieve warning
- **WHEN** COMPRESSION persists past the relief-effort threshold
- **THEN** a warn-only `SYNC cannot_relieve` event is emitted
- **AND** no control behavior changes solely due to the counter

### Requirement: Relief-effort counters are accumulated and exposed

The firmware SHALL accumulate relief effort in commanded-MMU mm:
`g_sync_refill_effort_mm` while the buffer is in TENSION and
`g_sync_relieve_effort_mm` while in COMPRESSION, derived from the existing
commanded-MMU mm integration. Both counters SHALL reset on sync state change
and buffer-state change (sustained-since-entry semantics). Both SHALL be
exposed in the `protocol.c` status line and as GET parameters. No control
behavior SHALL derive from these counters.

#### Scenario: Refill effort accrues during sustained TENSION

- **WHEN** the buffer stays in TENSION while filament is commanded
- **THEN** `g_sync_refill_effort_mm` increases by the commanded-MMU mm and
  is readable via status and GET

#### Scenario: Counters reset on state change

- **WHEN** the sync state or buffer state changes
- **THEN** both effort counters reset to zero

### Requirement: Warn only effort threshold events

The firmware SHALL emit warn-only diagnostic events when sustained relief
effort exceeds a configured threshold. A `SYNC cannot_refill` event MUST be
emitted once per episode when refill effort crosses
`CONF_SYNC_CANNOT_REFILL_MM` while still in TENSION. A `SYNC cannot_relieve`
event MUST be emitted once per episode when relieve effort crosses
`CONF_SYNC_CANNOT_RELIEVE_MM` while still in COMPRESSION. These events MUST be
diagnostic only and MUST NOT alter control output.

#### Scenario: cannot_refill warns once per episode

- **WHEN** refill effort exceeds `CONF_SYNC_CANNOT_REFILL_MM` while still in
  TENSION
- **THEN** exactly one `SYNC cannot_refill` event is emitted until the
  counter resets, and control output is unchanged

#### Scenario: cannot_relieve warns once per episode

- **WHEN** relieve effort exceeds `CONF_SYNC_CANNOT_RELIEVE_MM` while still
  in COMPRESSION
- **THEN** exactly one `SYNC cannot_relieve` event is emitted until the
  counter resets, and control output is unchanged

## Historical Rationale and Constants

### Live-Tune Lock
`LIVE_TUNE_LOCK:1` prevents host-firmware races. LOCKED = accepts host writes; UNLOCKED = reverts to defaults.

### Chatter Resistance
- **Noise Gate (`sigma/x`)**: Relative residual noise threshold required to lock (default 0.25).
- **3-Channel Unlock**:
    - **Catastrophic**: Residual exceeds 10.0 * sigma.
    - **Streak**: 5 consecutive residuals exceed 3.0 * sigma.
    - **Drift**: EWMA of residuals drifts more than 4.0 * sigma.
- **Lock Dwell**: Minimum 100 samples required after warmup.

### Rollback
- **`--reset-runtime`**: Sends `LOCK:0` + `LOAD`. Clears memory and re-applies defaults.
- **Schema 4 Migration**: One-way. Adds scalar residual stats to support 3-channel unlock.
