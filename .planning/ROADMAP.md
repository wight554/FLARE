# Roadmap: FLARE

## Overview

Development roadmap for FLARE firmware, sync buffer controls, and host tooling, incorporating active work streams migrated from historical OpenSpec changes.

## Phases

- [ ] **Phase 1: Hardware Validation & Audit Closeout** - Execute pending bench tests (`HW:`) for audit hardening, reliability fixes, flash wear visibility, and PSF runout escalation race
- [x] **Phase 2: Buffer Retract & Catch Hardening** - Implement 55-task Type-P buffer retract catch and settle hardening (`bl-retract-catch-hardening`)
- [x] **Phase 3: Klipper Event-Driven Mirror & Slicer Purge** - Complete event-driven `flare_daemon` push and OrcaSlicer transition purge tuning
- [x] **Phase 4: Host Sync Simulation Coverage** - Finalize `flare_sim` scenario derivation and regression coverage
- [x] **Phase 5: Automated Calibration & Live Tuning** - Deterministic sensor calibration routines and live serial tuning CLI
- [x] **Phase 6: Advanced Toolchange & RELOAD Automation** - Mechanical cutter sequencing, spool failover, and filament bypass mode
- [x] **Phase 7: Flash Ping-Pong Atomic Persistence** - Dual A/B ping-pong sectors with sequence arbitration and brownout recovery
- [x] **Phase 8: Settings TLV / Delta Schema Migration** - Non-destructive schema evolution via packed Tag-Length-Value encoding with v63 lazy migration
- [x] **Phase 9: Daemon Security & Remote Command Hardening** - Bearer token authentication, loopback exemption, per-client token-bucket rate limiting on serial commands, and WebUI/CLI token integration
- [x] **Phase 10: TMC2209 Register Heartbeat & Auto-Recovery** - Idle-loop CHOPCONF sentinel verification, 1000ms alternating cadence, strict motion lockout, brownout auto-recovery, and host event escalation
- [x] **Phase 11: Firmware Forensics & Main Loop Jitter Instrumentation** - Retention RAM blackbox crash logging across watchdog resets, GET:CRASHLOG retrieval, and high-resolution loop jitter benchmarking
- [x] **Phase 12: Post-Phase 2–10 Regression Fixes** - Close spec/decision-note regressions from the 2026-09-11 review of `79f7a95..2a24a33` (load park FAULT_BUF, cutter abort limp servo, bare-BL catch creep, heartbeat lockout, --dump rebuild)
- [ ] **Phase 13: Type-P Sync Relief & Fault Trip** - Happy-Hare borrow: bounded relief snap (≤1.33× demand) replacing snap-to-max, distance-based (mm) pegged-fault trip with arm-after-first-transition, firmware-local feed probe for the +1.0 tension ambiguity
- [ ] **Phase 14: Klipper MMU Status Parity** - Happy-Hare v4 / Fluidd / Mainsail `develop` parity: `flowguard` dict, missing `printer.mmu` keys, `happy_hare_version`, dialog command stubs, richer `action` strings, status-schema test
- [ ] **Phase 15: Daemon Moonraker Lane Data & Maintenance Counters** - Moonraker `lane_data` push for OrcaSlicer and persisted maintenance counters (blade cuts, swaps) with warning/limit thresholds
- [ ] **Phase 16: TMC Tension Current Boost** - Happy-Hare tangle prevention: raise gear IRUN while type-P tension is pegged during active sync, routed through the heartbeat shadow registers

## Phase Details

### Phase 1: Hardware Validation & Audit Closeout
**Goal**: Verify code-complete firmware and protocol fixes on physical test rig
**Depends on**: Nothing
**Requirements**: REQ-project-architecture, REQ-motion-safety, REQ-persistence-contract
**Backlog Reference**: `.planning/backlog/audit-hardening-fixes/`, `audit-reliability-fixes/`, `flash-wear-visibility/`, `psf-runout-escalation-race-fix/`
**Success Criteria** (what must be TRUE):
  1. `CAL` issued during motion returns `ER:PERSIST_BUSY` without corrupting flash
  2. Cutter feed timeout and cut interruption return clean `ER:BUSY` with cut completed
  3. Type-P runout RELOAD completes end-to-end on rig with staged compression grab
  4. Flash wear counter persists accurately across boot cycles on real RP2040 flash
  5. Genuine runout escalates immediately to RELOAD without race conditions
**Plans**: TBD

