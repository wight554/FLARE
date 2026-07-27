# Tasks: audit-reliability-fixes

## 1. H1 — Type-P RELOAD contact + follow success (toolchange.c)

- [x] 1.1 `tc_tick_reload_approach`: type-P contact → `g_buf_pos > PSF_LOAD_CONTACT_THRESHOLD_NORM` (replace stale `< PSF_HOME_DEVIATION_THRESHOLD_NORM`); remove dead `else if (lane->task == TASK_IDLE)` re-check (toolchange.c:431)
- [x] 1.2 `tc_tick_reload_follow`: type-P success = tension-zone crossing (existing check) gated on `follow_age_ms >= RELOAD_TOUCH_SETTLE_MS + RELOAD_TOUCH_BOOST_MS` (entry-dip window excluded) OR toolhead sensor (any time); NOT a deep-position threshold — the law's JOIN_SPS tension refill (26.7 mm/s vs 2-10 mm/s draw) makes depths below the zone edge unreachable; parity with type-D = success preempts the refill on the same trigger signal. Type-D branch byte-identical (debounced + raw switch); LOAD_MAX fallback unchanged
- [x] 1.3 Delete `PSF_HOME_DEVIATION_THRESHOLD_NORM` (last reference gone) from controller_shared.h
- [x] 1.4 BEHAVIOR.md "RELOAD contact and follow": document type-P contact/success semantics; TEST_CASES.md: add type-P RELOAD hardware case (watch `BP` > +0.5 at contact, deep-tension dip at grab, no instant `RELOAD:LOADED`)

## 2. H2 — Faulted-lane drive interlock (sync.c, toolchange.c, motion.c)

- [x] 2.1 `sync_apply_to_active`: bail out before any drive branch when `lane->fault != FAULT_NONE` (stop TASK_FEED if running); never drive a task-IDLE lane via the raw else branch
- [x] 2.2 `tc_tick_reload_follow` motor-drive else branch: same fault guard (toolchange.c:561-567)
- [x] 2.3 Non-reload RUNOUT on active lane during auto-started sync → `sync_disable(true)` (motion.c runout paths); confirm no double-disable with `reload_trigger`'s NO_FILAMENT abort
- [x] 2.4 Static check: build + grep no remaining `motor_set_rate_sps` call reachable with a faulted lane from background controllers

## 3. H3 — Hardware watchdog (main.c)

- [x] 3.1 `watchdog_enable(1000, true)` after settings/sensor boot init; `watchdog_update()` once per superloop pass
- [x] 3.2 Verify BOOTSEL path (`sleep_ms(100)` + `reset_usb_boot`) and flash save fit inside the budget; note watchdog-reset cause in boot (optional status flag)

## 4. M1 — BL motion vs persistence gate (protocol.c, sync.c)

- [x] 4.1 `controller_activity_in_progress()`: add `sync_buffer_lock_motor_moving()` (PRIME/FOLLOW block persistence; LOCKED hold allowed)
- [x] 4.2 `sync_buffer_lock_follow` dt clamp: saturate at `BL_FOLLOW_DT_MAX_S` instead of collapsing to 1 ms fallback
- [x] 4.3 MANUAL.md: SV/LD/RS/CAL rows note BL-motion rejection

## 5. M2/M3 — Estimator-state hygiene (sync.c, sync_buf.c)

- [x] 5.1 Variance blend → read-path transform feeding `bp_eff`; `g_buf_pos` never written by the control law
- [x] 5.2 Extract single `sync_rearm_active()` helper; use in FAULT_HOLD recovery, RELIEF tick re-arm, RELIEF transition re-arm; route state forcing through `buf_force_stable_state`; drop dead ternary (sync.c:1254)

## 6. M4 — Geometry/goal caching (sync_buf.c)

- [x] 6.1 **WON'T-DO (decided 2026-06-11).** Pure perf, no correctness payoff:
  ~2 soft-float divides at 10 kHz + cheap compares ≈ 1-2% of one core, and the
  audit itself measured headroom sufficient (fixed-point declared unwarranted —
  same logic applies). The cache buys a staleness surface in this codebase's
  worst bug family (cf. stale fault timers, stagnant-anchor no-rearm, saturated
  flag vs rail-break race — all 2026-06 finds). D8's own risk note concedes the
  forgotten-refresh hazard, and `psf_goal_norm` is not pure-config anyway: it
  reads `g_bl_goal_override` (sync_buf.c:431-435), runtime state flipped at BL
  arm/BS/timeout, so invalidation spreads beyond the settings-apply chokepoints
  into motion-state transitions. **Escape hatch if profiling ever demands:**
  hoist to a per-tick local at the hot caller (compute once per loop pass) — no
  global cache, no invalidation protocol.
