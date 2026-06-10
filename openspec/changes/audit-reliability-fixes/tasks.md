# Tasks: audit-reliability-fixes

## 1. H1 — Type-P RELOAD contact + follow success (toolchange.c)

- [ ] 1.1 `tc_tick_reload_approach`: type-P contact → `g_buf_pos > PSF_LOAD_CONTACT_THRESHOLD_NORM` (replace stale `< PSF_HOME_DEVIATION_THRESHOLD_NORM`); remove dead `else if (lane->task == TASK_IDLE)` re-check (toolchange.c:431)
- [ ] 1.2 `tc_tick_reload_follow`: type-P success branch → deep tension (`g_buf_pos <= -PSF_HOME_THRESHOLD_NORM`) OR toolhead sensor; type-D keeps `BUF_TENSION` switch (debounced + raw); LOAD_MAX fallback unchanged
- [ ] 1.3 Delete `PSF_HOME_DEVIATION_THRESHOLD_NORM` (last reference gone) from controller_shared.h
- [ ] 1.4 BEHAVIOR.md "RELOAD contact and follow": document type-P contact/success semantics; TEST_CASES.md: add type-P RELOAD hardware case (watch `BP` > +0.5 at contact, deep-tension dip at grab, no instant `RELOAD:LOADED`)

## 2. H2 — Faulted-lane drive interlock (sync.c, toolchange.c, motion.c)

- [ ] 2.1 `sync_apply_to_active`: bail out before any drive branch when `lane->fault != FAULT_NONE` (stop TASK_FEED if running); never drive a task-IDLE lane via the raw else branch
- [ ] 2.2 `tc_tick_reload_follow` motor-drive else branch: same fault guard (toolchange.c:561-567)
- [ ] 2.3 Non-reload RUNOUT on active lane during auto-started sync → `sync_disable(true)` (motion.c runout paths); confirm no double-disable with `reload_trigger`'s NO_FILAMENT abort
- [ ] 2.4 Static check: build + grep no remaining `motor_set_rate_sps` call reachable with a faulted lane from background controllers

## 3. H3 — Hardware watchdog (main.c)

- [ ] 3.1 `watchdog_enable(1000, true)` after settings/sensor boot init; `watchdog_update()` once per superloop pass
- [ ] 3.2 Verify BOOTSEL path (`sleep_ms(100)` + `reset_usb_boot`) and flash save fit inside the budget; note watchdog-reset cause in boot (optional status flag)

## 4. M1 — BL motion vs persistence gate (protocol.c, sync.c)

- [ ] 4.1 `controller_activity_in_progress()`: add `sync_buffer_lock_motor_moving()` (PRIME/FOLLOW block persistence; LOCKED hold allowed)
- [ ] 4.2 `sync_buffer_lock_follow` dt clamp: saturate at `BL_FOLLOW_DT_MAX_S` instead of collapsing to 1 ms fallback
- [ ] 4.3 MANUAL.md: SV/LD/RS/CAL rows note BL-motion rejection

## 5. M2/M3 — Estimator-state hygiene (sync.c, sync_buf.c)

- [ ] 5.1 Variance blend → read-path transform feeding `bp_eff`; `g_buf_pos` never written by the control law
- [ ] 5.2 Extract single `sync_rearm_active()` helper; use in FAULT_HOLD recovery, RELIEF tick re-arm, RELIEF transition re-arm; route state forcing through `buf_force_stable_state`; drop dead ternary (sync.c:1254)

## 6. M4 — Geometry/goal caching (sync_buf.c)

- [ ] 6.1 Cache `psf_goal_norm` / `buf_threshold_mm` / `buf_physical_half_travel_mm`; `buf_geometry_refresh()` from settings-apply paths + BL goal-override changes
- [ ] 6.2 Verify bitwise-identical control outputs (status spot-check) + extend parity test to assert refresh on every geometry-affecting SET

## 7. M5/L7 — Event attribution (motion.c, protocol.c)

- [ ] 7.1 `UNLOAD_TIMEOUT` carries lane payload
- [ ] 7.2 Manual-unload terminal fault event (`UNLOAD:FAULT:<CUT_FAILED|OUT_BLOCKED>`, critical-class) replacing silent resets
- [ ] 7.3 `ER:BUSY:<src>` suffix (TC/LANE/CUTTER/UNLOAD/BL); verify daemon/tuner regex tolerance + wire-format test; MANUAL.md events/errors tables
- [ ] 7.4 Daemon two-part event whitelist updated for new event shapes (S4 lesson)

## 8. Low cleanups

- [ ] 8.1 L1: ramp BL prime like FOLLOW (no instant SYNC_MAX step)
- [ ] 8.2 L2: delete dead prime phase-2 (post-cap) machinery + stale comments
- [ ] 8.3 L3: `buf_analog_update` velocity uses actual elapsed ms (pass from `buf_sensor_tick`)
- [ ] 8.4 L4: drop `BUF_HOME_STATE` knob (settings field + SET/GET + persistence; SETTINGS_VERSION bump) or document decision to keep reserved; delete dead `PSF_TENSION_PIN_NORM`/`PSF_UNLOAD_RELIEF_ARM_MS`
- [ ] 8.5 L5: `RELOAD_Y_TIMEOUT_MS=0` disables Y gate without insta-failing tail-clear wait
- [ ] 8.6 L6: CONTEXT.md SETTINGS_VERSION 47 → current
- [ ] 8.7 L8: decide `g_buf_signal.age_ms` semantics (fix stamp site or remove field); re-check prior-audit F11 intent

## 9. Validation

- [ ] 9.1 `bash scripts/validate_regression.sh` + dev-superset build (`-DFLARE_DEV_TUNING=ON`)
- [ ] 9.2 Hardware (type-D rig): dry-spin re-fire after sync restart attempt; watchdog no-fire during SV; BL prime ramp at raised SYNC_MAX
- [ ] 9.3 Hardware (type-P rig): runout RELOAD end-to-end + manual `RL:` — contact at compression, success only on grab; no instant `RELOAD:LOADED`
