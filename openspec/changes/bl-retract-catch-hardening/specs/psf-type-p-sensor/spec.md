## ADDED Requirements

### Requirement: Type-P Buffer-Lock Prime Uses Predictive Rail Stop
The type-P buffer-lock prime SHALL run at `SYNC_MAX_SPS` and SHALL stop on a
predicted position rather than the instantaneous filtered position, mirroring
the boot-stabilize predictive stop. The firmware SHALL NOT throttle the type-P
prime to `BUF_STAB_SPS` as a substitute for prediction: the underlying problem
is PSF filter lag, and slowing the prime leaves it unable to out-run a
concurrent extruder retract.

Predicted position SHALL be
`g_buf_pos + BL_PRIME_PREDICT_LEAD_S * g_vel_norm_f`, compared against
`PSF_HOME_THRESHOLD_NORM` with the sign of the armed rail.

The type-P PD control law remains feed-only
(`clamp_i(target, 0, max_sps)`); retract authority is provided solely by the
`buffer-state-lock` catch, not by the PD.

#### Scenario: Prime out-runs a concurrent retract
- **WHEN** `BL:T` is armed on a type-P buffer
- **AND** the extruder retracts concurrently
- **THEN** the prime commands `SYNC_MAX_SPS`, not `BUF_STAB_SPS`

#### Scenario: Predictive stop prevents rail slam
- **WHEN** the type-P prime approaches the armed rail at `SYNC_MAX_SPS`
- **AND** the filtered position lags the true position
- **THEN** the prime stops when the predicted position crosses
  `PSF_HOME_THRESHOLD_NORM`
- **AND** the buffer does not reach the mechanical extreme

#### Scenario: PD provides no retract authority
- **WHEN** the extruder retracts while the controller is in `SYNC_ACTIVE`
- **THEN** the PD output remains clamped at or above zero
- **AND** retract compensation requires an armed `BL` lock
