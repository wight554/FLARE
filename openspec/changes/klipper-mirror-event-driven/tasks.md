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
- [ ] 2.2 `klipper/mmu.py` `get_status`: delete the synthetic filament-mm
  computation (1413-1481 animation block + `filament_position`/`bowden_progress`)
  and the `bowden_length` / `extruder_to_nozzle` reads. DEFERRED — entangled with
  the checkpoint latch (3.x) and depends on the Fluidd panel filament_pos/
  bowden_progress contract (task 1.1). Do with 3.1.

## 2b. Stats: a swap counts an unload

- [x] 2b.1 `scripts/flare_daemon.py` `record_event_stats`: on `TC:DONE` also
  `unloads_success += 1` (TC unload phase emits no standalone `UNLOADED`).
  Acceptance: loads/unloads track across a print of swaps, not loads ≫ unloads.

## 3. Checkpoint latch

- [ ] 3.1 `klipper/mmu.py`: track `filament_pos` as monotonic phase progress (latch
  Gate-passed on gear/gate-seen or load phase; hold through toolhead; step down on
  unload; clear on completed unload) per the task 1.1 ladder. Acceptance: a
  simulated fast gate→toolhead transition leaves Gate + Toolhead both set.
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
