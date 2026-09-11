## 1. Reproduce in the sim first (TDD)

- [x] 1.1 Built `reload_fast_runout_rail_guard_race` (high 20mm/s demand,
      jam+sensor-clear at t=5000, otherwise same as
      `reload_genuine_runout_escalation` but without its low-demand
      workaround) — saturates the tension rail by t=6000, well inside
      `CONF_PSF_WALL_SAT_MS` (1000ms).
- [x] 1.2 Confirmed RED against pre-fix firmware: 656 `SYNC,FAULT_HOLD`
      events, zero `RUNOUT`, zero `RELOAD` in a 23s/1150-tick
      invariant-safe run — matches the real rig's ~6.4s loop period
      closely. Assertion added to `scripts/test_sync_sim.py`
      (`test_fast_runout_escalates_via_rail_guard_not_fault_hold_loop`)
      expecting the fixed behavior before the fix landed.

## 2. Fix firmware (sync.c)

- [x] 2.1 Extracted the runout-escalation check + action from
      `sync_check_tension_dwell_and_ramp` into shared static helper
      `sync_try_runout_escalation(lane_t *lane, uint32_t now_ms)`.
- [x] 2.2 `sync_tick_type_p_rail_guard` (now threaded with `lane_t *lane`,
      one call site updated in `sync_tick_gated_checks`) calls the helper
      in its tension-rail fault-hold branch before `sync_fault_hold()`.
- [x] 2.3 `sync_check_tension_dwell_and_ramp` calls the same helper — no
      duplicated logic.

## 3. Verify

- [x] 3.1 `reload_fast_runout_rail_guard_race` now passes: `RUNOUT,1` ->
      `RELOAD:SWITCHING,1->2` at t=7040 (same tick the fault-hold used to
      fire), `RELOAD:JOINING,2` at t=17060, zero `FAULT_HOLD`.
- [x] 3.2 `reload_genuine_runout_escalation` (slow-demand, dwell path)
      unaffected: identical `RUNOUT,1`/`RELOAD:SWITCHING,1->2` at t=13300,
      `RELOAD:LOADED,2` at t=24860, zero `FAULT_HOLD` — no regression.
- [x] 3.3 Full regression: clean `build_sim` rebuild (warning-free),
      37/37 `unittest scripts.test_sync_sim -v` green, `git diff
      --name-only main -- firmware/` shows only `sync.c`.
- [x] 3.4 `ninja -C build_local` (dev-tuning superset, `FLARE_DEV_TUNING=ON`
      confirmed in cache) — clean, no warnings.
- [ ] 3.5 Hardware validation (type-P rig, folds into
      `audit-reliability-fixes` precedent): reproduce a genuine fast/full
      runout with `RELOAD_MODE=1`, confirm `RUNOUT`/`RELOAD:SWITCHING` fire
      instead of the `FAULT_HOLD` loop observed 2026-07-27. HW-gated, not
      run this session.

## 4. Record

- [x] 4.1 Updated `memories/repo/host-sync-sim.md` with the finding + fix.
- [x] 4.2 Updated `TEST_CASES.md` (8.2 Type-P RELOAD Runout Recovery)
      noting the race and its fix.
