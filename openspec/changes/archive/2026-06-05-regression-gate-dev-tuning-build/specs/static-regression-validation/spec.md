## ADDED Requirements

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