### Phase 2: Buffer Retract & Catch Hardening
**Goal**: Eliminate catch slips, overruns, and buffer instability during Type-P retracts
**Depends on**: Phase 1
**Requirements**: REQ-sync-refactor, REQ-buffer-state-lock, REQ-psf-type-p-sensor
**Backlog Reference**: `.planning/backlog/bl-retract-catch-hardening/` (55 tasks)
**Success Criteria** (what must be TRUE):
  1. Print-end retracts complete without losing virtual buffer position calibration
  2. `_FLARE_BUFFER_STABILIZE` settle timing dynamically adapts to feedrate
  3. Feed catch state machine handles pauses without triggering `FOLLOW_JAM`
**Plans**: TBD

### Phase 3: Klipper Event-Driven Mirror & Slicer Purge
**Goal**: Minimize host serial overhead and support dynamic slicer purge volumes
**Depends on**: Nothing (Host tooling)
**Requirements**: REQ-daemon-klipper-mirror, REQ-klipper-mmu-config, REQ-klipper-integration
**Backlog Reference**: `.planning/backlog/klipper-mirror-event-driven/`, `klipper-slicer-purge-and-load-tuning/`
**Success Criteria** (what must be TRUE):
  1. `flare_daemon.py` pushes `SET_MMU` updates on events only, dropping 4Hz constant polling
  2. OrcaSlicer per-transition purge volume overrides default flush lengths dynamically
  3. Fluidd / Mainsail MMU panel maintains real-time state without serial contention
**Plans**: TBD

### Phase 4: Host Sync Simulation Coverage
**Goal**: Full host-side regression coverage for all sync and reload scenarios
**Depends on**: Phase 2
**Requirements**: REQ-static-regression-validation, REQ-sync-state-model
**Backlog Reference**: `.planning/backlog/host-sync-sim/`, `spec-derived-sim-coverage/`
**Success Criteria** (what must be TRUE):
  1. `flare_sim` exercises all 16 Type-P PSF control law scenarios in headless CI
  2. Multi-lane RELOAD edge cases (H4/H5/H6) simulate without physical rig
  3. Regression script gates commits on full scenario pass
**Plans**: TBD

### Phase 5: Automated Calibration & Live Tuning
**Goal**: Rapid, reproducible operator tuning without manual header editing
**Depends on**: Phase 1
**Requirements**: REQ-calibration-workflow, REQ-live-tuner, REQ-analyzer-rigor
**Success Criteria** (what must be TRUE):
  1. Calibration wizard measures and sets Type-P ADC thresholds automatically
  2. Live tuner CLI allows adjusting PSF proportional gains over USB CDC
  3. Trace analyzer converts raw logs into step-rate vs buffer displacement charts
**Plans**: 1 plan complete (05-01)

### Phase 6: Advanced Toolchange & RELOAD Automation
**Goal**: Production-grade MMU multi-colour printing and spool runout reliability
**Depends on**: Phase 2, Phase 3
**Requirements**: REQ-toolchange-orchestration, REQ-cutter-feed-timeout, REQ-filament-bypass
**Success Criteria** (what must be TRUE):
  1. Toolchanges execute with servo/stepper cutter synchronization
  2. Standby spool pre-heats and loads seamlessly upon primary runout
  3. Filament bypass switch allows external spool feeding without MMU lock
**Plans**: 1 plan complete (06-01)

### Phase 7: Flash Ping-Pong Atomic Persistence
**Goal**: Power-loss resilient atomic settings persistence for RP2040 NOR flash using dual ping-pong sectors (A/B)
**Depends on**: Phase 1
**Requirements**: REQ-persistence-contract
**Success Criteria** (what must be TRUE):
  1. Dual 4 KB sectors (0x1FE000 and 0x1FF000) alternate saves atomically
  2. Monotonic sequence counter with signed difference arbitrates newest valid sector on boot
  3. CRC32 verification detects corrupted or truncated writes and recovers prior valid sector cleanly
  4. Write readback verification prevents active sector pointer flip on flash programming failure
**Plans**: 1 plan complete (07-01)

### Phase 8: Settings TLV / Delta Schema Migration
**Goal**: Non-destructive schema evolution for RP2040 NOR flash persistence to prevent operator calibration wipes on firmware upgrades
**Depends on**: Phase 7
**Requirements**: REQ-persistence-contract
**Success Criteria** (what must be TRUE):
  1. Packed Tag-Length-Value (TLV) flash encoding with 1024B buffer fits current and future tunables
  2. Legacy v63 flat struct detected and migrated lazily into globals without overwriting flash
  3. Unknown tags skipped safely without parser bounds overflows or crashes
  4. Global CRC32 and sequence arbitration maintain ping-pong atomicity
  5. Parity tests enforce full enum tag coverage across defaults, load, and save
