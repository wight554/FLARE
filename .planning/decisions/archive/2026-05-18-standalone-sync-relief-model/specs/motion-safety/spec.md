## ADDED Requirements

### Requirement: Non-Destructive Jam Relief
Trailing/overfull and hard-wall handling SHALL NOT discard the extruder
estimator, drift observer, or sigma/confidence state. Destructive reset SHALL
be reserved for true off transitions only.

#### Scenario: Overfull does not wipe local model
- **WHEN** a sustained trailing/overfull condition is reached
- **THEN** the controller pauses assist non-destructively
- **AND** the extruder estimator and drift/sigma state survive the event

#### Scenario: Hard wall stops motor but preserves model
- **WHEN** the hard-wall critical condition is reached
- **THEN** the motor stops
- **AND** estimator/drift/sigma state is preserved for conservative recovery

### Requirement: Under-Extrusion Direction Priority
The controller SHALL never pause local assist while the buffer is empty-side
(`BUF_ADVANCE`), and SHALL prioritize avoiding under-extrusion over relieving
overfull.

#### Scenario: Empty side always fed
- **WHEN** the buffer is in `BUF_ADVANCE`
- **THEN** assist is not paused or reduced for relief purposes
- **AND** the controller feeds within configured limits
