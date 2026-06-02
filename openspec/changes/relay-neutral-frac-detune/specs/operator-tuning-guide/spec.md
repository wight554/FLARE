## ADDED Requirements

### Requirement: Type-D tuning guidance names the controller-correct lever

Operator-facing type-D tuning guidance SHALL attribute the relay limit cycle
and its COMPRESSION/TENSION drift to `relay_neutral_frac` (and
`relay_catchup_frac`), and SHALL NOT instruct the operator to change
`sync_kp_rate` for a type-D (`BUF_SENSOR_TYPE == 0`) buffer. This requirement
applies to both `TUNING.md` and the verdict/help text emitted by
`flare_sync_check.py` (`analyze_stability`, `analyze_drift`). `sync_kp_rate`
guidance MAY appear only for analog type P (`BUF_SENSOR_TYPE == 1`), whose
`psf_control_law` actually consumes it.

#### Scenario: Stability/drift advice points at the relay knob for type-D

- **WHEN** `flare_sync_check.py` reports a stability (ringing) or drift FAIL
- **THEN** the remediation text names `relay_neutral_frac` (down = less
  COMPRESSION time, up = less TENSION drift), not `sync_kp_rate`

#### Scenario: kp is documented as inert for type-D

- **WHEN** `TUNING.md` describes type-D relay tuning
- **THEN** it states that `sync_kp_rate` / accel autotune does not affect the
  type-D relay (those apply to analog type-P) and that the quiet-cycle lever
  is `relay_neutral_frac`

### Requirement: Default relay_neutral_frac tracks demand without deliberate overfeed

The shipped default `relay_neutral_frac` SHALL match demand (`1.00`) for type-D
after the no-overshoot ramp fix, not deliberately overfeed. `TUNING.md` and
`config.ini.example` SHALL show this default, and it SHALL match the
`gen_config.py` default. Operators MAY raise it slightly only if hardware soak
shows steady TENSION drift.

#### Scenario: Documented default matches the generator

- **WHEN** the `relay_neutral_frac` default is read from `TUNING.md`,
  `config.ini.example`, and `gen_config.py`
- **THEN** all three agree on `1.00`

#### Scenario: Stability detector threshold is not masking the cycle

- **WHEN** the `flare_sync_check.py` stability ringing threshold is read
- **THEN** it is `1.0` cycles/s (not raised to absorb the relay limit cycle),
  so a genuine sustained cycle is reported rather than silenced

### Requirement: Type-D relay trim learns from switch crossings

For `BUF_SENSOR_TYPE == 0`, the firmware SHALL treat `BUF_NEUTRAL` crossings to
real switches as the available feedback signal: COMPRESSION touches SHALL reduce
the volatile neutral feed trim, TENSION touches SHALL increase it, and the trim
SHALL be anti-windup clamped. The trim SHALL apply only to the type-D NEUTRAL
relay feed and SHALL NOT alter analog type-P feedforward.

#### Scenario: COMPRESSION touch backs off neutral feed

- **WHEN** the type-D buffer crosses from `BUF_NEUTRAL` to `BUF_COMPRESSION`
- **THEN** the learned neutral trim is reduced by `SYNC_RELAY_TRIM_STEP_SPS`
- **AND** the result is clamped to `-SYNC_RELAY_TRIM_CLAMP_SPS`

#### Scenario: TENSION touch restores neutral feed

- **WHEN** the type-D buffer crosses from `BUF_NEUTRAL` to `BUF_TENSION`
- **THEN** the learned neutral trim is increased by `SYNC_RELAY_TRIM_STEP_SPS`
- **AND** the result is clamped to `+SYNC_RELAY_TRIM_CLAMP_SPS`

### Requirement: Type-D estimator anchors on neutral fill

For `BUF_SENSOR_TYPE == 0`, the firmware SHALL treat the
`BUF_NEUTRAL -> BUF_COMPRESSION` transition as the primary demand sample by
averaging the actual applied `sync_current_sps` over the NEUTRAL dwell,
preferring the pre-taper portion before compression-side braking when available,
and subtracting the measured fill rate. Degenerate fill samples SHALL be ignored,
slow near-converged fills SHALL remain eligible, and accepted demand samples
SHALL blend into `extruder_est_sps`. The residual neutral trim SHALL leak toward
zero during `BUF_NEUTRAL` dwell. This estimator correction and trim leak SHALL
NOT alter the analog type-P estimator/feedforward path.

#### Scenario: NEUTRAL fill samples known applied feed

- **WHEN** the type-D buffer crosses from `BUF_NEUTRAL` to `BUF_COMPRESSION`
- **THEN** firmware estimates demand from averaged NEUTRAL feed minus fill rate
- **AND** prefers the pre-taper applied-feed average when that window has enough
  samples
- **AND** blends that sample into `extruder_est_sps`

#### Scenario: Degenerate fill sample is rejected

- **WHEN** the type-D buffer crosses from `BUF_NEUTRAL` to `BUF_COMPRESSION`
- **AND** the NEUTRAL dwell is too short or the fill rate is far above averaged feed
- **THEN** firmware does not update `extruder_est_sps` from that crossing

#### Scenario: Residual trim self-centers in neutral

- **WHEN** the type-D buffer dwells in `BUF_NEUTRAL`
- **THEN** the learned neutral trim moves toward zero over time
