## Why

`flare_mmu.cfg` only supported a static `purge_len` per toolchange, so colour
flush could not track the slicer's per-transition flush volume. Porting the
LH-Stinger `_SP_SET_PURGE` idea lets the slicer drive purge length, but the
faithful path also forces buffer-state hygiene around the hotend load and two
small toolchange tunings the rig already runs. The OrcaSlicer
`[first_flush_volume]` placeholder is misleadingly named: `GCode.cpp` computes
`first_flush_volume = purge_length / 2` (filament length, halved), while
`flush_length` is the full length — so FLARE consumes `[flush_length]` directly
in mm with no magic multiplier.

## What Changes

- Add `_FLARE_SET_PURGE` and a `_FLARE_TC_STATE.next_purge` runtime variable so
  the slicer's Change-Filament G-code can set the next toolchange's purge length
  (`PURGE=[flush_length]`, mm). `_FLARE_LOAD_HOTEND` consumes it once and resets
  it; manual loads fall back to `_FLARE_VARS.purge_len`.
- Reset `next_purge` on the no-load paths so a slicer-set value cannot leak into
  a later load: the `_FLARE_CHANGE_LANE` "lane already loaded" skip branch and
  the `_FLARE_SYNC_TOOLHEAD` PRINT_START / PRINT_END / CANCEL hook.
- Bracket `_FLARE_LOAD_HOTEND` with `BS` buffer-state resets + settle dwells
  (before the meltzone push and after the purge), and push through the meltzone
  at half `purge_speed` (seat, not flush).
- `_FLARE_POST_TC_LOAD`: drop the `* 1.1` on `load_park_dist` and disable the
  Stage 1 MMU-only `MV:` approach by default (kept as a commented, re-enablable
  line) — the delayed-TS:1 insert handler already seats the tip during FL.

## Capabilities

### New Capabilities

- `klipper-mmu-config`: slicer-driven per-toolchange purge length via
  `_FLARE_SET_PURGE` + `next_purge`.

### Modified Capabilities

- `klipper-mmu-config`: `_FLARE_LOAD_HOTEND` now buffer-resets around the load,
  pushes at half purge speed, and purges the resolved per-change amount; the
  purge-speed feedrate scenario and the toolchange `load_park_dist` / Stage 1
  approach scenarios are updated to match.

## Impact

- `klipper/flare_mmu.cfg`: `_FLARE_VARS` (doc only), `_FLARE_TC_STATE`
  (`next_purge`), `_FLARE_SET_PURGE` (new), `_FLARE_LOAD_HOTEND`,
  `_FLARE_CHANGE_LANE`, `_FLARE_POST_TC_LOAD`, `_FLARE_SYNC_TOOLHEAD`.
- Spec `klipper-mmu-config`. No firmware, protocol, config.ini, or tune.h change.
- Slicer side: set Change-Filament G-code to `_FLARE_SET_PURGE PURGE=[flush_length]`
  (Orca). Validation needs a real multi-colour print (HW).
