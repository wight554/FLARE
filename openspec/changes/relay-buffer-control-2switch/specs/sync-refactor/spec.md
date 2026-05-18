## ADDED Requirements

### Requirement: Two-switch standalone buffer control is a hysteretic relay

In standalone two-switch mode (`BUF_SENSOR_TYPE == 0`) the controller SHALL
drive the active-lane feed as a hysteretic relay on the buffer switch
state, not a continuous PI loop on a dead-reckoned position. Per FLARE polarity (negative reserve target, REFILL effort in ADVANCE,
RELIEVE in TRAILING), `BUF_ADVANCE` is the empty/starved side and
`BUF_TRAILING` is the full reserve side. The relay SHALL command a
catch-up rate while ADVANCE is engaged (empty -> refill), a back-off rate
while TRAILING is engaged (full -> let the extruder draw it down), and a
gentle overfeed hold rate (above neutral) while in MID, each derived from
the baseline control floor. The existing ramp, rate clamp, fast-brake and
relief logic MUST still apply; the legacy trailing floor MUST be skipped
in relay mode (it assumes trailing = empty).

#### Scenario: ADVANCE switch engaged

- **WHEN** the buffer is in `BUF_ADVANCE` (empty) in two-switch standalone
  mode
- **THEN** feed targets the catch-up rate so the buffer refills

#### Scenario: TRAILING switch engaged

- **WHEN** the buffer is in `BUF_TRAILING` (full reserve) in two-switch
  standalone mode
- **THEN** feed targets the back-off rate so the extruder draws the
  buffer down

#### Scenario: MID band leans to the full reserve side

- **WHEN** the buffer is in `BUF_MID` in two-switch standalone mode
- **THEN** feed targets a hold rate above neutral so the equilibrium
  drifts gently toward the full/TRAILING reserve side and never reaches
  ADVANCE (never starve), producing a slow shallow limit cycle

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
