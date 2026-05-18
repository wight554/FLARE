## ADDED Requirements

### Requirement: Sync-Feedback Sensor taxonomy is canonical

Documentation and live specs SHALL use the umbrella concept Sync-Feedback
Sensor (SFS) with two named variants: PSF (Proportional Sync-Feedback,
analog) and DSF (Discrete Sync-Feedback, dual-switch 3-state). The
home-grown umbrella vocabulary that names the sensor as "2-switch" or
"relay" SHALL NOT be used to denote the sensor in live prose.

#### Scenario: Sensor referred to by SFS taxonomy

- **WHEN** a live spec or doc refers to the buffer sensor
- **THEN** it uses SFS / PSF / DSF, not "2-switch" or "relay" as the
  sensor name

### Requirement: BUF_SENSOR_TYPE value contract is documented

Every live reference to `BUF_SENSOR_TYPE` SHALL document the value
contract as `DSF = 0` and `PSF = 1`. The integer values MUST remain
unchanged by this change.

#### Scenario: Value contract stated at reference

- **WHEN** `BUF_SENSOR_TYPE` appears in a live spec, TUNING.md, or
  config.ini.example
- **THEN** the DSF=0 / PSF=1 mapping is stated or unambiguously linked

#### Scenario: Integer values unchanged

- **WHEN** the firmware is built after this change
- **THEN** `BUF_SENSOR_TYPE == 0` is still the discrete path and `== 1`
  the analog path, byte-identical behavior
