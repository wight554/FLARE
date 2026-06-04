# type-d-dynamic-flow Specification

## Purpose
Captures dynamic type-D relay feed behavior that helps the two-switch buffer recover demand without a mid-band position signal.

## Requirements
### Requirement: A tension touch slams a decaying recovery feed floor

For `BUF_SENSOR_TYPE == 0`, a crossing into `BUF_TENSION` SHALL set a recovery
feed floor `SYNC_TENSION_RECOVERY_FLOOR` (≈ the fast-segment / catchup rate) and
SHALL apply that floor as a lower bound on the `BUF_NEUTRAL` relay feed, decaying
the floor to zero over `SYNC_TENSION_RECOVERY_MS`. The floor SHALL be a feed-side
term independent of `extruder_est_sps` so it is not pulled back down by the
NEUTRAL-fill / COMPRESSION-drain estimator samples during the recovery cycle.
This holds NEUTRAL feed high through the recovery window so the buffer does not
re-drain into a second/third TENSION touch (the burst), collapsing each
demand-step event to a single tension touch. The floor SHALL hand off to
`extruder_est_sps` as it decays (by which time the estimator has caught the new
demand). The recovery floor SHALL apply only in `BUF_NEUTRAL`: when its aggressive
feed overshoots the buffer into `BUF_COMPRESSION`, the floor SHALL stop applying
(it is NEUTRAL-only) so the existing COMPRESSION gated-drain / true-stop and
relieve budget stabilize the buffer off the rail — the floor SHALL NOT fight the
drain. A brief COMPRESSION-side excursion (one or a few transient touches) during
the recovery window is accepted as the cost of preventing the re-drain tension
burst, provided it stabilizes via the COMPRESSION path. This SHALL NOT alter
analog type-P.

The velocity-snap raise-only update of `extruder_est_sps` on the tension crossing
MAY remain as a benign monotonic-up nudge; the recovery floor is the mechanism
that collapses the burst.

#### Scenario: Burst collapses to a single tension touch

- **WHEN** a demand step drives the type-D buffer into TENSION
- **THEN** the recovery floor is set and bounds NEUTRAL feed high through the
  recovery window
- **AND** the buffer does not re-drain into further TENSION touches before the
  floor decays (the step produces one touch, not a burst)

#### Scenario: Overshoot into COMPRESSION stabilizes via the drain, not the floor

- **WHEN** the recovery floor's aggressive feed overshoots the buffer into
  `BUF_COMPRESSION`
- **THEN** the floor stops applying (it is NEUTRAL-only) and the existing
  COMPRESSION gated-drain / true-stop + relieve budget drain the buffer off the
  rail
- **AND** the buffer stabilizes back toward NEUTRAL without the floor fighting the
  drain (a brief COMPRESSION excursion is accepted)

#### Scenario: Recovery floor decays and is not held high between steps

- **WHEN** `SYNC_TENSION_RECOVERY_MS` elapses after a tension touch with no new
  touch
- **THEN** the floor has decayed to zero and NEUTRAL feed is governed by the
  estimator again (quiet between steps, unlike a static high `SYNC_MIN_RATE`)

#### Scenario: Floor is independent of the estimator

- **WHEN** NEUTRAL-fill or COMPRESSION-drain crossings lower `extruder_est_sps`
  during the recovery cycle
- **THEN** the recovery floor still bounds NEUTRAL feed (it is not an EST term),
  so the recovered feed stays high and the re-drain is prevented

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
