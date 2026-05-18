## ADDED Requirements

### Requirement: Sync-Feedback Sensor taxonomy uses Happy Hare type codes

Documentation and live specs SHALL use the umbrella concept Sync-Feedback
Sensor with Happy Hare's canonical type codes: P (Proportional, analog),
D (Dual, two-switch 3-state), TO (Tension-Only), CO (Compression-Only).
New acronyms (DSF/SFS) MUST NOT be minted, and wiring shorthand or "relay"
MUST NOT be used to denote the sensor in live prose.

#### Scenario: Sensor referred to by HH type code

- **WHEN** a live spec or doc refers to the buffer sensor
- **THEN** it uses the Sync-Feedback Sensor term and a P/D/TO/CO code, not
  wiring shorthand, "relay", or an invented acronym

#### Scenario: Legacy analog alias retired

- **WHEN** the analog sensor is referenced in live prose or comments
- **THEN** it is called Happy Hare type P, not the legacy analog alias

### Requirement: BUF_SENSOR_TYPE value contract is documented

Every live reference to `BUF_SENSOR_TYPE` SHALL document the value
contract as `D = 0` and `P = 1`. The integer values MUST remain unchanged
by this change.

#### Scenario: Value contract stated at reference

- **WHEN** `BUF_SENSOR_TYPE` appears in a live spec, TUNING.md, or
  config.ini.example
- **THEN** the D=0 / P=1 mapping is stated or unambiguously linked

#### Scenario: Integer values unchanged

- **WHEN** the firmware is built after this change
- **THEN** `BUF_SENSOR_TYPE == 0` is still the dual-switch path and `== 1`
  the analog path, byte-identical behavior

### Requirement: TO and CO documented as recognized but unimplemented

Documentation SHALL list TO and CO as recognized Happy Hare Sync-Feedback
Sensor types that are not implemented in FLARE, so the taxonomy is
complete without implying FLARE support.

#### Scenario: TO/CO present but marked absent

- **WHEN** the sensor taxonomy is documented
- **THEN** TO and CO appear with an explicit "not implemented in FLARE"
  note
