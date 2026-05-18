## ADDED Requirements

### Requirement: Two-switch standalone buffer control is a hysteretic relay

In standalone two-switch mode (`BUF_SENSOR_TYPE == 0`) the controller SHALL
drive the active-lane feed as a hysteretic relay on the buffer switch
state, not a continuous PI loop on a dead-reckoned position. The relay
SHALL command a catch-up rate while the TRAILING switch is engaged, a
back-off rate while the ADVANCE switch is engaged, and a trailing-biased
hold rate (strictly below the catch-up/MID-neutral baseline) while in MID,
each derived from the baseline control floor. The existing ramp, rate
clamp, trailing floor, fast-brake and relief logic MUST still apply.

#### Scenario: TRAILING switch engaged

- **WHEN** the buffer is in `BUF_TRAILING` in two-switch standalone mode
- **THEN** feed targets the catch-up rate so the buffer refills

#### Scenario: ADVANCE switch engaged

- **WHEN** the buffer is in `BUF_ADVANCE` in two-switch standalone mode
- **THEN** feed targets the back-off rate so the extruder drains the buffer

#### Scenario: MID band leans trailing

- **WHEN** the buffer is in `BUF_MID` in two-switch standalone mode
- **THEN** feed targets a hold rate below neutral so the equilibrium
  drifts gently toward TRAILING (never-ADVANCE lean), producing a slow
  shallow limit cycle

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
