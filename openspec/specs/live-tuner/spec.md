# Live Tuner Specification

## Purpose
Tuner contract for Phase 2.8 and Phase 2.11 (chatter resistance) behavioral requirements.

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
- **THEN** the current state (`OFF`/`ACTIVE`/`HOLD`/`RELIEF_PAUSE`/
  `FAULT_HOLD`) is reported

#### Scenario: Cannot-refill warning
- **WHEN** ADVANCE persists past the refill-effort threshold
- **THEN** a warn-only `SYNC cannot_refill` event is emitted
- **AND** no control behavior changes solely due to the counter

#### Scenario: Cannot-relieve warning
- **WHEN** TRAILING persists past the relief-effort threshold
- **THEN** a warn-only `SYNC cannot_relieve` event is emitted
- **AND** no control behavior changes solely due to the counter

## Historical Rationale and Constants

### Live-Tune Lock
`LIVE_TUNE_LOCK:1` prevents host-firmware races. LOCKED = accepts host writes; UNLOCKED = reverts to defaults.

### Chatter Resistance (Phase 2.11)
- **Noise Gate (`sigma/x`)**: Relative residual noise threshold required to lock (default 0.25).
- **3-Channel Unlock**:
    - **Catastrophic**: Residual exceeds 10.0 * sigma.
    - **Streak**: 5 consecutive residuals exceed 3.0 * sigma.
    - **Drift**: EWMA of residuals drifts more than 4.0 * sigma.
- **Lock Dwell**: Minimum 100 samples required after warmup.

### Rollback
- **`--reset-runtime`**: Sends `LOCK:0` + `LOAD`. Clears memory and re-applies defaults.
- **Schema 4 Migration**: One-way. Adds scalar residual stats to support 3-channel unlock.