- [x] 6.2 Moot with 6.1 won't-do (no cache → no parity/refresh assertions needed).

## 7. M5/L7 — Event attribution (motion.c, protocol.c)

- [x] 7.1 `UNLOAD_TIMEOUT` carries lane payload
- [x] 7.2 Manual-unload terminal fault event (`UNLOAD:FAULT:<CUT_FAILED|OUT_BLOCKED>`, critical-class) replacing silent resets
- [x] 7.3 `ER:BUSY:<src>` suffix (TC/LANE/CUTTER/UNLOAD/BL); verify daemon/tuner regex tolerance + wire-format test; MANUAL.md events/errors tables
- [x] 7.4 Daemon two-part event whitelist updated for new event shapes (S4 lesson)

## 8. Low cleanups

- [x] 8.1 L1: ramp BL prime like FOLLOW (no instant SYNC_MAX step)
- [x] 8.2 L2: delete dead prime phase-2 (post-cap) machinery + stale comments
- [x] 8.3 L3: `buf_analog_update` velocity uses actual elapsed ms (pass from `buf_sensor_tick`)
- [x] 8.4 L4: drop `BUF_HOME_STATE` knob (settings field + SET/GET + persistence; SETTINGS_VERSION bump) or document decision to keep reserved; delete dead `PSF_TENSION_PIN_NORM`/`PSF_UNLOAD_RELIEF_ARM_MS`
- [x] 8.5 L5: `RELOAD_Y_TIMEOUT_MS=0` disables Y gate without insta-failing tail-clear wait
- [x] 8.6 L6: CONTEXT.md SETTINGS_VERSION 47 → current
- [x] 8.7 L8: decide `g_buf_signal.age_ms` semantics (fix stamp site or remove field); re-check prior-audit F11 intent

## 9. H4 — Consumer-aware follow completion + RL state-aware resume (toolchange.c, protocol.c)

- [x] 9a.1 `tc_reload_consumer_active()` helper (`g_extruder_est_sps > RELOAD_CONSUMER_MIN_SPS`); add the constant near the other RELOAD defines
- [x] 9a.2 `tc_reload_follow_check_jam`: gate the hard-push wall + "stuck in compression" timeout on consumer present (`consumer && BUF_COMPRESSION`); keep the absolute timeout backstop
- [x] 9a.3 `tc_tick_reload_follow` success: extract `tc_reload_follow_succeeded()`; no-consumer branch completes on `BUF_COMPRESSION` held past `RELOAD_TOUCH_SETTLE_MS + RELOAD_TOUCH_BOOST_MS` (staged at extruder mouth); consumer/type-P/type-D paths unchanged; per-tick fork
- [x] 9a.4 `tc_manual_reload`: state-aware resume — active lane empty + other loaded → `reload_trigger(active)` (swap); else resume approach/follow on active lane
- [x] 9a.5 `RL:` protocol handler: permit active-empty + other-present swap case (bypass same-lane `OTHER_LANE_ACTIVE` guards), keep `ER:NO_FILAMENT` only when neither lane holds filament
- [x] 9a.6 BEHAVIOR.md "RELOAD contact and follow": document the consumer fork (staged-compression completion when idle) + `RL:` resume semantics; MANUAL.md `RL:` row

## 10. Validation

