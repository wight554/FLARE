# Persistence Contract Specification

## Purpose
Durable contract for FLARE flash-backed runtime parameters and config generation, extracted from `firmware/src/settings_store.c` and `AGENTS.md`.
## Requirements
### Requirement: Settings Version Bump
The runtime settings layout MUST be protected by a strict schema version.

#### Scenario: Struct Modification
- **WHEN** any field is added, removed, or resized in `settings_t`
- **THEN** `SETTINGS_VERSION` MUST be incremented
- **AND** on boot, if flash version mismatches, settings are wiped to default

### Requirement: Flash Loading and Defaults
Missing or corrupt flash SHALL NOT prevent safe boot.

#### Scenario: Fresh Board
- **WHEN** `settings_load()` reads invalid magic or bad CRC
- **THEN** settings are initialized to compiled C defaults
- **AND** written back to flash immediately

### Requirement: Runtime Tunables Flow
Any **durable** tunable (tier T1 or T2) SHALL live in `config.ini` and flow
through `gen_config.py`. A T3 internal constant SHALL NOT enter this flow; it
lives in its owning source module (see the `config-surface-tiers` capability)
and is exempt from the config/persist/SET/GET path.

#### Scenario: New Parameter
- **WHEN** a new **durable** (T1/T2) runtime parameter is added
- **THEN** it MUST be represented in `config.ini` and `config.ini.example`
- **AND** consumed in firmware via generated `CONF_*` macros

#### Scenario: New internal constant
- **WHEN** a new T3 internal control-loop constant is introduced
- **THEN** it is defined as `static const` / `#define` in the owning module
- **AND** it is NOT added to `config.ini`, `settings_t`, or the release
  `SET:`/`GET:` surface

### Requirement: Persisted fields round-trip symmetrically

Every field of `settings_t` written by `settings_save()` SHALL be read back by
`settings_load()` and SHALL have its owning runtime global initialized by
`settings_defaults()`. No field may be write-only (saved to flash but never
loaded), because such a field silently discards an operator's `SV:`-persisted
value on the next boot.

#### Scenario: A saved field is also loaded and defaulted

- **WHEN** a field `X` appears in `settings_save()` as `s.X = GLOBAL_X`
- **THEN** `settings_load()` contains `GLOBAL_X = ... s->X ...`
- **AND** `settings_defaults()` assigns `GLOBAL_X` (from its `CONF_*` macro)

#### Scenario: Parity is enforced mechanically

- **WHEN** the persistence parity test runs against `firmware/src/settings_store.c`
- **THEN** it fails if any `settings_t` field is written in `settings_save()`
  but missing from either `settings_load()` or `settings_defaults()`

### Requirement: Settings round-trip fixes do not bump the version gratuitously

`SETTINGS_VERSION` SHALL NOT be incremented by a fix that only completes the
load/default arms of fields already present in the `settings_t` layout, since
the on-flash byte layout is unchanged and persisted operator settings MUST
survive.

#### Scenario: Layout-preserving fix keeps persisted settings

- **WHEN** `settings_load()` / `settings_defaults()` are extended to handle a
  field that already exists in `settings_t` and is already written by
  `settings_save()`
- **THEN** `SETTINGS_VERSION` is unchanged
- **AND** a unit with valid persisted flash retains its stored values

