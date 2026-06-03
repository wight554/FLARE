## ADDED Requirements

### Requirement: Tension-crossing EST snaps on velocity and escalates on bursts

For `BUF_SENSOR_TYPE == 0`, a crossing into `BUF_TENSION` SHALL update
`extruder_est_sps` via two triggers. (1) When drain velocity is present, the
update SHALL scale aggressiveness by crossing velocity toward the measured demand
`mmu_feed + drain_rate` at high blend weight, so the recovered NEUTRAL feed
matches demand on the first touch. (2) When a tension touch occurs within a burst
window of the previous one (consecutive touches — the buffer is still under-fed,
which is the only signal available for pinned re-touches where drain velocity is
zero), the firmware SHALL escalate the estimate jump geometrically per
consecutive touch until tension clears, and SHALL reset that escalation once a
`BUF_NEUTRAL` dwell holds past the burst window. A single slow crossing with no
recent prior touch SHALL retain the gentle softened fallback so slow drift does
not overshoot. Aggressive recovery here is intentional: the tension-recovery
direction overshoots toward COMPRESSION (benign), unlike a NEUTRAL lean. This
SHALL NOT alter the analog type-P estimator.

#### Scenario: Fast step converges in a single tension touch

- **WHEN** the type-D buffer crosses into TENSION with a high crossing velocity
  (a sharp slow→fast demand step)
- **THEN** `extruder_est_sps` is updated toward `mmu_feed + drain_rate` at full
  attack
- **AND** the post-recovery NEUTRAL feed matches the new demand

#### Scenario: Pinned re-touch burst is escalated, not crept

- **WHEN** consecutive TENSION touches occur within the burst window (pinned
  re-touches with zero drain velocity)
- **THEN** the estimate jump escalates geometrically per touch until tension
  clears, instead of creeping up a fixed fraction per touch
- **AND** the escalation resets after a `BUF_NEUTRAL` dwell holds past the window

#### Scenario: Slow single crossing keeps the gentle fallback

- **WHEN** the type-D buffer crosses into TENSION with low velocity and no recent
  prior tension touch (slow drift)
- **THEN** the estimate update uses the softened gentle path (no snap, no
  escalation)
- **AND** the buffer does not overshoot into COMPRESSION

### Requirement: Slow-drift protection is the compression reserve bias, not the feed floor

For `BUF_SENSOR_TYPE == 0`, slow-print anti-tension protection SHALL be provided
by the compression-side reserve bias (`SYNC_RESERVE_PCT`), which parks the buffer
off the TENSION rail, NOT by a high `SYNC_MIN_RATE` feed floor. The shipped
default `SYNC_MIN_RATE` SHALL be a quiet (low) value so real prints are not forced
into constant COMPRESSION clicking. `SYNC_MIN_RATE` SHALL remain operator-tunable
for those who prefer the loud zero-fast-step-skip behavior, and TUNING.md SHALL
document the trade-off.

#### Scenario: Quiet default with slow drift handled by reserve bias

- **WHEN** the firmware ships its default `SYNC_MIN_RATE`
- **THEN** the value is low (quiet on slow sections)
- **AND** slow prints ride the compression-side reserve, not the TENSION rail, so
  no high floor is needed to prevent slow drift

#### Scenario: Operator may restore the loud zero-skip floor

- **WHEN** an operator raises `SYNC_MIN_RATE` toward the fast-segment rate
- **THEN** fast-step TENSION touches are eliminated at the cost of constant
  COMPRESSION clicking (documented as the loud zero-skip mode)