- [x] 10.1 `bash scripts/validate_regression.sh` + dev-superset build (`-DFLARE_DEV_TUNING=ON`)
- [x] 10.2 Type-D rig checks (dry-spin re-fire, watchdog no-fire during SV, BL prime ramp at raised SYNC_MAX) relocated to TEST_CASES.md "Pending Type-D Rig Session" for a single type-D session.
- [ ] 10.3 Hardware (type-P rig): runout RELOAD end-to-end + manual `RL:` — contact at compression, success only on grab (consumer); no instant `RELOAD:LOADED`; **paused/no-consumer `RL:` completes on staged compression (no `FOLLOW_JAM`)**; missed-swap `RL:` (active lane empty, other loaded) resumes via swap
      Sim-level screen (not a substitute — see `host-sync-sim` design.md's authority boundary, sim never satisfies a `HW:` task): `tests/host/sim_scenario.c` scenarios `reload_idle_consumer_staged_completion` and `reload_genuine_runout_escalation` drive the real `toolchange.c` RELOAD state machine end to end and pass — `RELOAD:LOADED` reached via staged compression, no `FOLLOW_JAM`. See `python3 -m unittest scripts.test_sync_sim.ReloadFixEventTests -v` and `memories/repo/host-sync-sim.md`.

## 11. H5 — RL on an already-loaded lane must not restart approach/follow (toolchange.c)

- [x] 11.1 `tc_manual_reload`: after the swap-branch check, short-circuit on `g_toolhead_has_filament` — emit `RELOAD:LOADED` and return without resetting `g_tc_ctx` or starting motion
- [x] 11.2 BEHAVIOR.md / MANUAL.md "RELOAD contact and follow" / `RL:` row: document the already-loaded no-op
- [ ] 11.3 Hardware validation (any rig type, folds into 10.3): re-issue `RL:` immediately after a completed reload (or on a lane that never ran out) with a consumer active — confirm `RELOAD:LOADED` with no motion and no `FOLLOW_JAM`
      Sim-level screen: `tests/host/sim_scenario.c` scenario `reload_already_loaded_noop` calls `tc_manual_reload()` on an already-filament-loaded lane and passes — `RELOAD:LOADED` emitted, no `RELOAD:JOINING`/motion restart. See `ReloadFixEventTests.test_h5_rl_on_already_loaded_lane_is_a_noop`.

## 12. H6 — Genuine type-P runout must escalate to RELOAD, not loop FAULT_HOLD forever (sync.c)

- [x] 12.1 Thread `lane` into `sync_check_tension_dwell_and_ramp`; update its one call site (`sync_tick_calculate_target`)
- [x] 12.2 Before `sync_fault_hold()` on sustained tension dwell: if `lane && g_reload_mode && lane->task == TASK_FEED && tc_state() == TC_IDLE && !lane_in_present(lane) && !lane_out_present(lane)`, emit `RUNOUT`, clear toolhead filament, stop the lane, and call `reload_trigger(lane->lane_id, now_ms)` instead of fault-holding
- [x] 12.3 Build verified (`ninja flare_controller` + dev-tuning superset via `validate_regression.py`); type-D path unchanged (dwell check already `!= BUF_SENSOR_TYPE_D`-gated)
- [x] 12.4 Hardware validation (type-P rig, folds into 10.3): reproduce a real runout during active sync with `RELOAD_MODE=1` and confirm `RUNOUT`/`RELOAD:SWITCHING` fires within ~1 `SYNC_TENSION_DWELL_STOP_MS` window instead of looping `SYNC:FAULT_HOLD`.
      CONFIRMED on real type-P rig, 2026-07-27 (`reload.log`, `/telemetry` SSE capture, `reload_mode=1`/`buf_sensor_type=1`): `RUNOUT,2` -> `RELOAD:SWITCHING,2->1` fire on the same telemetry tick (t=0.40s from the preceding `SYNC:cannot_refill`); `RELOAD:JOINING,1` at t=18.02s (~17.6s approach); `RELOAD:LOADED,1` at t=21.82s. Zero `SYNC:FAULT_HOLD`, zero `FOLLOW_JAM` anywhere in the capture — the loop this task guards against did not occur. Bonus: post-load organic recovery also observed cleanly — `SYNC:cannot_refill` -> `SYNC:RELIEF_PAUSE` (t=26.83s) -> `SYNC:AUTO_START` (t=30.83s), matching the `sync_tick_auto_start_stop` re-arm mechanism validated in the sim two sessions prior (`spec-derived-sim-coverage` task 12).
      Still open: `RELOAD_MODE=0` (confirm the pre-existing FAULT_HOLD loop is unchanged) not run this session — this log only covers `RELOAD_MODE=1`.
      Sim-level screen: `tests/host/sim_scenario.c` scenario `reload_genuine_runout_escalation` (type-P) reproduces a genuine tension-pinned runout and confirms `RUNOUT`/`RELOAD:SWITCHING` fire with zero `SYNC,FAULT_HOLD` events — matches the real-rig sequence above closely (same event names, same zero-fault-hold outcome; sim doesn't chain the post-load RELIEF_PAUSE/AUTO_START recovery, that's covered generically elsewhere). Found in the process: the escalation and the pre-existing `CONF_PSF_WALL_SAT_MS`-based saturation fault-hold race on demand rate (a full/fast jam saturates and trips the 1 s wall-timeout before the 6 s dwell timer can act) — worth confirming on rig at realistic print speeds, not just this scenario's deliberately slow demand.
