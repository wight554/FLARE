## Why

`ARCHITECTURE_BRIEF.md` §10 (axis C, "10x print duration / cumulative
writes") flags that every settings save is a full sector erase + program of
the *same* physical flash sector (`settings_store.c:367`), with no wear
leveling and no counter to warn anyone as it walks toward RP2040 NOR
flash's typical ~100k-cycle endurance. This adds visibility only — a
persisted erase-cycle counter and a one-time warning event — not the
deeper A/B-rotation fix (out of scope, bigger architectural lift).

## What Changes

- New persisted `flash_erase_count` field, incremented once per
  `settings_save()` call (before that save's own erase+program).
- New `GET:FLASH_ERASE_COUNT` read-only param.
- New `EV:FLASH:WEAR_WARNING` event, fires once when the count first
  crosses `FLASH_WEAR_WARN_THRESHOLD` (80000).
- `SETTINGS_VERSION` bump (60 -> 61) — unavoidable given this repo's
  current settings-persistence model (no field-migration path).

## Capabilities

### Modified Capabilities
- `persistence-contract`: add a requirement that flash-sector erase cycles
  are counted and visible, with a one-time wear warning.

## Impact

- `firmware/src/settings_store.c`, `firmware/src/main.c`,
  `firmware/include/controller_shared.h`, `firmware/src/protocol.c`.
- `MANUAL.md` (new GET param + event documented).
- No config.ini/tune.h changes — this is a persisted runtime counter, not
  a user tunable, so it does not go through the 10-step tunable-add
  ritual.
