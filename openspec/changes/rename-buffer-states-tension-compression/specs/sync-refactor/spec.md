## ADDED Requirements

### Requirement: Buffer states use tension/compression/neutral vocabulary

The buffer state vocabulary SHALL be `BUF_TENSION` (filament tensioned,
buffer empty, printer pulling faster than the MMU pushes), `BUF_COMPRESSION`
(filament compressed, buffer full, MMU pushing faster than the printer
pulls), `BUF_NEUTRAL` (neutral band), and `BUF_FAULT`. The legacy names
`BUF_ADVANCE`, `BUF_TRAILING`, and the buffer-state `BUF_MID`/state-derived
`mid` MUST NOT appear anywhere in firmware, scripts, live specs, or docs,
and no back-compat alias SHALL exist. Arithmetic `mid`/`midpoint`
unrelated to the buffer state is out of scope and MAY remain.

#### Scenario: No legacy state token survives

- **WHEN** the repository is searched for `ADVANCE`/`TRAILING` and
  buffer-state-derived `mid`
- **THEN** zero matches exist in firmware, scripts, live specs, and docs
  (unrelated arithmetic `mid` excluded)

#### Scenario: Names match the physics

- **WHEN** the buffer is empty because the printer pulls faster than the
  MMU feeds
- **THEN** the state is `BUF_TENSION`; when the buffer is full because the
  MMU feeds faster than the printer pulls, the state is `BUF_COMPRESSION`

### Requirement: Serial protocol tokens and field keys are renamed

The serial protocol SHALL emit `BUF:TENSION|NEUTRAL|COMPRESSION`, the
corresponding `EV:BS:*` tokens, `EV:SYNC:TENSION_RISK_HIGH`, and renamed
short status field keys for any old-state-derived key (`AD`, `TD`, `APX`,
and similar) in place of the legacy spellings. In-repo parsing scripts
MUST be updated in the same change so they remain consistent.

#### Scenario: Status, events, and field keys use new tokens

- **WHEN** a status line or buffer/sync event is emitted
- **THEN** state tokens and old-state-derived field keys use the new
  vocabulary with no legacy survivor, and `scripts/flare_cmd.py` parses
  them

### Requirement: Config keys are renamed to the new vocabulary

Configuration keys that named the legacy states SHALL be renamed
(`sync_tension_dwell_stop_ms`, `sync_tension_ramp_delay_ms`,
`sync_compression_bias_frac`, `compression_rate`, `neutral_creep_*`,
`sync_overshoot_neutral_extend`). Legacy keys SHALL be ignored by the
existing unknown-key handling (no new hard-error path is added); a stale
`config.ini` falls back to defaults for the renamed keys. The key map
MUST be documented as a migration note in `MANUAL.md`.

#### Scenario: Legacy config key ignored

- **WHEN** a config uses a legacy key such as `sync_trailing_bias_frac`
- **THEN** it is silently ignored (default used, existing behavior) and
  `MANUAL.md` documents the new key name

### Requirement: The rename does not change control behavior

This rename SHALL be behavior-preserving. A status-line and event
semantics snapshot captured before and after MUST be numerically identical
(only token spellings differ); any behavioral delta is out of scope and
belongs to `audit-sync-polarity`.

#### Scenario: Pre/post snapshot identical

- **WHEN** the same scenario is run before and after the rename
- **THEN** all numeric fields and control outputs are identical and only
  state token spellings differ
