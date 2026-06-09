# Delta: live-tuner

## ADDED Requirements

### Requirement: Event parsing matches firmware wire format

The tuner SHALL parse firmware events in colon format (`EV:TYPE:DATA`), the
only format `cmd_event` emits. Safety reactions keyed on events
(`SYNC:FAULT_HOLD`, `SYNC:TENSION_RISK_HIGH`, `BUF:EST_FALLBACK`) MUST fire on
real wire lines, and a unit test MUST pin the expected format against
representative firmware lines.

#### Scenario: FAULT_HOLD reaction

- **WHEN** serial stream delivers `EV:SYNC:FAULT_HOLD`
- **THEN** tuner registers the fault-hold condition and applies its guard behavior

#### Scenario: Wire-format regression test

- **WHEN** unit suite runs
- **THEN** event-pattern tests feed literal firmware-format lines (`EV:SYNC:TENSION_RISK_HIGH`, `EV:BUF:EST_FALLBACK`)
- **AND** each watched event matches