**Plans**: 1 plan complete (08-01)

### Phase 9: Daemon Security & Remote Command Hardening
**Goal**: Secure remote daemon interfaces against unauthenticated actuation and serial buffer flooding without breaking local Klipper shell helpers or web dashboards
**Depends on**: Nothing (Host tooling)
**Requirements**: REQ-daemon-security
**Success Criteria** (what must be TRUE):
  1. Loopback callers (127.0.0.1/::1) execute all endpoints and commands unauthenticated with zero friction
  2. Remote callers require `Authorization: Bearer <token>` for mutating POST endpoints (`/cmd`, `/config`, `/gatemap`), returning 401 when missing/invalid
  3. Remote telemetry reads (`GET /status`, `GET /telemetry`, static UI) remain open and unauthenticated for dashboards
  4. Per-client token-bucket rate limiter throttles remote `POST /cmd` bursts to prevent USB serial ringbuffer starvation, returning HTTP 429
  5. WebUI and `flare_cmd.py` integrate seamlessly with Bearer token authentication and auto-discovery
**Plans**: 1 plan complete (09-01)

### Phase 10: TMC2209 Register Heartbeat & Auto-Recovery
**Goal**: Idle-loop TMC2209 register verification, brownout recovery, zero motion-jitter guarantee, and host event escalation
**Depends on**: Nothing
**Requirements**: REQ-tmc-heartbeat, REQ-tmc-motion-lockout, REQ-tmc-recovery-escalation, REQ-tmc-telemetry, REQ-tmc-host-sim
**Success Criteria** (what must be TRUE):
  1. CHOPCONF sentinel polled at 1000ms cadence alternating lanes during idle
  2. Zero UART reads during active motion, sync, cutting, or boot stabilization (strict idle lockout)
  3. Brownout recovers via 3-retry re-apply policy and emits `EV:TMC:RESTORED:<lane>`
  4. Persistent comm failure halts motion via `stop_all()` and emits `EV:TMC:FAULT:<lane>:COMM_FAIL`
  5. ST: status line includes `TMC:<l1_health><l2_health>` and host daemon mirrors telemetry
**Plans**: 1 plan complete (10-01)

### Phase 11: Firmware Forensics & Main Loop Jitter Instrumentation
**Goal**: Uninitialized RAM blackbox crash logging across watchdog resets, GET:CRASHLOG retrieval, and high-resolution loop jitter benchmarking
**Depends on**: Nothing
**Requirements**: REQ-forensics-retention-ram, REQ-forensics-event-sampling, REQ-forensics-watchdog-signal, REQ-forensics-protocol, REQ-loop-jitter-telemetry, REQ-forensics-host-sim
**Success Criteria** (what must be TRUE):
  1. Circular 32-entry blackbox in uninitialized RAM survives watchdog reboots without writing to flash
  2. Boot after watchdog reset emits `EV:CRASH:DETECTED:WATCHDOG` when signature valid
  3. `GET:CRASHLOG` streams chronological event history terminated with `OK:CRASH:END`
  4. `CAL:CRASHLOG_CLEAR` resets retention buffer
  5. `GET:LOOP_STATS` reports loop period, peaks, overruns, and top culprit module
  6. Loop iteration exceeding 15ms emits `EV:WARN:LOOP_LAG:<us>:<module>`
**Plans**: 1 plan complete (11-01)

