## ADDED Requirements

### Requirement: Two-switch standalone buffer control is a hysteretic relay

In standalone two-switch mode (`BUF_SENSOR_TYPE == 0`) the controller SHALL
drive the active-lane feed as a hysteretic relay on the buffer switch
state, not a continuous PI loop on a dead-reckoned position. Per FLARE polarity (negative reserve target, REFILL effort in ADVANCE,
RELIEVE in TRAILING), `BUF_ADVANCE` is the empty/starved side and
`BUF_TRAILING` is the full reserve side. The relay SHALL command a strong
fixed catch-up rate (off the baseline control floor) while ADVANCE is
engaged, stop feed (clamp to `SYNC_MIN_SPS`) while TRAILING is engaged, and
a demand-tracking rate (`extruder_est_sps * SYNC_RELAY_MID_FRAC`, clamped
to `[SYNC_MIN, baseline floor]`) while in MID. The MID rate MUST track
extruder demand, not the fixed baseline, so the buffer drifts slowly
instead of slamming the full wall. The existing ramp, rate clamp,
fast-brake and relief logic MUST still apply; the legacy trailing floor
MUST be skipped in relay mode (it assumes trailing = empty).

#### Scenario: ADVANCE switch engaged

- **WHEN** the buffer is in `BUF_ADVANCE` (empty) in two-switch standalone
  mode
- **THEN** feed targets the strong fixed catch-up rate so the buffer
  refills regardless of the estimator

#### Scenario: TRAILING switch engaged

- **WHEN** the buffer is in `BUF_TRAILING` (full reserve) in two-switch
  standalone mode
- **THEN** feed stops (clamps to `SYNC_MIN_SPS`) so the extruder draws
  the buffer off the full wall

#### Scenario: MID band tracks demand with a full-reserve lean

- **WHEN** the buffer is in `BUF_MID` in two-switch standalone mode
- **THEN** feed targets `extruder_est_sps * SYNC_RELAY_MID_FRAC` (clamped
  to the baseline floor), so it matches consumption with a gentle
  full/TRAILING lean and the buffer drifts slowly rather than slamming a
  wall

#### Scenario: Analog mode unchanged

- **WHEN** `BUF_SENSOR_TYPE != 0`
- **THEN** the relay override does not apply and prior control behavior is
  byte-identical

### Requirement: Normal switch contact does not trigger FAULT_HOLD

The controller SHALL NOT trigger FAULT_HOLD on normal TRAILING or ADVANCE
switch contact in two-switch standalone mode, because switch contact is the
relay control signal. The advance-dwell FAULT_HOLD and the
trailing-wall-critical FAULT_HOLD SHALL be gated to analog mode
(`BUF_SENSOR_TYPE != 0`). Genuine idle/runout handling via the existing
relief and continuous-trailing auto-stop paths MUST remain unchanged.

#### Scenario: Sustained TRAILING contact in relay mode

- **WHEN** the TRAILING switch is engaged in two-switch standalone mode,
  even with a hard/fast wall contact
- **THEN** no FAULT_HOLD is entered; the relay catch-up and trailing floor
  handle the empty buffer

#### Scenario: Sustained ADVANCE contact in relay mode

- **WHEN** the ADVANCE switch is engaged past the advance-dwell timeout in
  two-switch standalone mode
- **THEN** no FAULT_HOLD is entered; the relay back-off drains the buffer

#### Scenario: Analog mode fault behavior preserved

- **WHEN** `BUF_SENSOR_TYPE != 0`
- **THEN** the advance-dwell and trailing-wall-critical FAULT_HOLD
  behavior is unchanged
