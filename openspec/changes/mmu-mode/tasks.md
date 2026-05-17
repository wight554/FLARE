# Tasks: mmu-mode

Status legend: [ ] todo  [~] in progress  [x] done

## 0. Pre-flight
- [ ] Get `GET:BUF_TRAVEL` half-travel + intended max tip-forming/ramming
      retract from operator → decide HOLD-command necessity.

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
- [ ] Emit `EV:UNLOADED` only after final clear (nosf_cmd.py blocking intact)
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

## 6. (Conditional) Sync HOLD command
- [ ] Only if task 0 shows buffer travel can't absorb tip-forming amplitude
- [ ] Define `SM:` HOLD semantics (freeze motor, ignore estimator, buffer
      absorbs) + release; macro hooks around tip forming

## 7. Close-out
- [ ] Full regression review per design.md Validation
- [ ] Update `openspec/specs/toolchange-orchestration` + `klipper-integration`
- [ ] Archive change
