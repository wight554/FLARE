# Motion Safety Specification

## Purpose
Durable contract for FLARE motor, filament, and task safety limits, extracted from `firmware/src/motion.c` and `BEHAVIOR.md`.

## Requirements

### Requirement: Dry Spin Protection
The system SHALL halt any spinning motor if no filament is detected at the intake and the buffer is not pulling.

#### Scenario: Filament Lost Mid-Task
- **WHEN** `TASK_FEED` or `TASK_LOAD_FULL` is active
- **AND** the `IN` sensor clears
- **AND** the buffer is not in `BUF_ADVANCE` (pulling a tail)
- **AND** this state persists for > 8 seconds
- **THEN** the motor stops and `FAULT:DRY_SPIN` is emitted
- **AND** automatic background restarts (sync or reload) are blocked until cleared by manual command or new filament insertion

### Requirement: Task Travel Limits
Automated tasks SHALL NOT spin indefinitely without hitting a physical checkpoint.

#### Scenario: Missing Sensor
- **WHEN** `TASK_LOAD_FULL` is running
- **AND** `LOAD_MAX_MM` travel distance is reached without triggering the completion state
- **THEN** the lane stops
- **AND** `LOAD_TIMEOUT` is emitted

### Requirement: Safe Autopreload
Autopreload SHALL only engage for freshly inserted filament and MUST leave the path clear for the other lane.

#### Scenario: Fresh Insertion
- **WHEN** the lane is `IDLE` and its `OUT` sensor is clear
- **AND** `AUTO_PRELOAD` is enabled
- **AND** the `IN` sensor rises
- **THEN** `TASK_AUTOLOAD` starts, drives until `OUT` triggers, then retracts by `RETRACT_MM` to park the tip

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