### Phase 12: Post-Phase 2–10 Regression Fixes
**Goal**: Close every regression and decision-note conflict from the 2026-09-11 two-axis review (`12-REVIEW.md`) so Phases 2–10 code honours `.planning/specs` and `.planning/decisions/archive`
**Depends on**: Phase 11 (touches `main.c`/`protocol.c` concurrently — land after 11-01 commits)
**Requirements**: REQ-tc-park-no-fault, REQ-cutter-abort-settle, REQ-bl-bare-passive, REQ-bufposraw-readonly, REQ-tmc-lockout-strict, REQ-stop-keeps-th-latch, REQ-dump-rebuilds, REQ-daemon-host-decision, REQ-mirror-retry, REQ-test-collection, REQ-docs-style-parity
**Success Criteria** (what must be TRUE):
  1. Type-D toolchange with default `TC_TS_PARK_MM` completes without `FAULT:MOVE_COMPRESSION`; sync applies after `TC:DONE`
  2. `cutter_abort()` holds `SERVO_BLOCK_US` for `servo_settle_ms` before PWM off
  3. Bare `BL:T`/`BL:C` is passive on both sensor types; sim asserts no follow seed
  4. `GET:BUF_POS_RAW` is read-only and `ER:` on type-D
  5. Heartbeat never polls in `BL_LOCKED`/`SYNC_ACTIVE`/`RELIEF_PAUSE`/`FAULT_HOLD`; no blocking `sleep_ms`; fault latches after 3 failures
  6. `STOP`/`PA` preserve the `TH:1` latch
  7. `flare_cmd.py --dump` output rebuilds via `gen_config.py`; parity test enforces it
  8. Daemon bind-host decision recorded, `--trust-proxy` always requires auth, service template consistent
  9. Skipped Klipper push retries ≤500 ms; `lane*_task` triggers sync
  10. All `scripts/test_*.py` collected by `unittest discover`; no hard-coded runners in `validate_regression.py`
  11. MANUAL.md lists `BL:BREAK`, `TC:TS_PARKED`/`LOAD_RETRY_RETRACT`/`LOAD_PARK`; STYLE §2/§3/§4 violations from `12-REVIEW.md` resolved
**Plans**: 1 plan complete (12-01)

### Phase 13: Type-P Sync Relief & Fault Trip
**Goal**: Close the open type-P feed-hunting and tension-ambiguity items by borrowing Happy-Hare v4 sync-controller mechanics that fit FLARE's buffer-only (Klipper-agnostic) control loop
**Depends on**: Phase 12 (touches `sync.c` refill path landed in 12-01); real-print baseline capture of current snap-to-max behaviour first (decision in `typep-feed-hunting`)
**Requirements**: TBD (derive from `.planning/research/2026-09-11-happy-hare-borrow-scan.md` §1.1, §1.2, §1.4)
**Research**: `.planning/research/2026-09-11-happy-hare-borrow-scan.md` — HH `main` @ `ef8431c` (2026-09-08)
**Success Criteria** (what must be TRUE):
  1. Urgent refill in the TENSION soft-wall (`sync.c` refill branch) targets `min(max_sps, est × SYNC_PSF_RELIEF_MULT)` (default 1.33, HH `extreme_relief_frac=0.25`) with a temporarily doubled slew cap, never `max_sps` directly; `g_psf_target_filt` still seeds at demand
  2. New `SYNC_TENSION_STOP_MM` (config.ini + SET/GET + `--dump` + MANUAL.md) accumulates relief motion while pegged and trips `FAULT_HOLD` alongside the existing `sync_tension_dwell_stop_ms`; both arm only after the first observed buffer-state transition in the active-sync window
  3. Bounded firmware-local feed probe distinguishes "home rail, no consumer" from "starved" at type-P +1.0 tension; result exposed in `ST:` and consumed by `mode × filament_present` resolution
  4. `flare_sim` scenarios cover refill-without-overshoot, mm-trip vs ms-trip ordering, and probe outcomes; no regression in the 16 PSF scenarios
  5. Nothing from the type-D relay path (confident estimator, mid-band estimator, EST pivots) is reintroduced; `HW:` items remain unchecked until rig validation
**Plans**: TBD

### Phase 14: Klipper MMU Status Parity
**Goal**: Keep the `klipper/mmu.py` Happy-Hare facade and daemon mirror rendering correctly in current Fluidd/Mainsail/KlipperScreen builds (verified against their `develop` sources)
**Depends on**: Nothing (host tooling); independent of Phase 13
**Requirements**: TBD (derive from research §1.3, §5, §6, §7)
**Research**: `.planning/research/2026-09-11-happy-hare-borrow-scan.md`
**Success Criteria** (what must be TRUE):
  1. `printer.mmu.flowguard` dict (`enabled/active/trigger/level/max_clog/max_tangle`) is published, `level` derived from firmware dwell/saturation timers and 0 whenever sync is inactive; Fluidd and Mainsail FlowGuard meters render
  2. All `printer.mmu` keys read by current Fluidd/Mainsail exist with correct types (`endless_spool_groups/enabled` mapped to RELOAD, `sync_feedback_flow_rate`, `sync_drive`, `reason_for_pause`, `operation`, `last/next_tool`, `is_paused`, `has_bypass`, `unit`, `filament_direction`, `gate_temperature`, `slicer_tool_map`, `espooler`, `drying_state`, `encoder: None`, …) plus `mmu_machine.happy_hare_version`
  3. `MMU_TEST_CONFIG`, `MMU_LED`, `MMU_GRIP`/`MMU_RELEASE`/`MMU_SERVO`, `MMU_PRINT_START`/`MMU_PRINT_END` and the `*_VARS` dialogs register as ack/no-op so panel dialogs don't error
  4. `action` reports HH strings (`Cutting Filament`, `Preload`, `Loading`, `Unloading`, …) derived from existing `EV:` events
  5. A `test_status_fields_exist_before_ready`-style unit test asserts the full status schema before the daemon connects
