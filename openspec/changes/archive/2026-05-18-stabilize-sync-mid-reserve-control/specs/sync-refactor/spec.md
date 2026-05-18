## ADDED Requirements

### Requirement: Safe sync defaults use the hardware-tested reserve envelope

The firmware defaults SHALL use the current hardware-tested sync reserve envelope for fresh generated builds: `SYNC_MAX_RATE` equivalent to 2200 mm/min, `SYNC_DN_RATE` equivalent to 80 mm/min per control tick, `SYNC_OVERSHOOT_PCT` set to 150, `SYNC_ADV_RAMP_MS` set to 0, and `SYNC_OVERSHOOT_MID_EXT` enabled. The default `SYNC_MIN_RATE` SHALL remain 100 mm/min.

#### Scenario: Fresh generated config

- **WHEN** firmware defaults are generated from `config.ini` or `scripts/gen_config.py`
- **THEN** the compiled sync defaults match the hardware-tested safe reserve envelope
- **AND** `SYNC_MIN_RATE` remains 100 mm/min

#### Scenario: Runtime override remains possible

- **WHEN** the operator sends existing `SET:` commands for the sync tunables
- **THEN** the runtime values still override the compiled defaults
- **AND** the corresponding `GET:` commands report the runtime values

### Requirement: MID reserve control prevents stale-estimator advance collapse

During `SYNC_ACTIVE`, while the active lane is feeding in `BUF_MID`, the sync controller SHALL avoid collapsing target feed below a conservative anti-advance floor when the reserve position is near or beyond the trailing reserve target and the estimator is stale or low-confidence. This assist SHALL NOT apply in `BUF_TRAILING` so trailing braking and recovery can still reduce feed to the existing safe floor.

#### Scenario: Stale MID dwell near trailing reserve

- **WHEN** sync is active, the active lane is feeding, the buffer is physically in `BUF_MID`, and reserve control is dwelling near or beyond the trailing reserve target with stale estimator evidence
- **THEN** the controller maintains a conservative feed floor sufficient to reduce repeated `BUF_ADVANCE` hits
- **AND** the floor is based on existing sync flow/baseline information rather than raising global `SYNC_MIN_RATE`

#### Scenario: Physical trailing recovery

- **WHEN** the buffer enters `BUF_TRAILING`
- **THEN** the MID-only anti-advance assist is removed
- **AND** existing trailing recovery, overshoot trim, collapse ramp, fast brake, and fault-hold protections remain authoritative

#### Scenario: Advance-risk warning stays diagnostic

- **WHEN** `ADV_RISK_HIGH` is emitted during a print
- **THEN** the event remains a warning that advance-pin density is high
- **AND** standalone sync control still acts through reserve-target behavior rather than requiring a host tool
