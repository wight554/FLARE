## MODIFIED Requirements

### Requirement: Type-D standalone buffer control is a hysteretic relay

The controller SHALL drive the active-lane feed as a two-level / hysteretic
relay law in standalone Sync-Feedback Sensor type D mode
(`BUF_SENSOR_TYPE == 0`, D=0), not a continuous PI loop on a dead-reckoned
position. Per FLARE polarity (negative reserve target,
REFILL effort in TENSION, RELIEVE in COMPRESSION), `BUF_TENSION` is the
empty/starved side and `BUF_COMPRESSION` is the full reserve side. The relay
law SHALL command a strong fixed catch-up rate (off the baseline control
floor) while TENSION is engaged, a **true zero feed (0)** while COMPRESSION is
engaged so no filament is pushed into a full buffer (the output `SYNC_MIN`
clamp MUST be bypassed for COMPRESSION so feed actually reaches 0; the extruder
draw pulls the buffer off the wall and recovery uses the existing relieve /
`SYNC_AUTO_STOP_MS` path), and a demand-tracking rate
(`extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC`, clamped to `[SYNC_MIN, baseline
floor]`) while in NEUTRAL. The NEUTRAL rate MUST track
extruder demand, not the fixed baseline, so the buffer drifts slowly
instead of slamming the full wall. The existing ramp, rate clamp,
fast-brake and relief logic MUST still apply; the legacy compression floor
MUST be skipped in relay mode (it inherited the old empty/full assumption).

#### Scenario: TENSION switch engaged

- **WHEN** the buffer is in `BUF_TENSION` (empty) in type-D standalone mode
- **THEN** feed targets the strong fixed catch-up rate so the buffer
  refills regardless of the estimator

#### Scenario: COMPRESSION switch engaged

- **WHEN** the buffer is in `BUF_COMPRESSION` (full reserve) in type-D
  standalone mode
- **THEN** feed is commanded to true zero (not `SYNC_MIN`), so no filament is
  added to the full buffer and the extruder draws the buffer off the wall

#### Scenario: End of feed does not over-fill

- **WHEN** the extruder stops drawing while the buffer is in `BUF_COMPRESSION`
  in type-D mode
- **THEN** the MMU feed is 0 (not `SYNC_MIN` forward), so the buffer does not
  deepen past the switch toward the physical wall while waiting to recover

#### Scenario: NEUTRAL band tracks demand with a full-reserve lean

- **WHEN** the buffer is in `BUF_NEUTRAL` in type-D standalone mode
- **THEN** feed targets `extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC` (clamped
  to the baseline floor), so it matches consumption with a gentle
  full/COMPRESSION lean and the buffer drifts slowly rather than slamming a
  wall

#### Scenario: Zero compression feed does not deadlock the relay

- **WHEN** the buffer is in `BUF_COMPRESSION` with zero MMU feed and the
  extruder draw moves the arm back across the NEUTRAL switch
- **THEN** the relay flips out of COMPRESSION on the physical crossing
  (`relay_min_flip_mm` defaults to 0, time-based), i.e. zero feed does not
  freeze the relay

#### Scenario: Type-P analog mode unchanged

- **WHEN** `BUF_SENSOR_TYPE != 0` (type P analog, P=1)
- **THEN** the relay override does not apply and prior control behavior is
  byte-identical
