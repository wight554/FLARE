# Phase 12: Post-Phase 2–10 Regression Fixes — Specification

**Goal**: Close the regressions and decision-note conflicts found by the 2026-09-11 two-axis review of `79f7a95..2a24a33` (Phases 2–10) against `.planning/specs/*/spec.md` and `.planning/decisions/archive/*/design.md`.
**Scope**: Firmware (`toolchange.c`, `cutter.c`, `sync.c`, `protocol.c`, `motion.c`), Klipper (`flare_mmu.cfg`), host tooling (`flare_cmd.py`, `flare_daemon.py`, `flare_analyze.py`, `validate_regression.py`), docs (`MANUAL.md`).
**Source**: review reports summarised in `12-REVIEW.md` (same directory).

Build/test baseline at review time: `ninja -C build_local` PASS, 133 Python tests PASS, `test_persistence`/`test_tmc_recovery` PASS. Every item below is a behavioural or contract regression the green gates did not catch.

---

## 1. Toolchange load park (HIGH)

Ref: `toolchange.c:366-372`; 06-SPEC §4.2; klipper-integration spec ("Klipper `G1 E{load_park_dist}` after MMU push past sensor"); decision klipper-mmu-config-overhaul.

### Requirements
1. `TC_LOAD_PARK` SHALL NOT drive a forward `TASK_MOVE` with the buffer guard active when the load completed via `buf_compression_sane` (buffer already at COMPRESSION → `FAULT:MOVE_COMPRESSION`, `FAULT_BUF` latched, sync refused for the rest of the print via `sync.c:1233`).
2. Park SHALL occur only on the toolhead-sensor low→high edge (06-SPEC §4.2), never on buffer-inferred completion; on type-P it SHALL NOT push against the analog rail.
3. If park is retained, it SHALL either (a) use `move_ignore_buffer` with an explicit compression-limited distance, or (b) be delegated to Klipper (`G1 E` in `_FLARE_CHANGE_LANE`) and `CONF_TC_TS_PARK_MM` default to `0`.
4. `FAULT_BUF` raised inside a toolchange SHALL surface as `EV:TC:ERROR:*`, not `TC:DONE`.
5. Retry exhaustion SHALL emit `EV:TC:ERROR:TS_NOT_HIT` (06-SPEC §4.1), and retry SHALL NOT fire on a `RUNOUT`-stopped load.

---

## 2. Cutter abort servo de-energize (HIGH)

Ref: `cutter.c:178-179, 190-191`; cutter-feed-timeout spec ("cutter_abort() … servo returns to `SERVO_BLOCK_US`"); commit 112f0eb.

### Requirements
1. `cutter_abort()` / `cutter_fail()` SHALL command `SERVO_BLOCK_US` and keep PWM enabled for at least `servo_settle_ms` before `servo_idle()`, matching the settle-then-idle pattern at `cutter.c:255-258, 328-331` (e.g. a `CUT_ABORT_SETTLE` state ticked by `cutter_tick`).
2. The blade SHALL never be left mid-stroke and limp after an abort.

---

## 3. Bare `BL:T` / `BL:C` catch scope creep (HIGH)

Ref: `sync.c:829-831, 957, 969-975, 1020-1031`; decision buffer-state-lock D5 ("reactive catch removed — stall risk, motor commanded to retract against still-taut filament; bare BL = passive lock + optional follow-on"); 02-SPEC ("Type-D preserved identically"); `flare_mmu.cfg:187` `_FLARE_RETRACT_GUARD_BEGIN` without LENGTH.

### Requirements
1. Bare `BL:<side>` (no follow args) SHALL be a passive lock: `g_bl_follow_mm = 0`, no `SYNC_MAX_SPS` seed, no `GLOBAL_MAX_SPS` escalation, on both sensor types.
2. The rate-servo catch SHALL arm only when explicit `follow_mm`/`follow_rate` are supplied (02-02-PLAN scope).
3. Type-D BL behaviour SHALL be byte-identical to pre-Phase-2 (D5); `test_sync_sim.py` SHALL assert bare-BL passivity for D and P.
4. `_FLARE_RETRACT_GUARD_BEGIN` SHALL always pass LENGTH when a catch is intended.

