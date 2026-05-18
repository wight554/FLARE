## ADDED Requirements

### Requirement: Buffer states use tension/compression/neutral vocabulary

The buffer state vocabulary SHALL be `BUF_TENSION` (filament tensioned,
buffer empty, printer pulling faster than the MMU pushes), `BUF_COMPRESSION`
(filament compressed, buffer full, MMU pushing faster than the printer
pulls), `BUF_NEUTRAL` (neutral band), and `BUF_FAULT`. The legacy names
`BUF_ADVANCE`, `BUF_TRAILING`, and the state `BUF_MID` MUST NOT appear
anywhere in firmware, scripts, live specs, or docs, and no back-compat
alias SHALL exist.

#### Scenario: No legacy state token survives

- **WHEN** the repository is searched for `ADVANCE`, `TRAILING`, or the
  buffer-state `MID`
- **THEN** zero matches exist in firmware, scripts, live specs, and docs

#### Scenario: Names match the physics

- **WHEN** the buffer is empty because the printer pulls faster than the
  MMU feeds
- **THEN** the state is `BUF_TENSION`; when the buffer is full because the
  MMU feeds faster than the printer pulls, the state is `BUF_COMPRESSION`

### Requirement: Serial protocol state tokens are renamed

The serial protocol SHALL emit `BUF:TENSION|NEUTRAL|COMPRESSION`, the
corresponding `EV:BS:*` tokens, and `EV:SYNC:TENSION_RISK_HIGH` in place of
the legacy `ADVANCE`/`MID`/`TRAILING`/`ADV_RISK_HIGH` tokens. In-repo
parsing scripts MUST be updated in the same change so they remain
consistent with the firmware.

#### Scenario: Status and events use new tokens

- **WHEN** a status line or buffer/sync event is emitted
- **THEN** it uses the tension/compression/neutral tokens and no legacy
  token, and `scripts/flare_cmd.py` parses them

### Requirement: Config keys are renamed to the new vocabulary

Configuration keys that named the legacy states SHALL be renamed
(`sync_tension_dwell_stop_ms`, `sync_tension_ramp_delay_ms`,
`sync_compression_bias_frac`, `compression_rate`, `neutral_creep_*`,
`sync_overshoot_neutral_extend`). No legacy key SHALL be accepted; the key
map MUST be documented as a migration note.

#### Scenario: Legacy config key rejected

- **WHEN** a config uses a legacy key such as `sync_trailing_bias_frac`
- **THEN** it is not recognized, and `MANUAL.md` documents the new key name

### Requirement: The rename does not change control behavior

This rename SHALL be behavior-preserving. A status-line and event
semantics snapshot captured before and after MUST be numerically identical
(only token spellings differ); any behavioral delta is out of scope and
belongs to `audit-sync-polarity`.

#### Scenario: Pre/post snapshot identical

- **WHEN** the same scenario is run before and after the rename
- **THEN** all numeric fields and control outputs are identical and only
  state token spellings differ
