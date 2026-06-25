## Why

A klipper-config audit found the `klipper-mmu-config` spec lagging behind
`klipper/flare_mmu.cfg`: the spec names `T1`/`T2` (cfg has `T0`/`T1`), forbids
`FLARE_PRELOAD` (cfg ships it), and pins stale tip-forming dip defaults
(`dip_melt_gap=2.5`, `dip_pause=3` vs the cfg's `0.1`/`10`). These are spec-only
catch-ups — the cfg is the source of truth and is already correct.

## What Changes

- Correct the "Single-file" macro list: `T0`/`T1` (not `T1`/`T2`); add
  `FLARE_PRELOAD` and `_FLARE_SET_PURGE` to the available-macro list.
- Drop `FLARE_PRELOAD` from "Removed development macros" (it is intentionally
  present); add `FLARE_CUT` to it (the standalone cut macro is removed from the
  cfg — the cutter cycle is sequenced by the firmware toolchange `TC:`); keep
  `FLARE_CUT_BARE` / `FLARE_CUT_TEST` forbidden.
- Remove the `FLARE_CUT` macro from `klipper/flare_mmu.cfg` and drop it from the
  "Single-file" macro list.
- Add a "Preload macro" requirement describing `FLARE_PRELOAD` behavior.
- Fix the tip-forming dip-defaults scenario to the shipped values
  (`dip_melt_gap=0.1`, `dip_speed=30.0`, `dip_pause=10`).

## Deferred (needs domain input + overlaps an open change)

The audit also found `dist_meltzone_to_nozzle_tip` declared but unused, and the
spec's "full hotend path" / "ignore-buffer `MV:` retract" tip-form descriptions
describing the pre-buffer-lock design. Resolving these requires deciding whether
current `_FLARE_BL_RETRACT`-based tip forming *should* account for
`dist_meltzone_to_nozzle_tip` (i.e. is the unused var a latent regression or
genuinely dead). They also live in the "Variables block" / "Toolchange macro"
requirements already modified by the open `klipper-slicer-purge-and-load-tuning`
change, so editing them here would create conflicting deltas. Left for that
change's finalization. The dead var is intentionally NOT removed yet.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `klipper-mmu-config`: macro inventory corrected (`T0`/`T1`, `FLARE_PRELOAD`
  present + documented, `_FLARE_SET_PURGE` listed), tip-forming dip defaults
  synced to the shipped config.

## Impact

- Spec `klipper-mmu-config` + one `klipper/flare_mmu.cfg` edit (remove the
  `FLARE_CUT` macro). No firmware or host changes; the remaining drift fixes are
  spec-only (cfg already matches).
