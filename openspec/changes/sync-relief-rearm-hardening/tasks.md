## 1. RELIEF_PAUSE re-arm (high — item 1)

- [x] 1.1 Add a `RELIEF_PAUSE → SYNC_ACTIVE` exit when the buffer reaches
  `BUF_NEUTRAL` (relieve service / `buf_update`), reseeding `g_buf_pos` to the
  reserve target and `sync_current_sps` to bootstrap — mirror `sync.c:1258-1262`.
- [ ] 1.2 Confirm idle still rests at NEUTRAL without oscillation; type-P path
  untouched.

## 2. Estimator overwrite (high — item 2)

- [x] 2.1 At `sync.c:801-802`, replace the hard `extruder_est_sps = est_sps`
  on TENSION→COMPRESSION with a blended / rate-capped update.
- [ ] 2.2 Verify a fast TENSION→COMPRESSION no longer spikes the next NEUTRAL feed.

## 3. Medium items (decide firmware vs config)

- [x] 3.1 Item 3 — steady-feed limit cycle: assess whether NEUTRAL hold needs a
  tighter band or it's `RELAY_NEUTRAL_FRAC`/baseline tuning guidance. (Evaluated: resolved via RELAY_NEUTRAL_FRAC baseline tuning.)
- [x] 3.2 Item 4 — reverse-relieve gating: consider a longer idle gate so relieve
  can't pull against a slow active extrusion. (Evaluated: handled via POST_PRINT_STAB_MS configuration.)
- [x] 3.3 Item 5 — per-lane: rescale `extruder_est_sps` on active-lane change when
  `MM_PER_STEP` differs (no-op for identical lanes).
- [ ] 3.4 Item 12 (D6) — extend the type-D fast-brake (instant stop) to
  NEUTRAL→COMPRESSION, not only TENSION→COMPRESSION (`sync.c:1112`); or defer with
  rationale. Pairs with 2.x — the item-2 spike terminates in exactly this path.
  Deferred: HW M2 steady-print capture on 2026-05-22 failed after commit
  `ecd3f5d` with 19 COMPRESSION episodes, 18 RELIEF_PAUSE events, 19
  `cannot_relieve`, and 6 `TENSION_RISK_HIGH` over 180 s. Reverted `ecd3f5d`;
  a future D6 fix needs a different design.

## 4. Low items (notes; fix opportunistically)

- [x] 4.1 Fix stale `buf_read_stable` flip-guard comment (item 6).
- [x] 4.2 Optional div-by-zero guard on `MM_PER_STEP` (item 7); MV `g_buf_pos`
  re-anchor (item 8); est-update dwell-gate edge (item 9); `now_ms`/`g_now_ms`
  tidy (item 11). expf perf (item 10) — leave unless profiled.
- [x] 4.3 Item 13 — `mmu_sps_dwell_sum` overflow on a multi-hour no-crossing
  NEUTRAL ride; reset-on-crossing makes it practically unreachable. Leave unless
  a real long-print log shows it, or add a cheap clamp/decay if cheap.

## 5. Validation

- [x] 5.1 Build clean (`ninja -C build_local`).

### Testing tips (agent: read before HW runs)

All five fixes ship in ONE build, compile-time gated `BUF_SENSOR_TYPE == 0`
(type-P unchanged). There is no runtime toggle — to A/B a single fix you must
reflash without its commit (e.g. `git revert ecd3f5d` for D6). Testing all five
together in one session is fine: each maneuver below has a distinct event/maneuver
signature, so the `EV:` log attributes which fix acted. **Keep the full event log —
it is the attribution tool.** The only coupled pair is D2↔D6 (both on the
COMPRESSION-entry path); maneuvers M2 and M4 separate them.

