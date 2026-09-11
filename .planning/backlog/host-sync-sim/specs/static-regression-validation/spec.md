## ADDED Requirements

### Requirement: Host simulation build and scenario suite
The regression gate MUST build the host sync simulation and execute its scenario
suite as part of the standard validation run.

The gate MUST fail when the simulation fails to build, when any scenario fails its
assertions, or when any scenario violates a global invariant.

Because the simulation compiles firmware sources directly, a compilation failure in
the simulation build MUST be reported as a firmware source defect rather than
skipped as an optional test.

#### Scenario: Running the regression gate builds and runs the simulation
- **WHEN** the operator runs `scripts/validate_regression.py`
- **THEN** the host simulation binary is built from the firmware sync sources
- **AND** every registered scenario executes
- **AND** any scenario failure or invariant violation fails the gate

#### Scenario: Simulation build break fails the gate
- **WHEN** a firmware source change breaks the host simulation build
- **THEN** the gate fails and reports the compilation error
- **AND** the failure is not treated as an optional or skippable test
