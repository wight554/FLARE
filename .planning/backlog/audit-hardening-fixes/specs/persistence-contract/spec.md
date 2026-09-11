# Delta: persistence-contract

## ADDED Requirements

### Requirement: Drivetrain geometry inputs validated at every entry point

The system MUST validate values feeding `mm_per_step` math and TMC register
encoding (`microsteps`, `full_steps`, `gear_ratio`, `rotation_distance`) at all
three entry points: `SET:` handlers, `gen_config.py`, and `settings_load()`.
`microsteps` MUST be a chopconf-valid power of two (1..256). Denominator terms
MUST be clamped so no division by zero or `256/microsteps` fault is reachable
from flash content.

#### Scenario: SET rejects non-power-of-2 microsteps

- **WHEN** host sends `SET:MICROSTEPS:24`
- **THEN** firmware replies `ER` and keeps the prior value
- **AND** no partial chopconf state is written to the driver

#### Scenario: gen_config rejects invalid drivetrain config

- **WHEN** `config.ini` contains non-power-of-2 `microsteps` or `rotation_distance <= 0`
- **THEN** `gen_config.py` exits non-zero with a clear error
- **AND** no `tune.h` is emitted with the invalid value

#### Scenario: Flash with out-of-range drivetrain fields

- **WHEN** `settings_load()` reads CRC-valid flash whose drivetrain fields are
  out of range (e.g. `microsteps == 0` from layout drift)
- **THEN** loaded values are clamped to valid range before any division or TMC apply
- **AND** boot completes without hardfault