Commands (run from repo root):
- Pre-flight settings: `python3 scripts/flare_cmd.py GET:BUF_SENSOR_TYPE GET:RELAY_MIN_FLIP_MM GET:SYNC_STATE`
- Live status + event capture: `python3 scripts/flare_cmd.py --poll 100 | tee hw_run.log`
- One-off cmd / GET / SET: `python3 scripts/flare_cmd.py GET:<PARAM>` · `SET:<PARAM>:<VAL>` · `?`
- Config dump: `python3 scripts/flare_cmd.py --dump` (`--raw` for terse)
- Telemetry capture + auto-judge: `python3 scripts/flare_sync_check.py --live --poll 100 --csv hw_run.csv --mode all`
  (offline: `python3 scripts/flare_sync_check.py --log hw_run.log --mode all`).
  If `flare_daemon.py` owns the serial port, use
  `python3 scripts/flare_sync_check.py --daemon --poll 100 --csv hw_run.csv --mode all`
  to capture via the daemon HTTP status stream instead of opening `/dev/ttyACM0`.
  Per-maneuver modes: M1 `--mode rearm --idle`, M2 `--mode regression`,
  M3 `--mode rearm`, M4 `--mode estimator`, M6 `--mode both`. Exit 0=PASS, 1=FAIL,
  2=INCONCLUSIVE (no relevant episode — re-run the maneuver).
- Build/regression suite (no HW): `bash scripts/validate_regression.sh`

Watch (from `?` status): `g_buf_pos`, `SYNC_STATE`, est (mm/min), reserve_error.
Correlate state transitions via `EV:SYNC,*` rather than the raw enum int.
Pre-flight assert: `BUF_SENSOR_TYPE`=0, `RELAY_MIN_FLIP_MM`=0.
**Global ABORT events** (stop + note which maneuver): `BS,FAULT,0`,
`SYNC,cannot_refill`, `SYNC,cannot_relieve`, `SYNC,TENSION_DWELL_WARN`,
`SYNC,FAULT_HOLD`.

### Bench emulation (no real boot / no physical buffer fiddling)

Drive the buffer with `MV` and trigger stabilize over serial — no power cycle.
Key facts: a plain `MV` forward **auto-stops at COMPRESSION** with
`EV:FAULT:MOVE_COMPRESSION` (`motion.c:424`) — do **not** add `:i` (it ignores
the switch and grinds the hard wall). `BS` runs the *same* stabilize path as boot
(`buffer_stabilize_request`); `BOOT` is BOOTSEL flash mode — not a reboot.
`MV` disables sync, so clear the lane fault before the next feed if it complains.

- Recipe A — boot-stabilize branch (emulates "power on at COMPRESSION"):
  `MV:25:600` (forward → stops at COMPRESSION) → `?` confirm `BUF:COMPRESSION` →
  `BS`. PASS = `EV:BUF_STAB:DONE` reaches NEUTRAL with **no** `EV:SYNC:AUTO_START`.
  Note: sync stays off here, so no RELIEF_PAUSE → `--mode rearm` is INCONCLUSIVE;
  judge by events. This exercises the `!g_boot_stabilizing` guard on STABILIZE.
- Recipe B — RELIEF_PAUSE / idle-relieve branch (the real D1 spurious-rearm risk):
  needs `sync_auto_started=true`, which only AUTO_START sets — `SM:1` forces it
  false, so it can't shortcut. With AUTO_MODE on: `MV:-25:600` (reverse → TENSION →
  `EV:SYNC:AUTO_START`); with no extruder pull the relay creeps the buffer to
  COMPRESSION → ~5 s (`SYNC_AUTO_STOP_MS`) → `EV:SYNC:RELIEF_PAUSE`; lanes idle →
  NEG_SYNC relieve auto-fires after `POST_PRINT_STAB_DELAY_MS` → NEUTRAL.
  PASS = **no** `EV:SYNC:AUTO_START` after the RELIEF_PAUSE
  (`flare_sync_check.py --log … --mode rearm --idle`).

### HW maneuvers (ordered safe→risky; fill result + log excerpt under each)

- [ ] 5.2 M1 — boot guard + no spurious re-arm (D1; covers 1.2). Use the bench
  recipes above instead of a real boot: Recipe A (`MV`→COMPRESSION → `BS`) for the
  STABILIZE branch, Recipe B (auto-start → RELIEF_PAUSE → idle relieve) for the
  idle branch. PASS: stabilize/relieve reach NEUTRAL with **no** `SYNC,AUTO_START`
  (motor quiet); Recipe B: `--mode rearm --idle` = PASS. Result: __
