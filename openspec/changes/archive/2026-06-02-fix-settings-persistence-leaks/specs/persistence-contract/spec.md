## ADDED Requirements

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
