## Context

LH-Stinger sets a runtime `next_purge` in `_SP_VARS_RUNTIME` from the slicer and
consumes it during the change. FLARE has no separate runtime macro; `_FLARE_TC_STATE`
already holds runtime toolchange state (`mmu_active`), so `next_purge` lives there.

## Decisions

- **Placeholder = `[flush_length]`, no multiplier.** `GCode.cpp:875` derives
  `purge_length = purge_volume / filament_area`; `:949/:7933` expose `flush_length
  = purge_length` (full mm), `:920/:7882` expose `first_flush_volume = purge_length
  / 2`. Passing `[flush_length]` gives the correct mm directly and is diameter-safe
  via `filament_area`. `[first_flush_volume]` users double slicer-side. No
  `purge_multiplier` knob (removed after confirming it is a constant 1.0 no-op).

- **Sentinel `-1` + consume-once.** `next_purge` defaults `-1` (unset). `_FLARE_LOAD_HOTEND`
  resolves `next_purge if >= 0 else purge_len`, then resets to `-1`. This keeps
  manual / non-slicer loads on the static default and prevents a one-shot slicer
  value from repeating.

- **Leak guards on no-load paths.** Consume-on-load misses two cases: a change
  skipped because the lane is already loaded, and an abort/cancel after
  `_FLARE_SET_PURGE` but before the load. Reset `next_purge` in the
  `_FLARE_CHANGE_LANE` skip branch and in `_FLARE_SYNC_TOOLHEAD` (the documented
  PRINT_START/END/CANCEL hook, which runs on the reset/error path that END alone
  would miss on a crash). The reset is placed where it cannot run mid-toolchange,
  so it never wipes a value the slicer just set.

- **Buffer-reset brackets + half-speed push.** Empirically validated on the rig:
  a `BS` + dwell before the meltzone push and after the purge keeps the buffer in
  a known state across the load; the meltzone approach only needs to seat
  filament, so it runs at `purge_speed / 2` rather than the flush speed.

- **Stage 1 MV disabled.** The delayed-TS:1 insert handler seats the tip during
  FL, making the Stage 1 MMU-only `MV:` redundant; it is commented out (re-enable
  if a setup under-feeds) and `load_park_dist` drops its `* 1.1` safety factor.

## Risks

- Slicer leak edge-cases beyond the two guarded paths would re-introduce a stale
  purge; covered by the unset-default and the PRINT_START reset (clean slate each
  print). HW print validation still required for purge sizing and the BS bracket
  timing.
