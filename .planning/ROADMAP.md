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
**Plans**: 1 plan in progress (09-01)

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
