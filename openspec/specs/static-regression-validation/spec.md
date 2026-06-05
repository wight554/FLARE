# static-regression-validation Specification

## Purpose
Automated host-side Python unit testing and regression gating — discovers and runs all `scripts/test_*.py`, enforcing the static validation gate before commits.
## Requirements
### Requirement: Automated Regression Test Suite
The host regression validation test suite MUST automatically discover and execute all unit tests in the scripts directory.

#### Scenario: Running the regression gate script
- **WHEN** the operator runs `scripts/validate_regression.sh`
- **THEN** the script executes all active Python unit tests
- **AND** reports success only if all tests pass with exit code 0

### Requirement: Standard Test Discovery Compatibility
All Python test modules in the repository MUST be compatible with standard test runners (such as unittest and pytest) without triggering module-import exits.

#### Scenario: Running test discovery
- **WHEN** a standard test discovery runner imports the `test_flare_mmu_status.py` module
- **THEN** the import completes successfully without executing a `sys.exit` block
- **AND** the test suite executes normally

### Requirement: Dev-tuning superset firmware build

The regression gate MUST build the firmware with `FLARE_DEV_TUNING=ON` so code behind
`#ifdef FLARE_DEV_TUNING` (e.g. `protocol.c` Tier-3 SET/GET handlers) is compiled and
validated, matching the configuration deployed on the Pi.

#### Scenario: Running the regression gate script

- **WHEN** the operator runs `scripts/validate_regression.sh`
- **THEN** the firmware is configured and built with `FLARE_DEV_TUNING=ON`
- **AND** a broken reference inside a `#ifdef FLARE_DEV_TUNING` block fails the gate

#### Scenario: Dev-guarded code after a bulk refactor

- **WHEN** a bulk identifier rename or refactor lands
- **THEN** the dev-tuning-ON build in the gate compiles the dev-guarded handlers
- **AND** any identifier missed in an inactive preprocessor branch is caught locally

