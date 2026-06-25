## 1. Slicer-driven purge plumbing

- [x] 1.1 `klipper/flare_mmu.cfg` `_FLARE_TC_STATE`: add
  `variable_next_purge: -1.0` with a comment (`-1` = unset; load falls back to
  `_FLARE_VARS.purge_len`). Acceptance: variable present, default `-1.0`.
- [x] 1.2 `klipper/flare_mmu.cfg`: add `[gcode_macro _FLARE_SET_PURGE]` that sets
  `_FLARE_TC_STATE.next_purge` to `PURGE` when `> 0` else `_FLARE_VARS.purge_len`,
  documenting `PURGE=[flush_length]` (Orca) and the `*2` note for
  `[first_flush_volume]`. Acceptance: `_FLARE_SET_PURGE PURGE=107.7` sets
  `next_purge=107.7`; `PURGE=0` sets it to `purge_len`.
- [x] 1.3 `klipper/flare_mmu.cfg` `_FLARE_LOAD_HOTEND`: resolve
  `purge = next_purge if >= 0 else purge_len`, reset `next_purge` to `-1` when it
  was set, and call `_FLARE_PURGE` only when `purge > 0`. Acceptance: a set
  `next_purge` is purged once then reset; unset uses `purge_len`.

## 2. Leak guards on no-load paths

- [x] 2.1 `klipper/flare_mmu.cfg` `_FLARE_CHANGE_LANE`: in the "lane already
  loaded, skipping" branch, set `next_purge` to `-1`. Acceptance: a skipped
  change does not leave a consumable `next_purge`.
- [x] 2.2 `klipper/flare_mmu.cfg` `_FLARE_SYNC_TOOLHEAD`: reset `next_purge` to
  `-1` (placed so it cannot run mid-toolchange). Acceptance: PRINT_START /
  PRINT_END / CANCEL via this hook clears any stale value.

## 3. Load + toolchange tuning

- [x] 3.1 `klipper/flare_mmu.cfg` `_FLARE_LOAD_HOTEND`: add `BS` + `G4 P2000`
  before the meltzone push and `BS` + `G4 P1000` after the purge step.
  Acceptance: both buffer-reset brackets present.
- [x] 3.2 `klipper/flare_mmu.cfg` `_FLARE_LOAD_HOTEND`: push at
  `push_speed = purge_speed / 2` (three stages use `push_speed`). Acceptance:
  meltzone push feedrates derive from `purge_speed / 2`.
- [x] 3.3 `klipper/flare_mmu.cfg` `_FLARE_POST_TC_LOAD`: drop the `* 1.1` on
  `load_park_dist`; comment out the Stage 1 `MV:` line with a re-enable note.
  Acceptance: `load_park_dist = dist_filament_park + dist_sensor_to_synced_move`;
  no MV approach runs by default.

## 4. Validation

- [x] 4.1 Spec: `openspec validate klipper-slicer-purge-and-load-tuning --strict`
  passes.
- [x] 4.2 Spec: `openspec validate --specs --strict` passes.
- [ ] 4.3 HW: dry-run macro plumbing on the rig — `_FLARE_SET_PURGE PURGE=107.7`
  then read `_FLARE_TC_STATE.next_purge` via `SET_GCODE_VARIABLE`/status; confirm
  `_FLARE_LOAD_HOTEND` purges 107.7 mm and resets to `-1`.
- [ ] 4.4 HW: multi-colour OrcaSlicer print with Change-Filament G-code
  `_FLARE_SET_PURGE PURGE=[flush_length]`; confirm per-transition purge length
  tracks the slicer flush matrix and the `BS` brackets keep the buffer stable.
- [ ] 4.5 HW: skip + cancel leak check — trigger a skipped change and a mid-change
  cancel after `_FLARE_SET_PURGE`; confirm a following manual load uses
  `purge_len`, not the stale value.

## Readiness and Delivery Checks

- [ ] No firmware touched; dev-tuning superset build N/A for this change
  (klipper-config only). Confirm no `firmware/` or `scripts/` edits in the diff.
- [ ] Documentation sync: confirm no MANUAL.md / BEHAVIOR.md purge references
  drift (none reference purge today; re-grep before archive).
- [x] `openspec validate klipper-slicer-purge-and-load-tuning --strict` passing.
- [x] `openspec validate --specs --strict` passing.
- [ ] Append observation `memories/repo/klipper-slicer-purge-and-load-tuning.md`
  (3-5 compressed lines: flush_length vs first_flush_volume, sentinel/consume +
  leak-guard paths, BS-bracket/half-push tuning) before archiving.
