## MODIFIED Requirements

### Requirement: Retract Assist Gate Is Quiet And Non-Destructive
The host `BL` buffer-lock command SHALL place the controller in
`SYNC_RETRACT_ASSIST` (the buffer-lock lifecycle state), with normal
closed-loop sync off, post-print negative sync suppressed, controller state
preserved, and learning paused. While locked the gate SHALL NOT react to
buffer changes; on raw departure from the armed extreme it SHALL transition
into the catch sub-state. The gate MUST NOT destructively reset
estimator, drift, sigma, or reserve integrator state. The legacy `RA:1` /
`RA:0` host commands and the `RA` status field SHALL be removed; no alias
is provided.

The catch sub-state rate contract is owned by the `buffer-state-lock`
capability ("Instant-Slam Catch With Asymmetric Safety"): an error-proportional
rate servo bounded by `GLOBAL_MAX_SPS`, approached through the pull-in ramp.
This capability SHALL NOT restate a conflicting bound.

#### Scenario: Host requests buffer lock
- **WHEN** the host sends `BL:T`
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
- **AND** `EV:BL:BREAK` is emitted
- **AND** the active lane is driven in the mirror direction under the
  `buffer-state-lock` catch rate servo, bounded by `GLOBAL_MAX_SPS`

#### Scenario: Host clears buffer lock
- **WHEN** the host sends `BS`
- **THEN** the controller leaves `SYNC_RETRACT_ASSIST`
- **AND** immediately attempts post-print negative sync once so an already
  compressed buffer can start reversing without an idle dwell

#### Scenario: Legacy RA command is rejected
- **WHEN** the host sends `RA:1` or `RA:0`
- **THEN** the controller replies `ER:CMD`
- **AND** no state change occurs
