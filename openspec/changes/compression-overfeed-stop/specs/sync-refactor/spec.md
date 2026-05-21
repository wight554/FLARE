## MODIFIED Requirements

### Requirement: Type-D standalone buffer control is a hysteretic relay

The controller SHALL drive the active-lane feed as a two-level / hysteretic
relay law in standalone Sync-Feedback Sensor type D mode
(`BUF_SENSOR_TYPE == 0`, D=0), not a continuous PI loop on a dead-reckoned
position. Per FLARE polarity (negative reserve target,
REFILL effort in TENSION, RELIEVE in COMPRESSION), `BUF_TENSION` is the
empty/starved side and `BUF_COMPRESSION` is the full reserve side. The relay
law SHALL command a strong fixed catch-up rate (off the baseline control
floor) while TENSION is engaged, a **true zero feed** while COMPRESSION is
engaged (so no filament is added to a full buffer; the extruder draws the
buffer off the wall), and a demand-tracking rate
(`extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC`, clamped to `[SYNC_MIN, baseline
floor]`) while in NEUTRAL. The NEUTRAL rate MUST track
extruder demand, not the fixed baseline, so the buffer drifts slowly
instead of slamming the full wall. The flip OUT of COMPRESSION MUST NOT depend
on MMU feed travel (it keys on the physical NEUTRAL crossing driven by extruder
draw), so a zero COMPRESSION feed cannot freeze the relay. The existing ramp,
rate clamp, fast-brake and relief logic MUST still apply; the legacy
compression floor MUST be skipped in relay mode (it inherited the old
empty/full assumption).

#### Scenario: TENSION switch engaged

- **WHEN** the buffer is in `BUF_TENSION` (empty) in type-D standalone mode
- **THEN** feed targets the strong fixed catch-up rate so the buffer
  refills regardless of the estimator

#### Scenario: COMPRESSION switch engaged

- **WHEN** the buffer is in `BUF_COMPRESSION` (full reserve) in type-D
  standalone mode
- **THEN** feed is commanded to true zero so no filament is added to the full
  buffer, and the extruder draw pulls the buffer off the wall

#### Scenario: COMPRESSION with extruder idle (purge pause)

- **WHEN** the buffer is pinned in `BUF_COMPRESSION` in type-D mode and the
  extruder is not drawing (e.g. a purge has ended)
- **THEN** the MMU adds no further filament (zero feed), so buffer pressure
  does not build past the switch into the bowden

#### Scenario: NEUTRAL band tracks demand with a full-reserve lean

- **WHEN** the buffer is in `BUF_NEUTRAL` in type-D standalone mode
- **THEN** feed targets `extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC` (clamped
  to the baseline floor), so it matches consumption with a gentle
  full/COMPRESSION lean and the buffer drifts slowly rather than slamming a
  wall

#### Scenario: Flip out of COMPRESSION with zero feed

- **WHEN** the buffer is in `BUF_COMPRESSION` with zero MMU feed and the
  extruder draw moves the arm back across the NEUTRAL switch
- **THEN** the relay flips out of COMPRESSION on the physical crossing
  regardless of `relay_min_flip_mm`, i.e. the zero feed does not deadlock the
  relay

#### Scenario: Type-P analog mode unchanged

- **WHEN** `BUF_SENSOR_TYPE != 0` (type P analog, P=1)
- **THEN** the relay override does not apply and prior control behavior is
  byte-identical

## ADDED Requirements

### Requirement: Type-D compression relief is overfill-budgeted

The controller SHALL stop sync feed (enter `RELIEF_PAUSE`) once a small bounded
overfill is reached while the buffer is pinned in `BUF_COMPRESSION` and not
relieving, rather than only after a fixed blind dwell timer. While pinned in
COMPRESSION with the virtual position at or deepening past `-threshold` and the
accumulated relieve effort exceeding a small budget (on the order of 1-2 mm),
the controller SHALL enter `RELIEF_PAUSE` and emit the `RELIEF_PAUSE` event.
This caps the filament force-fed into a full buffer to the budget. Behavior is
gated to `BUF_SENSOR_TYPE == 0`; type-P analog relief is unchanged. The normal
relay limit cycle, which touches COMPRESSION briefly and leaves it via extruder
draw before the budget accrues, MUST NOT trip this early relief.

#### Scenario: Pinned compression with no relief trips fast

- **WHEN** the buffer is pinned in `BUF_COMPRESSION` in type-D mode, the virtual
  position is not recovering, and the accumulated relieve effort passes the
  overfill budget
- **THEN** the controller enters `RELIEF_PAUSE` within that budget (a small
  overfill), not after the multi-second blind timer

#### Scenario: Normal compression touch does not trip relief

- **WHEN** the buffer touches `BUF_COMPRESSION` during the normal relay limit
  cycle and the extruder draw pulls it back across NEUTRAL before the overfill
  budget accrues
- **THEN** no early `RELIEF_PAUSE` is entered and the cycle continues

#### Scenario: Type-P analog relief unchanged

- **WHEN** `BUF_SENSOR_TYPE != 0` (type P analog)
- **THEN** the existing relief behavior is unchanged
