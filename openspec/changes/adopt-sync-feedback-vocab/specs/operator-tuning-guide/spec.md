## ADDED Requirements

### Requirement: TUNING.md uses Sync-Feedback Sensor P/D vocabulary

TUNING.md SHALL use the Sync-Feedback Sensor vocabulary with Happy Hare
type codes (P, D; TO/CO noted as unimplemented) and SHALL document the
`BUF_SENSOR_TYPE` value contract (D=0, P=1) where sensor mode is
referenced, naming the sensor separately from the control law and not
using the legacy "PSF" alias.

#### Scenario: Dual/analog sections use the taxonomy

- **WHEN** an operator reads the dual-switch or analog tuning guidance in
  TUNING.md
- **THEN** it identifies the sensor as type D or type P with the
  `BUF_SENSOR_TYPE` value stated, and the control law named separately

#### Scenario: config.ini.example aligned

- **WHEN** an operator reads `config.ini.example` around the sensor type
- **THEN** the D=0 / P=1 contract is documented in the same Sync-Feedback
  Sensor vocabulary as TUNING.md, with no "PSF" alias
