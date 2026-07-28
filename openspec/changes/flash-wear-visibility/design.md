## Context

`settings_save()` erases and reprograms the same 4KB sector on every save
(`SETTINGS_FLASH_OFFSET`, `settings_store.c:22,367`). RP2040's onboard NOR
flash endurance is typically ~100k erase cycles per sector. There is
currently no way to know how many erase cycles a given board has
accumulated.

## Goals / Non-Goals

**Goals:**
- Make erase-cycle count visible (`GET:`) and warn once as it approaches
  typical endurance.
- Zero behavior change to any other persisted field.

**Non-Goals:**
- Wear leveling / A/B sector rotation (the real fix for the underlying
  problem, bigger lift, not attempted here).
- A config.ini-driven tunable threshold — `FLASH_WEAR_WARN_THRESHOLD` is a
  fixed engineering constant, not something an operator should retune.

## Decisions

- Counter lives in `settings_t` like every other persisted field, subject
  to the same `SETTINGS_VERSION`-bump-on-change rule this repo already
  enforces — no special-casing, even though that means an `RS:` reset also
  resets the wear count (documented, not treated as a defect: this repo
  has no field-migration path today, and carving out an exception for one
  field would be worse than consistency).
- Increment happens in `settings_save()` itself, immediately before the
  erase+program, so the persisted value always reflects the write that is
  about to happen (not the previous one).
- Warning event fires exactly once (equality check on the exact threshold
  crossing tick, not `>=`), so it doesn't spam on every subsequent save.

## Risks / Trade-offs

- `SETTINGS_VERSION` bump wipes all operator tuning on this reflash (a
  known, pre-existing, project-wide limitation — `ARCHITECTURE_BRIEF.md`
  §9's "worst decision #2" — not something this change fixes or makes
  worse beyond the one-time bump).
- The counter itself doesn't prevent wear, only reports it — by design,
  per Non-Goals above.
