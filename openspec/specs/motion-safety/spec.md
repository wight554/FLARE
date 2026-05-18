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

### Requirement: Terminal jam paths enter non-destructive FAULT_HOLD

Hard-wall critical and advance-dwell stop SHALL transition the sync
controller to `SYNC_FAULT_HOLD` instead of calling destructive
`sync_disable(true)`. Entry MUST stop motion (`sync_current_sps = 0`) and
MUST NOT reset the extruder estimator, drift observer, sigma/confidence, or
reserve integrator. Each path SHALL emit a `SYNC FAULT_HOLD` event.

#### Scenario: Hard-wall critical triggers FAULT_HOLD

- **WHEN** the trailing wall is critical (virtual endstop, push above
  `SYNC_TRAILING_HARD_PUSH_MM_S`, wall time below
  `SYNC_TRAILING_HARD_WALL_MS`)
- **THEN** the controller enters `SYNC_FAULT_HOLD`, motion stops, the
  estimator/drift/sigma state is preserved, and a `SYNC FAULT_HOLD` event
  is emitted

#### Scenario: Advance-dwell stop triggers FAULT_HOLD

- **WHEN** the buffer is pinned at ADVANCE for at least
  `SYNC_ADVANCE_DWELL_STOP_MS`
- **THEN** the controller enters `SYNC_FAULT_HOLD`, motion stops, the
  estimator/drift/sigma state is preserved, and a `SYNC FAULT_HOLD` event
  is emitted

### Requirement: FAULT_HOLD auto-recovers without host

While in `SYNC_FAULT_HOLD` the controller SHALL remain motion-stopped until
`CONF_SYNC_FAULT_HOLD_RECOVERY_MS` has elapsed since entry, then transition
to `SYNC_OFF` and emit `SYNC FAULT_HOLD_RECOVERY`, allowing normal auto-arm
to re-enter `SYNC_ACTIVE` reusing the preserved estimator subject to
existing freshness aging. No host command SHALL be required to recover.

#### Scenario: Auto-recovery after stable interval

- **WHEN** the controller has been in `SYNC_FAULT_HOLD` for at least
  `CONF_SYNC_FAULT_HOLD_RECOVERY_MS`
- **THEN** it transitions to `SYNC_OFF`, emits `SYNC FAULT_HOLD_RECOVERY`,
  and a subsequent auto-arm re-enters `SYNC_ACTIVE` without discarding the
  preserved estimator

#### Scenario: No premature recovery

- **WHEN** less than `CONF_SYNC_FAULT_HOLD_RECOVERY_MS` has elapsed since
  FAULT_HOLD entry
- **THEN** the controller stays motion-stopped and does not re-arm
