## 1. Scope the UI contract (investigation)

- [ ] 1.1 Determine which `filament_pos` values the Fluidd/Mainsail MMU panel maps
  to the Gate vs Toolhead checkpoint dots (inspect the panel's status mapping).
  Record the ladder + thresholds the latch must honor. Acceptance: documented
  mapping referenced by tasks 3.x.
- [ ] 1.2 Confirm no UI element other than the cosmetic piston consumes the
  continuous `sync_feedback` float (grep the panel + `get_status` consumers).
  Acceptance: list of consumers; buffer-state enum retained.

## 2. Drop cosmetic/continuous mirror fields

- [x] 2.1 `scripts/flare_daemon.py`: removed the continuous analog fields
  (`SYNC_FEEDBACK` float piston, `SPS`, `FEED_RATE`, `REV_RATE`) from the mirrored
  `SET_MMU` field set; kept `SYNC_FEEDBACK_STATE`. Reconcile compares only desired
  `fields`, so dropping them causes no false-divergence/full-push.
- [x] 2.2 `klipper/mmu.py` `get_status`: deleted the synthetic filament-mm
  animation block + `filament_position`/`bowden_progress` computation + the
  `bowden_length`/`extruder_to_nozzle` reads, and removed the now-dead
  `_load_path_len_mm` helper. `filament_pos` is now the discrete loaded-state
  landmark ({0,4,10}); `filament_position`=0.0 and `bowden_progress`=-1.0 are kept
  as static keys (no gliding tip, no fake mm). Panel shows discrete stops.

## 2b. Stats: a swap counts an unload

- [x] 2b.1 `scripts/flare_daemon.py` `record_event_stats`: on `TC:DONE` also
  `unloads_success += 1` (TC unload phase emits no standalone `UNLOADED`).
  Acceptance: loads/unloads track across a print of swaps, not loads ≫ unloads.

## 3. Checkpoint latch

- [x] 3.0 `klipper/mmu.py` `get_status`: ROOT CAUSE of stuck Gate = `mmu_gate`
  reads the transit-only hub/`y_split` sensor, which clears once filament settles
  past the Y junction (Toolhead, a steady sensor, stays set). Latch `mmu_gate` to
  filament at/past the gate (loaded lane OR downstream presence OR toolhead).
  Acceptance: a loaded lane shows Gate checked alongside Toolhead.
- [ ] 3.1 `klipper/mmu.py`: optionally also latch `filament_pos` monotonically
  through the load (if the panel maps any checkpoint to filament_pos rather than
  the `mmu_gate` sensor — confirm via task 1.1). May be unnecessary now that 3.0
  fixes the Gate sensor dot. Acceptance: HW-confirm whether still needed.
- [ ] 3.2 Add a host-side unit test feeding a fast load/unload `SET_MMU` sequence
  and asserting `filament_pos` never skips the Gate checkpoint and steps down on
  unload. Acceptance: `python3 -m unittest` covers it (note: unittest discover
  silently skips non-TestCase files — verify the test actually runs).

## 4. Event-driven push

- [x] 4.1 Event-driven achieved by removing the continuous analog fields (2.1):
  the existing delta loop already emits nothing when no field changed
  (`daemon-klipper-mirror` "No field changed"), so with the analog churn gone a
  steady print emits zero `SET_MMU` and a discrete transition emits one promptly —
  no loop restructure needed. Acceptance: confirmed by 2.1 + existing delta logic.
- [ ] 4.2 Preserve "Full resync recovery" and "Host-busy backpressure" behavior
  unchanged. Acceptance: existing daemon-klipper-mirror scenarios still hold.

## 5. Validation

- [ ] 5.1 `python3 -m py_compile scripts/*.py klipper/mmu.py`.
- [ ] 5.2 `openspec validate klipper-mirror-event-driven --strict` and
  `openspec validate --specs --strict` pass.
- [ ] 5.3 HW: Fluidd/Mainsail MMU panel — confirm push rate drops to event-driven,
  buffer state displays, and Gate/Toolhead checkpoints latch correctly across a
  real load and unload (the reported stuck-Gate case is gone).

## Readiness and Delivery Checks

- [ ] No firmware touched; `python3 -m py_compile` for the scripts/mmu.py edits.
- [ ] Documentation sync: KLIPPER.md "Dashboard Integration" section reflects
  buffer-state (not piston animation) and no synthetic mm readout.
- [ ] `openspec validate klipper-mirror-event-driven --strict` passing.
- [ ] `openspec validate --specs --strict` passing.
- [ ] Append observation `memories/repo/klipper-mirror-event-driven.md` (3-5 lines:
  4Hz-overload root cause = analog piston + synthetic mm, event-driven push,
  checkpoint latch fix for stuck Gate) before archiving.
