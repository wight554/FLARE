## ADDED Requirements

### Requirement: TUNING.md uses SFS/DSF/PSF vocabulary

TUNING.md SHALL use the Sync-Feedback Sensor vocabulary (SFS, PSF, DSF)
and SHALL document the `BUF_SENSOR_TYPE` value contract (DSF=0, PSF=1)
where sensor mode is referenced, naming the sensor separately from the
control law.

#### Scenario: Relay/analog sections use the taxonomy

- **WHEN** an operator reads the discrete (dual-switch) or analog tuning
  guidance in TUNING.md
- **THEN** it identifies the sensor as DSF or PSF with the
  `BUF_SENSOR_TYPE` value stated, and the control law named separately

#### Scenario: config.ini.example aligned

- **WHEN** an operator reads `config.ini.example` around the sensor type
- **THEN** the DSF=0 / PSF=1 contract is documented in the same SFS
  vocabulary as TUNING.md
