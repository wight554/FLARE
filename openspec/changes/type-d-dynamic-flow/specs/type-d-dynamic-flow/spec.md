## ADDED Requirements

### Requirement: Direction-asymmetric type-D feed response

For `BUF_SENSOR_TYPE == 0`, the firmware SHALL apply asymmetric response
aggressiveness by direction: a feed lean that drives the buffer *toward* the
TENSION rail (corrective lean while in `BUF_NEUTRAL`) SHALL be gentle and
proportional, while recovery *from* a TENSION touch SHALL be aggressive. This
reflects the asymmetric cost (TENSION = print defect, COMPRESSION = benign
recalibration noise). This SHALL NOT alter analog type-P behavior.

#### Scenario: Gentle lean toward tension, aggressive recovery from it

- **WHEN** the type-D buffer is drifting toward TENSION in `BUF_NEUTRAL`
- **THEN** the corrective feed lean is proportional and bounded (no slam)
- **WHEN** the type-D buffer has touched the TENSION switch
- **THEN** the recovery response is aggressive (full attack), overshoot toward
  COMPRESSION being the safe direction

### Requirement: Proactive NEUTRAL soft-wall lean corrects slow drift

For `BUF_SENSOR_TYPE == 0`, while in `BUF_NEUTRAL` the firmware SHALL add a feed
lean proportional to the dead-reckoned tension-side excursion of `g_buf_pos`
past a deadband, applied through the volatile one-sided neutral trim, and SHALL
decay that lean toward zero as the buffer recovers toward the reserve target.
The lean SHALL be bounded by the existing trim clamp and SHALL be disabled when
its gain knob is zero. This proactive lean SHALL only raise feed (never drive
toward TENSION) and SHALL NOT alter analog type-P feedforward.

#### Scenario: Slow tension drift is corrected without constant clicks

- **WHEN** the dead-reckoned `g_buf_pos` drifts toward TENSION during a steady
  slow print at a low `SYNC_MIN_RATE`
- **THEN** the firmware raises NEUTRAL feed proportional to the excursion
- **AND** the lean decays back toward zero once the buffer recovers, so feed is
  only leaned while actually drifting

#### Scenario: Soft-wall lean is disabled by zero gain

- **WHEN** the soft-wall gain knob is `0`
- **THEN** no proactive NEUTRAL lean is applied (behavior reverts to the trim's
  TENSION-touch backstop only)

### Requirement: Tension-crossing EST aggressiveness scales with crossing velocity

For `BUF_SENSOR_TYPE == 0`, on a crossing into `BUF_TENSION` the firmware SHALL
scale the `extruder_est_sps` update aggressiveness by the crossing velocity
(drain rate). A fast crossing (a sharp demand step) SHALL update the estimate
toward the measured demand `mmu_feed + drain_rate` at high blend weight so the
recovered NEUTRAL feed matches demand on the first touch; a slow crossing SHALL
retain the gentle softened fallback so slow drift does not overshoot. This SHALL
NOT alter the analog type-P estimator.

#### Scenario: Fast step converges in a single tension touch

- **WHEN** the type-D buffer crosses into TENSION with a high crossing velocity
  (a sharp slow→fast demand step)
- **THEN** `extruder_est_sps` is updated toward `mmu_feed + drain_rate` at full
  attack
- **AND** the post-recovery NEUTRAL feed matches the new demand, so the buffer
  does not re-drain into a second TENSION touch

#### Scenario: Slow crossing keeps the gentle fallback

- **WHEN** the type-D buffer crosses into TENSION with a low crossing velocity
  (slow drift)
- **THEN** the estimate update uses the softened gentle path (no aggressive snap)
- **AND** the buffer does not overshoot into COMPRESSION

### Requirement: Feed floor is not the primary anti-tension lever

For `BUF_SENSOR_TYPE == 0`, slow-drift anti-tension protection SHALL be provided
by the proactive proportional lean, not by a high `SYNC_MIN_RATE` feed floor. The
shipped default `SYNC_MIN_RATE` SHALL be a quiet (low) value so real prints are
not forced into constant COMPRESSION clicking. `SYNC_MIN_RATE` SHALL remain
operator-tunable for those who prefer the loud zero-fast-step-skip behavior, and
TUNING.md SHALL document the trade-off.

#### Scenario: Quiet default with drift still corrected

- **WHEN** the firmware ships its default `SYNC_MIN_RATE`
- **THEN** the value is low (quiet on slow sections)
- **AND** slow tension drift is still corrected by the proactive soft-wall lean,
  not by the floor

#### Scenario: Operator may restore the loud zero-skip floor

- **WHEN** an operator raises `SYNC_MIN_RATE` toward the fast-segment rate
- **THEN** fast-step TENSION touches are eliminated at the cost of constant
  COMPRESSION clicking (documented as the loud zero-skip mode)