---

## 4. `GET:BUF_POS_RAW` side effects (MEDIUM)

Ref: `protocol.c:509-512`; psf-type-p-sensor spec; `CAL:` activity gate at `protocol.c:1886`.

### Requirements
1. `GET:BUF_POS_RAW` SHALL be read-only: report the last sampled normalized ADC value without calling `buf_analog_update()` (no EMA/velocity injection on type-P, no `g_buf_pos` overwrite on type-D).
2. It SHALL return `ER:` on type-D (`BUF_SENSOR_TYPE=0`).

---

## 5. TMC heartbeat idle lockout & fault storm (MEDIUM)

Ref: `motion.c:684-707`; `controller_activity_in_progress()` `protocol.c:204-217`; 10-SPEC §2.1 "strict idle lockout"; buffer-state-lock (BL_LOCKED lock-break race).

### Requirements
1. The heartbeat SHALL NOT poll while `sync` state is `BL_LOCKED`, `SYNC_ACTIVE`, `RELIEF_PAUSE` or `FAULT_HOLD`, regardless of lane `task` (extend `controller_activity_in_progress()` or add a sync-side gate).
2. Recovery SHALL NOT block the main loop with `sleep_ms`; backoff SHALL be state-machine driven across ticks (10-SPEC §3.2 "50 ms backoff" ≠ blocking).
3. After 3 failed re-applies the lane SHALL latch a fault and stop polling until `CAL:TMC_RESET`/reboot — no 1 Hz `stop_all()` + `TMC:FAULT` storm.
4. `stop_all()` on TMC fault SHALL NOT be issued while a buffer lock is held; it SHALL release the lock explicitly and emit `BL:BREAK`.

---

## 6. `STOP`/`PA` (MMU_PAUSE) clears toolhead latch (MEDIUM)

Ref: `protocol.c:1783-1791` aliases `ST`; 06-SPEC §3.2 ("safe idle/hold without resetting position counters or active lane designation"); toolchange-orchestration "Toolhead clear wait is meaningful".

### Requirements
1. `STOP`/`PA` SHALL halt motion and set `SYNC_OFF` but SHALL NOT call `set_toolhead_filament(false)`; the `TH:1` latch survives to the next `TC:`.
2. Only `ST` (explicit full stop) keeps the legacy clearing behaviour; MANUAL.md SHALL document the difference.

---

## 7. `BS` on already-NEUTRAL buffer force-zeroes position (LOW)

Ref: `sync.c:234`; decision fix-typep-relief-pause-rearm-strand D3 (avoid `buf_force_stable_state(NEUTRAL)` estimator wipe); psf-type-p "measures position directly".

### Requirements
1. `BS` no-op path SHALL emit `BUF_STAB:DONE` without calling `buf_force_stable_state()` on type-P (no `g_buf_pos = 0`, no `g_extruder_est_sps` wipe).

---

## 8. Host: `--dump` no longer rebuilds (HIGH)

Ref: `flare_cmd.py:185` `("BYPASS","bypass",False)`; config-surface-tiers spec:85-89 ("dumped config rebuilds without warnings"); decision tier-config-surface "Migration" (demoted-key manifest, skip on dump); `gen_config.py:277-279` hard exit on unknown key.

### Requirements
1. `BYPASS` SHALL be removed from `DUMP_PARAMS` (runtime state, not a tunable).
2. `test_settings_parity.py` SHALL assert every `DUMP_PARAMS` config key is accepted by `gen_config.validate_known_keys`.

---

## 9. Host: daemon `--host` default vs decision 4.1 (MEDIUM)

