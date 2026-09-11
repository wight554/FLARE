## ADDED Requirements

### Requirement: Sensor and control law are named separately

Live prose SHALL name the sensor (Sync-Feedback Sensor type P/D)
separately from the control law. The dual-switch path's law SHALL be
referred to as the "type-D two-level / hysteretic relay control law" and
the analog path's law as the "type-P PD/EKF (reserve) control law";
Wiring shorthand and "relay" MUST NOT be used as if they named the sensor.

#### Scenario: Discrete control law named without conflation

- **WHEN** a live spec or doc describes the dual-switch controller
- **THEN** it names the type-D sensor and the two-level/relay law as
  distinct layers, not a single wiring/control shorthand sensor-feature

### Requirement: Vocabulary rollout is behavior-neutral

This change SHALL NOT alter any control logic, protocol token, config key,
or C symbol; it is prose and documented-contract only. The host build and
a captured status/event snapshot MUST be identical before and after.

#### Scenario: Faithful rollout gate

- **WHEN** the firmware is built and a status/event snapshot captured
  after this change
- **THEN** it is numerically identical to the pre-change snapshot
