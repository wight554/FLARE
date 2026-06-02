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
