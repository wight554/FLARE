# Delta: static-regression-validation

## ADDED Requirements

### Requirement: Settings parity guard executes in the gate

The settings round-trip parity check (`test_settings_parity.py`) MUST execute —
not merely import — under `validate_regression.sh`, and MUST scan every
`settings_load_*` helper body (the load logic is split across
`settings_load_motion/tmc/servo_cutter/sync_reload`), so a save/load asymmetry
fails the gate.

#### Scenario: Gate runs parity check

- **WHEN** `scripts/validate_regression.sh` runs
- **THEN** parity assertions execute via unittest discovery
- **AND** a field saved but not loaded (or loaded but not defaulted) fails the gate

#### Scenario: Load helpers refactored

- **WHEN** load logic moves between `settings_load*` helpers without changing behavior
- **THEN** the parity check still passes (it scans all helper bodies, not one
  hardcoded function)
