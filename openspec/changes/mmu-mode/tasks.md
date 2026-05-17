# Tasks: mmu-mode

Status legend: [ ] todo  [~] in progress  [x] done

## 0. Pre-flight
- [x] Buffer half-travel measured: `BUF_HALF_TRAVEL` = 7.8 mm. Tip-forming
      analysis ⇒ HOLD primitive REQUIRED (see proposal "Resolved").

## 1. Tunable rename TC_AUTO_CUT → UNLOAD_CUT
- [x] `controller_shared.h` extern rename
- [x] `main.c` global rename
- [x] `settings_store.c` field rename + `SETTINGS_VERSION` bump
- [x] `scripts/gen_config.py` key `tc_auto_cut` → `unload_cut`
- [x] `config.ini` + `config.ini.example` key rename
- [x] `firmware/include/tune.h` regenerate (CONF_UNLOAD_CUT)
- [x] protocol.c SET + GET token rename
- [x] Docs: MANUAL.md / BEHAVIOR.md / KLIPPER.md references
- [x] `ninja -C build_local` + `py_compile` green; commit + push
      Validation 2026-05-17: `ninja -C build_local`;
      `python3 -m py_compile scripts/*.py`.

## 2. UL: cut-on-unload
- [x] protocol.c `UL:` multi-phase: clear OUT → CU → clear OUT (cutter enabled)
- [x] Preserve single-retract path when `!ENABLE_CUTTER || !UNLOAD_CUT`
- [x] Emit `EV:UNLOADED` only after final clear (flare_cmd.py blocking intact)
- [x] Regression: RELOAD `TASK_UNLOAD` path must NOT cut
- [x] Build + commit + push
      Validation 2026-05-17: `ninja -C build_local`;
      `python3 -m py_compile scripts/*.py`.

## 3. UM: entry-conditional cut
- [x] Flow 1 (OUT/YS present at entry): full UL cycle (cut if enabled) + extend
      reverse until IN clears
- [x] Flow 2 (OUT clear at entry): no cut, retract until IN clears
- [x] Build + commit + push
      Validation 2026-05-17: `ninja -C build_local`;
      `python3 -m py_compile scripts/*.py`.

## 4. TC: equivalence
- [ ] Confirm/align TC unload phase == new UL semantics (TC ≡ UL+swap+FL)
- [ ] Gate on renamed `UNLOAD_CUT`
- [ ] Regression: preload, autoload, TC: end-to-end
- [ ] Build + commit + push

## 5. Klipper integration
- [ ] KLIPPER.md `change_lane` macro: extruder tip-form + unload from gears →
      `TC:`; remove `FLARE_UNLOAD` from spool-change path
- [ ] Document tip-forming vs sync tuning (POST_PRINT_STAB_DELAY_MS, BUF travel)
- [ ] Command-light: macro sends NO `TS:1`/`TS:0`/`SM:`; `TS_BUF_MS`/sensor
      documented as optional accelerator (not a gate)
- [ ] Validate: `TC:` (UL+swap+FL) self-completes via `buf_advance_sane`
      geometry + `AUTO_MODE` sync-on-LOADED, no host command
- [ ] Drop/align `TC_LOAD_WAIT_TH` hard `toolhead_has_filament` gate to the
      FL `loaded` OR-condition (task 4 overlap)

## 6. Sync HOLD primitive (REQUIRED — partial, keeps basic stab)
- [ ] Add `g_sync_hold` flag (runtime-only)
- [ ] Gate top of `sync_tick` on held (no sync mode/estimator/auto-start)
- [ ] In `buffer_stabilize_start_internal`: when held, refuse
      `BUFFER_SERVICE_NEG_SYNC`, still permit `BUFFER_SERVICE_STABILIZE`
- [ ] Command surface: explicit `HD:1` enable / `HD:0` disable (dedicated,
      not overloading `SM:`) + `GET:HOLD` + `?:` dump field
- [ ] Safety auto-clear on `TS:1` and on `TC:`/`UL:` start (never stuck held);
      explicit `HD:0` is the primary path
- [ ] `change_lane` macro 6 steps: `HD:1` → tip form → `HD:0` → full retract
      (neg-sync follows, no hold) → `TC:` → pickup/`TS:1`
- [ ] Regression: held must not block hard-brake during actual feed; stab
      still re-centers buffer while held
- [ ] Build + commit + push

## 7. Close-out
- [ ] Full regression review per design.md Validation
- [ ] Update `openspec/specs/toolchange-orchestration` + `klipper-integration`
- [ ] Archive change
