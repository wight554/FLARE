## ADDED Requirements

### Requirement: Type-D RELIEF_PAUSE re-arms without a full drain

The controller SHALL re-arm sync from `RELIEF_PAUSE` to `SYNC_ACTIVE` when the
buffer recovers to `BUF_NEUTRAL` (e.g. via the reverse-relieve service), not only
when it reaches `BUF_TENSION`, in type-D standalone mode (`BUF_SENSOR_TYPE == 0`).
On that re-arm the controller SHALL reseed the virtual position toward the
reserve target and bootstrap the feed rate (as the FAULT_HOLD recovery does), so
a resuming print does not have to drain the buffer to empty before the MMU feeds
again. Type-P analog behavior MUST be unchanged.

#### Scenario: High-flow resume after a relief pause

- **WHEN** sync has entered `RELIEF_PAUSE`, the relieve service has brought the
  buffer to `BUF_NEUTRAL`, and the print resumes drawing filament
- **THEN** sync re-arms from NEUTRAL and feeds, rather than staying paused until
  the extruder drains the buffer to `BUF_TENSION`

#### Scenario: Idle after a relief pause

- **WHEN** the buffer is relieved to `BUF_NEUTRAL` while the printer stays idle
- **THEN** the buffer rests at NEUTRAL with no feed and no oscillation (re-arm
  occurs on the next genuine draw)

#### Scenario: Type-P analog mode unchanged

- **WHEN** `BUF_SENSOR_TYPE != 0`
- **THEN** the RELIEF_PAUSE recovery behavior is unchanged

### Requirement: Type-D estimator does not hard-overwrite from a modeled transition

In type-D standalone mode the velocity estimator (`extruder_est_sps`) SHALL NOT
be fully replaced by a value derived from a *modeled* (assumed full-span)
TENSION→COMPRESSION transition. The estimator update for that transition SHALL be
blended or rate-capped so a single short or partial transition cannot spike the
estimate and over-feed the subsequent NEUTRAL band. The TENSION catch-up path
(which refills regardless of the estimator) MUST still apply.

#### Scenario: Fast TENSION→COMPRESSION disturbance

- **WHEN** the buffer crosses TENSION→COMPRESSION quickly (short dwell) in type-D
  mode
- **THEN** `extruder_est_sps` changes by a bounded amount (blended/capped), not a
  full overwrite, so the next NEUTRAL feed does not spike and drive the buffer
  back into compression
