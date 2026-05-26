## MODIFIED Requirements

### Requirement: Retract Assist Gate Is Quiet And Non-Destructive
The host `BL` buffer-lock command SHALL place the controller in
`SYNC_RETRACT_ASSIST` (the buffer-lock lifecycle state), with normal
closed-loop sync off, post-print negative sync suppressed, controller state
preserved, and learning paused. While locked the gate SHALL NOT react to
buffer changes; on raw departure from the armed extreme it SHALL transition
into an instant-slam catch sub-state. The gate MUST NOT destructively reset
estimator, drift, sigma, or reserve integrator state. The legacy `RA:1` /
`RA:0` host commands SHALL remain accepted as aliases for `BL:T` and `BS`
respectively for backward compatibility.

#### Scenario: Host requests buffer lock
- **WHEN** the host sends `BL:T` (or the legacy `RA:1`)
- **THEN** the controller enters `SYNC_RETRACT_ASSIST`
- **AND** closed-loop sync and post-print negative sync stop
- **AND** estimator/drift/sigma state is preserved

#### Scenario: Locked sub-state ignores idle buffer changes
- **WHEN** the lane is locked at the armed extreme
- **AND** the buffer raw state is the armed extreme (no external force)
- **THEN** `BUF_COMPRESSION`, `BUF_TENSION`, and `BUF_NEUTRAL` do not start
  firmware buffer-following motion

#### Scenario: Catch sub-state engages on raw departure
- **WHEN** the lane is locked at `BUF_TENSION`
- **AND** the raw buffer state leaves `BUF_TENSION` due to external force
- **THEN** the controller transitions to the catch sub-state on the same tick
- **AND** the active lane is driven in the mirror direction at
  `min(GLOBAL_MAX_SPS, SYNC_MAX_SPS)` with no PD ramp on the first step

#### Scenario: Host clears buffer lock
- **WHEN** the host sends `BS` (or the legacy `RA:0`)
- **THEN** the controller leaves `SYNC_RETRACT_ASSIST`
- **AND** immediately attempts post-print negative sync once so an already
  compressed buffer can start reversing without an idle dwell
