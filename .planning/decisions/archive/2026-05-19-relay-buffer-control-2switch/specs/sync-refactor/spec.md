## ADDED Requirements

### Requirement: Type-D standalone buffer control is a hysteretic relay

The controller SHALL drive the active-lane feed as a two-level / hysteretic
relay law in standalone Sync-Feedback Sensor type D mode
(`BUF_SENSOR_TYPE == 0`, D=0), not a continuous PI loop on a dead-reckoned
position. Per FLARE polarity (negative reserve target,
REFILL effort in TENSION, RELIEVE in COMPRESSION), `BUF_TENSION` is the
empty/starved side and `BUF_COMPRESSION` is the full reserve side. The relay
law SHALL command a strong fixed catch-up rate (off the baseline control
floor) while TENSION is engaged, stop feed (clamp to `SYNC_MIN_SPS`) while
COMPRESSION is engaged, and a demand-tracking rate
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
- **THEN** feed stops (clamps to `SYNC_MIN_SPS`) so the extruder draws
  the buffer off the full wall

#### Scenario: NEUTRAL band tracks demand with a full-reserve lean

- **WHEN** the buffer is in `BUF_NEUTRAL` in type-D standalone mode
- **THEN** feed targets `extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC` (clamped
  to the baseline floor), so it matches consumption with a gentle
  full/COMPRESSION lean and the buffer drifts slowly rather than slamming a
  wall

#### Scenario: Type-P analog mode unchanged

- **WHEN** `BUF_SENSOR_TYPE != 0` (type P analog, P=1)
- **THEN** the relay override does not apply and prior control behavior is
  byte-identical

### Requirement: Normal switch contact does not trigger FAULT_HOLD

The controller SHALL NOT trigger FAULT_HOLD on normal COMPRESSION or TENSION
switch contact in type-D standalone mode, because switch contact is the
relay-law control signal. The tension-dwell FAULT_HOLD and the
compression-wall-critical FAULT_HOLD SHALL be gated to type-P analog mode
(`BUF_SENSOR_TYPE != 0`, P=1). Genuine idle/runout handling via the existing
relief and continuous-compression auto-stop paths MUST remain unchanged.

#### Scenario: Sustained COMPRESSION contact in relay mode

- **WHEN** the COMPRESSION switch is engaged in type-D standalone mode,
  even with a hard/fast wall contact
- **THEN** no FAULT_HOLD is entered; the relay stop state handles the full
  buffer

#### Scenario: Sustained TENSION contact in relay mode

- **WHEN** the TENSION switch is engaged past the tension-dwell timeout in
  type-D standalone mode
- **THEN** no FAULT_HOLD is entered; the relay catch-up refills the buffer

#### Scenario: Type-P analog fault behavior preserved

- **WHEN** `BUF_SENSOR_TYPE != 0` (type P analog, P=1)
- **THEN** the tension-dwell and compression-wall-critical FAULT_HOLD
  behavior is unchanged
