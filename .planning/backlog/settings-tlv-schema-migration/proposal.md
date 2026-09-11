# Proposal: Settings TLV / Delta Schema Migration

## Why
Currently in `firmware/src/settings_store.c`, any schema change requires incrementing `SETTINGS_VERSION` (currently `61u`). When `s->version != SETTINGS_VERSION`, the loader completely wipes the saved sector and resets all user calibration back to compiled defaults. This has caused 61 historic wipe cycles on upgrade and creates enormous friction when adding tunables.

## What Changes
- Replace the monolithic fixed-offset `settings_t` binary dump with a tagged key-value or Tag-Length-Value (TLV) record layout, or a backward-compatible versioned migration header.
- On boot, unknown tags are ignored or preserved; missing tags fall back to compiled defaults without wiping existing calibrated parameters.
- Eliminates destructive resets on minor firmware upgrades.

## Impact
- `firmware/src/settings_store.c`
- `firmware/include/controller_shared.h`
- `scripts/test_settings_parity.py`
