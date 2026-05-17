# Tasks: mmu-mode

Status legend: [ ] todo  [~] in progress  [x] done

## 0. Pre-flight
- [x] Buffer half-travel measured: `BUF_HALF_TRAVEL` = 7.8 mm. Tip-forming
      analysis ⇒ HOLD primitive REQUIRED (see proposal "Resolved").

## 1. Tunable rename TC_AUTO_CUT → UNLOAD_CUT
- [ ] `controller_shared.h` extern rename
- [ ] `main.c` global rename
- [ ] `settings_store.c` field rename + `SETTINGS_VERSION` bump
- [ ] `scripts/gen_config.py` key `tc_auto_cut` → `unload_cut`
- [ ] `config.ini` + `config.ini.example` key rename
- [ ] `firmware/include/tune.h` regenerate (CONF_UNLOAD_CUT)
- [ ] protocol.c SET + GET token rename
- [ ] Docs: MANUAL.md / BEHAVIOR.md / KLIPPER.md references
- [ ] `ninja -C build_local` + `py_compile` green; commit + push

## 2. UL: cut-on-unload
- [ ] protocol.c `UL:` multi-phase: clear OUT → CU → clear OUT (cutter enabled)
- [ ] Preserve single-retract path when `!ENABLE_CUTTER || !UNLOAD_CUT`
- [ ] Emit `EV:UNLOADED` only after final clear (flare_cmd.py blocking intact)
- [ ] Regression: RELOAD `TASK_UNLOAD` path must NOT cut
- [ ] Build + commit + push

## 3. UM: entry-conditional cut
- [ ] Flow 1 (OUT/YS present at entry): full UL cycle (cut if enabled) + extend
      reverse until IN clears
- [ ] Flow 2 (OUT clear at entry): no cut, retract until IN clears
- [ ] Build + commit + push

## 4. TC: equivalence
- [ ] Confirm/align TC unload phase == new UL semantics (TC ≡ UL+swap+FL)
- [ ] Gate on renamed `UNLOAD_CUT`
- [ ] Regression: preload, autoload, TC: end-to-end
- [ ] Build + commit + push

## 5. Klipper integration
- [ ] KLIPPER.md `change_lane` macro: extruder tip-form + unload from gears →
      `TC:`; remove `FLARE_UNLOAD` from spool-change path
- [ ] Document tip-forming vs sync tuning (POST_PRINT_STAB_DELAY_MS, BUF travel)
- [ ] Command-light: macro sends NO `TS:1`/`TS:0`/`SM:`; document
      `TS_BUF_MS > 0` as a hard prerequisite (else `TC:` LOAD_TIMEOUT)
- [ ] Validate: `TC:` self-completes via `TS_BUF_MS` and sync auto-starts on
      `BUF_ADVANCE` with no host command

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
