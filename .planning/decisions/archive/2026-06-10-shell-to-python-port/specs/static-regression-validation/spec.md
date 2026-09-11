# static-regression-validation Specification (Delta)

## MODIFIED Requirements

### Requirement: Automated Regression Test Suite
The host regression validation test suite MUST automatically discover and execute all unit tests in the scripts directory.

#### Scenario: Running the regression gate script
- **WHEN** the operator runs `scripts/validate_regression.py`
- **THEN** the script executes all active Python unit tests
- **AND** reports success only if all tests pass with exit code 0

### Requirement: Dev-tuning superset firmware build

The regression gate MUST build the firmware with `FLARE_DEV_TUNING=ON` so code behind
`#ifdef FLARE_DEV_TUNING` (e.g. `protocol.c` Tier-3 SET/GET handlers) is compiled and
validated, matching the configuration deployed on the Pi.

#### Scenario: Running the regression gate script

- **WHEN** the operator runs `scripts/validate_regression.py`
- **THEN** the firmware is configured and built with `FLARE_DEV_TUNING=ON`
- **AND** a broken reference inside a `#ifdef FLARE_DEV_TUNING` block fails the gate

#### Scenario: Dev-guarded code after a bulk refactor

- **WHEN** a bulk identifier rename or refactor lands
- **THEN** the dev-tuning-ON build in the gate compiles the dev-guarded handlers
- **AND** any identifier missed in an inactive preprocessor branch is caught locally