Ref: `flare_daemon.py:1671` default `127.0.0.1` (38de2a7); decision audit-hardening-fixes task 4.1 REVERTED 2026-06-19 ("loopback default broke the LAN dashboard … default `0.0.0.0` again"); `scripts/flare_daemon.service` has no `--host`; `--trust-proxy` + loopback → `AUTH_REQUIRED=false` (`:851-870, :905-906`).

### Requirements
1. Record a superseding decision: with Phase-9 bearer auth, choose either `0.0.0.0` default (restores LAN dashboard, auth enforced) or keep `127.0.0.1` AND add `--host 0.0.0.0` to the installed service template. Document in `12-SPEC.md` §9 outcome and MANUAL/KLIPPER docs.
2. `AUTH_REQUIRED` SHALL be true whenever `--trust-proxy` is set, independent of bind host.

---

## 10. Host: mirror staleness after skipped push while Printing (MEDIUM)

Ref: `flare_daemon.py:1631-1636, 1370-1383`; daemon-klipper-mirror spec ("recover within one tick"); `flare_mmu.cfg:402,410` reads `printer.mmu.gate_status` at render time.

### Requirements
1. When a push is skipped because Klipper is `Printing`/gcode-busy, the syncer SHALL re-arm a short retry (≤500 ms), not wait for the next `EV:` or the 10 s reconcile.
2. `parse_status_line` `keys_to_check` SHALL include `lane1_task`/`lane2_task` so `ACTION` updates event-driven.

---

## 11. Host: test collection & analyzer safe-mode (LOW)

Ref: `validate_regression.py:140-143`; decisions docs-test-overhaul D1, prune-dead-diagnostic-scripts ("zero orphans"); static-regression-validation spec; `flare_analyze.py:1266-1274`; analyzer-rigor spec.

### Requirements
1. `test_gcode_marker.py`, `test_gen_config.py`, `test_klipper_motion_tracker.py`, `test_flare_baseline_recommender.py` SHALL be converted to `unittest.TestCase` so `discover` collects them; the two hard-coded runner invocations in `validate_regression.py` SHALL be removed.
2. `flare_analyze.py --chart` without `--out` SHALL still honour the safe-mode refusal (non-zero exit when zero LOCKED buckets).

---

## 12. Docs & style parity (LOW)

Ref: standards review; STYLE.md §2/§3/§4; MANUAL.md:368,370.

### Requirements
1. MANUAL.md `BL` event list SHALL include `BREAK`; `TC:*` row SHALL include `TS_PARKED`, `LOAD_RETRY_RETRACT`, `LOAD_PARK`.
2. Rename `s_tmc_heartbeat_*` → `g_tmc_heartbeat_*` (`motion.c:653-654`), `settings_t_v63` → `settings_v63_t` (`settings_store.c:120`, `test_settings_parity.py:45`); move `<stdio.h>` above project headers in `motion.c:19`.
3. TC_TS clamp bounds SHALL be single-definition constants shared by `protocol.c:1183-1187` and `settings_store.c:560-562`; `toolchange.c:377` `50.0f` fallback SHALL come from `tune.h`.
4. Remove uncalled `settings_validate()` (`settings_store.c:370`) or wire it into load.
5. Add `--- Co-Authored-By` trailer compliance note: task-workflow spec:52 requires the Claude trailer on Claude-assisted commits.

---

## Out of scope
- `settings_store.c` tag→(ptr,size) table refactor (Shotgun Surgery judgement call) — backlog.
- `cut_feed_timeout_ms`/`cut_settle_timeout_ms` persistence (pre-existing tier gap) — backlog.
- buffer-state-lock D2 half-travel prime cap for type-D — pre-existing, needs HW decision.

## HW: validation (never auto-check)
- HW: type-D toolchange with `TC_TS_PARK_MM` default → no `FAULT:MOVE_COMPRESSION`, sync engages after `TC:DONE`.
- HW: cutter abort mid-stroke → blade returns to block before PWM off.
- HW: bare `BL:T` on type-D during retract guard → no motor retract, passive lock only.
