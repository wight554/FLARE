# Tasks: audit-hardening-fixes

## 1. Firmware — protocol correctness

- [x] 1.1 F1: `sync.c:718,863,932` — `cmd_event("EV:BL", ...)` → `cmd_event("BL", ...)`; grep firmware for other `cmd_event("EV` instances (must be zero)
- [x] 1.2 F6: add critical-event path bypassing `cmd_event_permitted()` budget (keep `stdio_usb_connected()` check); switch `FAULT:*` (motion.c), `CUT:ERROR` (cutter.c), `TC:ERROR` + `RELOAD:FAULT` (toolchange.c), BL `TIMEOUT` (sync.c) to it
- [x] 1.3 Cleanups: delete dead `Y_TO_BUF_NEUTRAL` (main.c:220); clamp `pos` in RR chained snprintf (protocol_tmc.c:115-124); `TW` value scan `%i` → `%x` with unsigned temp (protocol_tmc.c:60); `STATUS_LINE_MAX` → `CMD_LINE_MAX - 8` (protocol_status.c:16); fix ms clamps misusing `PATH_DIST_MAX_MM` + use/delete dead `RELAY_TRIM_*_MAX_SPS` consts (protocol.c:1049,1076-1078,1103-1107)

## 2. Firmware — guards & validation

- [x] 2.1 F2: gate `CAL:PSF_*` saves with `controller_activity_in_progress()` → `ER:PERSIST_BUSY` (protocol.c:1726-1740)
- [x] 2.2 F3: `SET:MICROSTEPS` reject non-power-of-2 (`(v&(v-1))==0`, 1..256) → `ER` (protocol.c:919)
- [x] 2.3 F4: `settings_load_tmc()` clamp `microsteps` (snap to valid pow2), `full_steps` {200,400}, `gear_ratio`/`rotation_distance` to `TMC_*_MIN/MAX` before `mm_per_step` division (settings_store.c:449-466); guard `tmc_set_stealthchop_sps` against `microsteps <= 0` (tmc2209.c:430)
- [x] 2.4 F5: `cutter_test_us` returns false unless `CUT_IDLE`/`CUT_BOOT_PARK`; `CP` handler replies `ER:BUSY` on refusal (cutter.c:196, protocol.c:1275)
- [x] 2.5 F7: `TW` reg==CHOPCONF mirrors value into `tmc->chopconf` (protocol_tmc.c:69-74)
- [x] 2.6 F8: shared `tc_busy()` gate → `ER:BUSY` for `T:`/`TC:`/`RL:`/`UL:`/`UM:`(active)/`LO:`/`FL:`/`FD:` while TC active; remove silent-no-op-with-OK in `TC:`/`RL:` handlers; `MV:` untouched (protocol.c:1426-1568, toolchange.c tc_start/tc_manual_reload)
- [x] 2.7 F9: NULL guard at top of `tc_tick_reload_follow` mirroring `tc_tick_reload_approach` (toolchange.c:526-527 inverted deref-then-check)
- [x] 2.8 F10: zero `g_flow_sched_live_delta[]` on sync OFF transition (design D11); verify learned baseline no longer carries across prints in one power-on session
- [x] 2.9 F11: `g_buf_signal.age_ms` truthful — stamp `g_buf_analog_last_sample_ms` only on actual fresh sample, or compute age from real sample timestamp (sync_buf.c:892/896/910)
- [x] 2.10 Build gate: `ninja -C build_local` + dev superset (`-DFLARE_DEV_TUNING=ON`) clean

## 3. Scripts — protocol drift

- [ ] 3.1 S4: daemon event whitelist += `RELOAD` (flare_daemon.py:616); flare_cmd event rebuild comma → colon (flare_cmd.py:336-338)
- [ ] 3.2 S3: tuner `EVENT_RE` and `startswith` patterns comma → colon (flare_live_tuner.py:133,582-588)
- [ ] 3.3 Wire-format unit test: literal firmware lines (`EV:SYNC:FAULT_HOLD`, `EV:SYNC:TENSION_RISK_HIGH`, `EV:BUF:EST_FALLBACK`, `EV:BL:TIMEOUT`, `EV:RELOAD:LOADED:1`) against tuner patterns + daemon split logic

## 4. Scripts — security & ownership

- [ ] 4.1 S1: daemon `--host` default `0.0.0.0` → `127.0.0.1`; startup warning when binding non-loopback (flare_daemon.py:1333)
- [ ] 4.2 S5: `exclusive=True` on all `serial.Serial` opens (daemon:595, flare_cmd:461, live_tuner:470/1227, sync_check:850, baseline_recommender:108); catch + actionable conflict message
- [ ] 4.3 S6: gen_config validate `microsteps` pow2 + hard-error `rotation_distance <= 0` (gen_config.py:347,359); error path test in test_gen_config.py
- [ ] 4.4 S7: `klipper_motion_tracker.py` wait loops scan `_messages` for matching id instead of requeue-and-poll (design D12); unit test feeding unsolicited message mid-wait must not spin (klipper_motion_tracker.py:130-143)
- [ ] 4.5 S8: `flash_flare.sh` UF2 fallback — use `find_and_mount_rp2` (already-mounted check) before raw `sudo mount`; document macOS limitation or add `/dev/disk*` scan (flash_flare.sh:139-148,320-343)