**Plans**: TBD

### Phase 15: Daemon Moonraker Lane Data & Maintenance Counters
**Goal**: Extend `flare_daemon.py` with the two Happy-Hare Moonraker-side features FLARE lacks — slicer lane discovery and consumable maintenance tracking
**Depends on**: Phase 14 (shares the status/mirror surface)
**Requirements**: TBD (derive from research §5, §6)
**Research**: `.planning/research/2026-09-11-happy-hare-borrow-scan.md`
**Success Criteria** (what must be TRUE):
  1. Daemon pushes `lane_data` to Moonraker in the shape OrcaSlicer reads (`mmu_server.py:1746-1846`), sourced from the existing gate map / Spoolman client
  2. Persisted counters (blade cuts from `EV:CUT:DONE`, swaps, RELOAD failovers) with per-counter `limit`/`warning`/`pause` thresholds, exposed in `/status`, WebUI, and an `MMU_STATS COUNTER=` equivalent; reset command available
  3. Counters survive daemon restart and are covered by unit tests
**Plans**: TBD

### Phase 16: TMC Tension Current Boost
**Goal**: Add Happy-Hare tangle prevention — extra gear-motor torque only while the type-P buffer is pegged in tension during active sync — without breaking the Phase 10 heartbeat/recovery contract
**Depends on**: Phase 13 (probe/fault semantics decide when boost is legitimate); Phase 10 shadow-register contract
**Requirements**: TBD (derive from research §1.5)
**Research**: `.planning/research/2026-09-11-happy-hare-borrow-scan.md`
**Success Criteria** (what must be TRUE):
  1. IRUN rises to `SYNC_TENSION_BOOST_IRUN` when type-P tension ≥ `SYNC_TENSION_BOOST_ON` (HH 0.3) and restores at ≤ `SYNC_TENSION_BOOST_OFF` (HH 0.2), only in `SYNC_ACTIVE`, always restored on unsync/fault/STOP
  2. Boost writes go through `g_shadow_ihold_irun[]` so heartbeat re-apply restores the current (boosted or base) value, never a stale one
  3. Knobs in config.ini / SET / GET / `--dump` / MANUAL.md; `flare_sim` asserts restore on every exit path; `HW:` thermal check on rig before default-on
**Plans**: TBD

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Hardware Validation & Audit Closeout | 0/1 | Awaiting physical rig session | - |
| 2. Buffer Retract & Catch Hardening | 3/3 | Complete | 2026-09-11 |
| 3. Klipper Event-Driven Mirror & Slicer Purge | 1/1 | Complete | 2026-09-11 |
| 4. Host Sync Simulation Coverage | 1/1 | Complete | 2026-09-11 |
| 5. Automated Calibration & Live Tuning | 1/1 | Complete | 2026-09-11 |
| 6. Advanced Toolchange & RELOAD Automation | 1/1 | Complete | 2026-09-11 |
| 7. Flash Ping-Pong Atomic Persistence | 1/1 | Complete | 2026-09-11 |
| 8. Settings TLV / Delta Schema Migration | 1/1 | Complete | 2026-09-11 |
| 9. Daemon Security & Remote Command Hardening | 1/1 | Complete | 2026-09-11 |
| 10. TMC2209 Register Heartbeat & Auto-Recovery | 1/1 | Complete | 2026-09-11 |
| 11. Firmware Forensics & Main Loop Jitter | 1/1 | Complete | 2026-09-11 |
| 12. Post-Phase 2–10 Regression Fixes | 1/1 | Complete (HW pending) | 2026-09-11 |
| 13. Type-P Sync Relief & Fault Trip | 0/? | Not planned | - |
| 14. Klipper MMU Status Parity | 0/? | Not planned | - |
| 15. Daemon Moonraker Lane Data & Maintenance Counters | 0/? | Not planned | - |
| 16. TMC Tension Current Boost | 0/? | Not planned | - |
