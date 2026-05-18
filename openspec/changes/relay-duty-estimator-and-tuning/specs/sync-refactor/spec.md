## ADDED Requirements

### Requirement: Relay tuning knobs are config-driven

Relay control fractions and estimator bounds SHALL be sourced from
`config.ini` via `gen_config.py` and the generated tune header, not
hardcoded `#define`s in `sync.c` (`SYNC_RELAY_CATCHUP_FRAC`,
`SYNC_RELAY_NEUTRAL_FRAC`, relay estimate `lo`/`hi`, confidence
window/threshold). On-Pi relay tuning SHALL be a config→flash loop (and
`SET:` where a knob is safe to change at runtime), not an
edit-`#define`-recompile loop.

#### Scenario: Relay knob changed via config

- **WHEN** an operator changes a relay knob in `config.ini`, runs
  `gen_config.py`, and flashes
- **THEN** the new value takes effect with no source edit to `sync.c`

#### Scenario: Legacy define removed

- **WHEN** the firmware is built after this change
- **THEN** the relay knobs no longer exist as hardcoded `#define`s and the
  values come from generated config

### Requirement: Relay NEUTRAL control law uses the bounded estimator

The relay-mode `BUF_NEUTRAL` target SHALL be produced by the relay
duty-cycle estimator (bounded, lean-on-top, fallback when unconfident).
The relay `BUF_TENSION` catch-up and `BUF_COMPRESSION` stop branches and
the analog (`BUF_SENSOR_TYPE != 0`) path SHALL remain unchanged.

#### Scenario: Analog path byte-identical

- **WHEN** `BUF_SENSOR_TYPE != 0`
- **THEN** control behavior is identical to before this change

#### Scenario: Relay safety branches unchanged

- **WHEN** relay mode is in `BUF_TENSION` or `BUF_COMPRESSION`
- **THEN** the target is `relay_base × SYNC_RELAY_CATCHUP_FRAC` or
  `SYNC_MIN_SPS` respectively, exactly as in `relay-buffer-control-2switch`

### Requirement: neutral_creep is resolved not left inert

This change SHALL resolve the previously telemetry-only `neutral_creep`
long-NEUTRAL anti-drift path, which MUST NOT remain as computed-but-unused
dead code afterward: either its compute and protocol slot are removed, or
its protocol slot is repurposed for estimator telemetry, because the duty
estimator subsumes its anti-drift role.

#### Scenario: No dead neutral_creep compute remains

- **WHEN** the firmware is built after this change
- **THEN** `neutral_creep` is either removed or actively used for
  estimator/confidence telemetry, with no inert computed-but-unused path