## 5. Scripts — dead guard repair

- [ ] 5.1 S2a: parity script scans all `settings_load\w*` function bodies (test_settings_parity.py:26,70-76); verify it passes on current tree after fix
- [ ] 5.2 S2b: wrap parity check in `unittest.TestCase` so `unittest discover` executes it; confirm failure injection (temporarily comment a load line) fails `validate_regression.sh`

## 6. Docs — drift fixes

- [ ] 6.1 D1: comma → colon event format, all 13 sites (MANUAL.md:226,227,234,308; BEHAVIOR.md:179,187,189,193,407,462,473,502,506)
- [ ] 6.2 D2+D3: BEHAVIOR.md:193 `EV:BL,WATCHDOG` → `EV:BL:TIMEOUT`; MANUAL.md events table add BL family (`PRIME`,`LOCKED`,`FOLLOW`,`FOLLOW_DONE`,`FOLLOW_GATED`,`PRIME_BOUND`,`TIMEOUT`), `CUT:DONE`/`CUT:ERROR`, `BUF_STAB` `REVERSE`, `SYNC` subtypes (`RELIEF_PAUSE`,`NEUTRAL_CREEP_CAP`,`cannot_refill`,`cannot_relieve`)
- [ ] 6.3 D4+D5: KLIPPER.md document daemon HTTP API (port 8088, loopback default, explicit `--host` LAN opt-in, endpoints); fix KLIPPER.md:22 false symlink claim to match `install_daemon.sh` reality
- [ ] 6.4 D6+D7: CONTEXT.md:31 + MANUAL.md:374 tuner wording → "observe-only by default; guarded SET writes via --allow-* flags"; MANUAL.md CAL rows add `ER:PERSIST_BUSY`, CP row add busy rejection, `T:`/`TC:`/load rows add TC-busy note; note fault-class events exempt from best-effort drop (MANUAL.md:12)
- [ ] 6.5 D8+D9+D10: MANUAL.md add core `?:` status-field table (`LN`..`SC`, field order per protocol_status.c); fix `CW:lane:reg:val` → `TW` (MANUAL.md:102 vs protocol_tmc.c:57); remove stale "appended after `SS:`" wording (MANUAL.md:282)

## 7. Klipper / WebUI integration

- [ ] 7.1 K1: `FLARE_WAIT_UNLOAD` guard `active_gate ∈ {0,1}` before lane sensor/task pick; derive from loaded gate or raise (klipper/mmu.py:897-906)
- [ ] 7.2 K3: daemon force-full `SET_MMU` includes `GATE_COLOR`/`GATE_MATERIAL`/`GATE_SPOOL_ID`/gate names (flare_daemon.py:1206-1216); verify klippy-restart recovers gate map with daemon already up
- [ ] 7.3 K5: replace hardcoded `/15.0` piston scale with half of board `BUF_MAX_TRAVEL` (flare_daemon.py:518,1092,1096,1134)
- [ ] 7.4 K2+K4: wait loops report daemon-unreachable after consecutive failures (mmu.py:911,1047-ish); align mmu.py fallback `bowden_length`/`extruder_to_nozzle` defaults with flare_mmu.cfg (1800/125)
- [ ] 7.5 K6+K8: install_daemon.sh installs (or explicitly instructs copying) `flare_mmu.cfg`; KLIPPER.md reinstall-after-Klipper-update note for extras copies; fix NC color typo (install_daemon.sh:10)
- [ ] 7.6 K7: unify mmu_sensors.py blanking with mmu.py path-cascade model (gear-clear anchored)

## 8. Validation

- [ ] 8.1 `scripts/validate_regression.sh` full pass
- [ ] 8.2 Hardware checks (TEST_CASES.md additions): CAL during motion → `ER:PERSIST_BUSY`; CP mid-cut → `ER:BUSY` + cut completes; `T:`/`TC:` mid-TC → `ER:BUSY`, sequence unaffected; BL timeout emits single-prefix `EV:BL:TIMEOUT` visible in daemon history; `flare_cmd RL` via daemon exits 0 on completion
- [ ] 8.3 Klipper-side checks: klippy restart with daemon up → gate map restored in Fluidd; `FLARE_WAIT_UNLOAD` with no active gate → clean error not premature complete; type-D piston deflection spans full range at configured `BUF_MAX_TRAVEL`