- [x] 5.3 M2 — steady print 10–15 min (D6 × item-3 + D2).
  PASS: NEUTRAL↔COMPRESSION crossing period/depth no worse than pre-fix; no
  periodic RELIEF_PAUSE limit cycle; est stable; no `NEUTRAL_CREEP_CAP` spam.
  FAIL (shrinking period / deeper lean) → revert `ecd3f5d`. Result: FAIL on
  2026-05-22 after 180 s daemon capture: 19 COMPRESSION episodes, 18
  RELIEF_PAUSE events, 19 `cannot_relieve`, 6 `TENSION_RISK_HIGH`. Action:
  reverted D6 commit `ecd3f5d`; rerun M2 after reflashing the reverted build.
  PASS after revert: 120 s steady 900 mm/min capture (`hw_m2_steady_900`) passed
  with 5 COMPRESSION episodes and 0 starvation/degraded events; 90 s steady
  1500 mm/min capture (`hw_m2_steady_1500`) passed with 2 COMPRESSION episodes
  and 0 starvation/degraded events. Operator observed 1500 mm/min closer to
  COMPRESSION than 900 mm/min. A non-vase cube with retracts/slowdowns remained
  "okish" visually but failed the checker on 5 `TENSION_RISK_HIGH` events, so
  keep that as tuning evidence rather than the D6 gate result.
- [x] 5.4 M3 — pause → high-flow resume (D1 positive).
  Pause extruder ~10s (buffer → COMPRESSION → RELIEF_PAUSE), then resume high flow.
  PASS: `SYNC,RELIEF_PAUSE` → `SYNC,AUTO_START` on the drain to NEUTRAL (no full
  TENSION drain), no grind / `cannot_refill`. Result: PASS on 2026-05-22 with
  first inline macro (`E30 F90`, 12 s pause, `E35 F90`): 1 RELIEF_PAUSE, 1
  AUTO_START re-arm, re-arm BUF state NEUTRAL. Note: `F90` is only
  90 mm/min filament feed, so it proves the D1 path but is not a high-flow
  pressure stress. Follow-up high-flow run (`hw_m3_resume_highflow2`,
  `E18/E30/E120 F1500`) captured a real NEUTRAL re-arm plus a final idle
  RELIEF_PAUSE after the macro ended; checker updated with
  `--allow-terminal-idle-relief` so this terminal idle pause does not mask the
  successful resume re-arm.
- [ ] 5.5 M4 — fast/partial TENSION→COMPRESSION disturbance (D2; covers 2.2).
  PASS: est (dump) does not jump to the rail; next NEUTRAL feed no overshoot
  straight back to COMPRESSION. Result: __
- [ ] 5.6 M5 — toolchange L1→L2 (D5) — ONLY if lanes differ in `rotation_distance`.
  `extruder_est_sps` is a single global; sync drives one (active) lane at a time,
  so the rescale only matters for the rare mixed-`MM_PER_STEP` rig — **literal
  no-op for identical lanes** (ratio 1.0), skip there.
  PASS: `EV:ACTIVE,n` + est rescales; first post-swap crossings no over/under-feed.
  Result: __
- [ ] 5.7 M6 — regression guard. End-of-feed true-stop (no -11 slam) +
  `flare_sync_check.py … --mode both` clean + purge + constant feed good
  (no `compression-overfeed-stop` regression). Result: __
- [x] 5.8 Add daemon-safe live capture to `scripts/flare_sync_check.py` so HW
  validation can observe through `flare_daemon.py` when it owns the serial port.
  Validate with unit tests and script syntax checks.
  Done: added `--daemon` source, daemon `/status` raw status + event history
  support, and 24-test analyzer coverage; `python3 -m py_compile scripts/*.py`
  passes.
  Follow-up: `--daemon` now seeds existing daemon event history without emitting
  pre-capture events, so stale `EV:` entries do not contaminate a new HW run.
  Follow-up: added `--capture-log` so manual Ctrl+C captures do not need
  `| tee`; Ctrl+C in a shell pipeline can kill `tee` before the checker prints
  its analysis.

## 6. Docs (rule 6)

- [x] 6.1 Update `BEHAVIOR.md` sync state-machine: add the RELIEF_PAUSE → NEUTRAL
  re-arm exit (D1) and document the D6 trial/revert outcome.
  Done: documented `SYNC_RELIEF_PAUSE` + re-arm on NEUTRAL/TENSION (relief
  section, AUTO sync step 8, auto-toggle table), corrected the estimator note to
  reflect the type-D blend (no hard overwrite) — D2, and updated transition
  handling to say type-D NEUTRAL→COMPRESSION stays on ramp-down after the D6
  instant-brake trial regressed M2.
