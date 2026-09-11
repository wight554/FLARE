## ADDED Requirements

### Requirement: Baseline and bias sourced from flow schedule

The sync controller SHALL obtain its baseline and trailing-bias by
evaluating the flow-keyed schedule at the live `extruder_est_sps`,
replacing the single-scalar read, with a length-1 schedule as the exact
degenerate fallback. This SHALL NOT alter the full-bias invariant: the
schedule only supplies inputs that `SYNC_ACTIVE` reserve-target control
already consumes; reserve target, `reserve_correction`, `zone_bias`,
soft-wall trim, and collapse ramp SHALL be unchanged.

#### Scenario: Active params follow flow

- **WHEN** live flow moves across schedule segments during `SYNC_ACTIVE`
- **THEN** the baseline/bias inputs update per the schedule while
  reserve-target control logic is unchanged

#### Scenario: Full-bias invariant preserved

- **WHEN** the schedule is evaluated for any flow
- **THEN** reserve target / `reserve_correction` / `zone_bias` / soft-wall
  trim / collapse ramp behavior is identical to before this change

### Requirement: Live learner ratchets within the active flow segment

The disciplined live baseline learner SHALL remain ephemeral, up-only,
non-persistent, and disciplined (multi-cycle, variance-reject, cooldown,
`SYNC_ACTIVE`-gated) but SHALL ratchet the baseline of the currently
active flow segment rather than a global scalar. On reboot or settings
reload the schedule SHALL reload from config (offline authority); the
live segment delta SHALL be lost.

#### Scenario: Segment-scoped ratchet

- **WHEN** the disciplined learner accepts an update while flow is in a
  given segment
- **THEN** only that segment's baseline ratchets up, not a global scalar

#### Scenario: Ephemeral on reboot

- **WHEN** the device reboots or reloads settings
- **THEN** the schedule is restored from config and any live segment
  ratchet is discarded
