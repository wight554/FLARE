## ADDED Requirements

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
