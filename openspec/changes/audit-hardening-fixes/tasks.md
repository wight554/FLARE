# Tasks: audit-hardening-fixes

## 1. Firmware — protocol correctness

- [ ] 1.1 F1: `sync.c:718,863,932` — `cmd_event("EV:BL", ...)` → `cmd_event("BL", ...)`; grep firmware for other `cmd_event("EV` instances (must be zero)
- [ ] 1.2 F6: add critical-event path bypassing `cmd_event_permitted()` budget (keep `stdio_usb_connected()` check); switch `FAULT:*` (motion.c), `CUT:ERROR` (cutter.c), `TC:ERROR` + `RELOAD:FAULT` (toolchange.c), BL `TIMEOUT` (sync.c) to it
- [ ] 1.3 Cleanups: delete dead `Y_TO_BUF_NEUTRAL` (main.c:220); clamp `pos` in RR chained snprintf (protocol_tmc.c:115-124); `TW` value scan `%i` → `%x` with unsigned temp (protocol_tmc.c:60); `STATUS_LINE_MAX` → `CMD_LINE_MAX - 8` (protocol_status.c:16); fix ms clamps misusing `PATH_DIST_MAX_MM` + use/delete dead `RELAY_TRIM_*_MAX_SPS` consts (protocol.c:1049,1076-1078,1103-1107)

## 2. Firmware — guards & validation

- [ ] 2.1 F2: gate `CAL:PSF_*` saves with `controller_activity_in_progress()` → `ER:PERSIST_BUSY` (protocol.c:1726-1740)
- [ ] 2.2 F3: `SET:MICROSTEPS` reject non-power-of-2 (`(v&(v-1))==0`, 1..256) → `ER` (protocol.c:919)
- [ ] 2.3 F4: `settings_load_tmc()` clamp `microsteps` (snap to valid pow2), `full_steps` {200,400}, `gear_ratio`/`rotation_distance` to `TMC_*_MIN/MAX` before `mm_per_step` division (settings_store.c:449-466); guard `tmc_set_stealthchop_sps` against `microsteps <= 0` (tmc2209.c:430)
- [ ] 2.4 F5: `cutter_test_us` returns false unless `CUT_IDLE`/`CUT_BOOT_PARK`; `CP` handler replies `ER:BUSY` on refusal (cutter.c:196, protocol.c:1275)
- [ ] 2.5 F7: `TW` reg==CHOPCONF mirrors value into `tmc->chopconf` (protocol_tmc.c:69-74)
- [ ] 2.6 F8: shared `tc_busy()` gate → `ER:BUSY` for `T:`/`TC:`/`RL:`/`UL:`/`UM:`(active)/`LO:`/`FL:`/`FD:` while TC active; remove silent-no-op-with-OK in `TC:`/`RL:` handlers; `MV:` untouched (protocol.c:1426-1568, toolchange.c tc_start/tc_manual_reload)
- [ ] 2.7 Build gate: `ninja -C build_local` + dev superset (`-DFLARE_DEV_TUNING=ON`) clean

## 3. Scripts — protocol drift

- [ ] 3.1 S4: daemon event whitelist += `RELOAD` (flare_daemon.py:616); flare_cmd event rebuild comma → colon (flare_cmd.py:336-338)
- [ ] 3.2 S3: tuner `EVENT_RE` and `startswith` patterns comma → colon (flare_live_tuner.py:133,582-588)
- [ ] 3.3 Wire-format unit test: literal firmware lines (`EV:SYNC:FAULT_HOLD`, `EV:SYNC:TENSION_RISK_HIGH`, `EV:BUF:EST_FALLBACK`, `EV:BL:TIMEOUT`, `EV:RELOAD:LOADED:1`) against tuner patterns + daemon split logic

## 4. Scripts — security & ownership

- [ ] 4.1 S1: daemon `--host` default `0.0.0.0` → `127.0.0.1`; startup warning when binding non-loopback (flare_daemon.py:1333)
- [ ] 4.2 S5: `exclusive=True` on all `serial.Serial` opens (daemon:595, flare_cmd:461, live_tuner:470/1227, sync_check:850, baseline_recommender:108); catch + actionable conflict message
- [ ] 4.3 S6: gen_config validate `microsteps` pow2 + hard-error `rotation_distance <= 0` (gen_config.py:347,359); error path test in test_gen_config.py

## 5. Scripts — dead guard repair

- [ ] 5.1 S2a: parity script scans all `settings_load\w*` function bodies (test_settings_parity.py:26,70-76); verify it passes on current tree after fix
- [ ] 5.2 S2b: wrap parity check in `unittest.TestCase` so `unittest discover` executes it; confirm failure injection (temporarily comment a load line) fails `validate_regression.sh`

## 6. Docs — drift fixes

- [ ] 6.1 D1: comma → colon event format, all 13 sites (MANUAL.md:226,227,234,308; BEHAVIOR.md:179,187,189,193,407,462,473,502,506)
- [ ] 6.2 D2+D3: BEHAVIOR.md:193 `EV:BL,WATCHDOG` → `EV:BL:TIMEOUT`; MANUAL.md events table add BL family (`PRIME`,`LOCKED`,`FOLLOW`,`FOLLOW_DONE`,`FOLLOW_GATED`,`PRIME_BOUND`,`TIMEOUT`), `CUT:DONE`/`CUT:ERROR`, `BUF_STAB` `REVERSE`, `SYNC` subtypes (`RELIEF_PAUSE`,`NEUTRAL_CREEP_CAP`,`cannot_refill`,`cannot_relieve`)
- [ ] 6.3 D4+D5: KLIPPER.md document daemon HTTP API (port 8088, loopback default, explicit `--host` LAN opt-in, endpoints); fix KLIPPER.md:22 false symlink claim to match `install_daemon.sh` reality
- [ ] 6.4 D6+D7: CONTEXT.md:31 + MANUAL.md:374 tuner wording → "observe-only by default; guarded SET writes via --allow-* flags"; MANUAL.md CAL rows add `ER:PERSIST_BUSY`, CP row add busy rejection, `T:`/`TC:`/load rows add TC-busy note; note fault-class events exempt from best-effort drop (MANUAL.md:12)

## 7. Validation

- [ ] 7.1 `scripts/validate_regression.sh` full pass
- [ ] 7.2 Hardware checks (TEST_CASES.md additions): CAL during motion → `ER:PERSIST_BUSY`; CP mid-cut → `ER:BUSY` + cut completes; `T:`/`TC:` mid-TC → `ER:BUSY`, sequence unaffected; BL timeout emits single-prefix `EV:BL:TIMEOUT` visible in daemon history; `flare_cmd RL` via daemon exits 0 on completion
