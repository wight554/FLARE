# Delta: toolchange-orchestration

## MODIFIED Requirements

### Requirement: RELOAD Buffer-Driven Contact
During runout RELOAD, the new lane SHALL approach until physical buffer contact is detected. Contact SHALL mean the buffer arm is pushed to the compression side for both sensor types: `BUF_COMPRESSION` for type-D, and analog position above `PSF_LOAD_CONTACT_THRESHOLD_NORM` (compression-side, sign convention +compression/−tension) for type-P. A type-P buffer resting at or drifting toward its tension/home rail SHALL NOT register as contact.

#### Scenario: RELOAD Approach
- **WHEN** the old lane clears the Y-splitter and `RELOAD_JOIN_MS` elapses
- **THEN** the new lane starts `TASK_FEED` at `JOIN_SPS`
- **AND** waits for the buffer to hit `BUF_COMPRESSION`
- **AND** aborts if the configured travel limit or physical timeout is reached before contact

#### Scenario: Type-P RELOAD Approach contact
- **WHEN** RELOAD approach runs with `BUF_SENSOR_TYPE=1` (type-P)
- **THEN** contact is detected only when `g_buf_pos > PSF_LOAD_CONTACT_THRESHOLD_NORM` (arm pushed compression-side by the new filament)
- **AND** the at-rest home position (−1.0) does not satisfy the contact condition

#### Scenario: Y gate disabled does not insta-fail tail-clear wait
- **WHEN** `RELOAD_Y_TIMEOUT_MS` is configured `0` and the old lane tail has not yet cleared
- **THEN** the Y-splitter gate is skipped but the tail-clear wait continues without raising `RELOAD_Y_TIMEOUT` immediately

### Requirement: RELOAD Bang-Bang Pressure Cycle
During the RELOAD follow phase, the new lane SHALL over-feed to close the gap and maintain pressure on the old tail. The follow success signal SHALL be equivalent in strength for both sensor types: the physical `BUF_TENSION` switch for type-D, and deep tension (`g_buf_pos <= -PSF_HOME_THRESHOLD_NORM`) or the toolhead sensor for type-P. The shallow goal-relative tension zone SHALL NOT complete a type-P follow, because old-tail drainage crosses it without an extruder grab.

#### Scenario: Follow Phase
- **WHEN** physical contact is established (`BUF_COMPRESSION`)
- **THEN** the motor target becomes `extruder_est_sps * RELOAD_LEAN_FACTOR` (over-feeding)
- **AND** drops to `COMPRESSION_RATE` if the physical arm hits the `COMPRESSION` wall
- **AND** repeats this cycle until `LOADED` (toolhead sensor triggered or `BUF_TENSION` sustained)

#### Scenario: Type-P follow success requires extruder grab
- **WHEN** RELOAD follow runs with `BUF_SENSOR_TYPE=1` and the buffer position falls below the goal-relative tension zone edge but stays above `-PSF_HOME_THRESHOLD_NORM`
- **THEN** the follow phase continues feeding (no `RELOAD:LOADED`)
- **AND** `RELOAD:LOADED` is emitted only on deep tension (`g_buf_pos <= -PSF_HOME_THRESHOLD_NORM`), toolhead sensor trigger, or the `LOAD_MAX` distance fallback
