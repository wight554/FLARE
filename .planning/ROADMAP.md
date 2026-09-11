# Roadmap: FLARE

## Overview

Development roadmap for FLARE firmware, sync buffer controls, and host tooling, incorporating active work streams migrated from historical OpenSpec changes.

## Phases

- [ ] **Phase 1: Hardware Validation & Audit Closeout** - Execute pending bench tests (`HW:`) for audit hardening, reliability fixes, flash wear visibility, and PSF runout escalation race
- [ ] **Phase 2: Buffer Retract & Catch Hardening** - Implement 55-task Type-P buffer retract catch and settle hardening (`bl-retract-catch-hardening`)
- [x] **Phase 3: Klipper Event-Driven Mirror & Slicer Purge** - Complete event-driven `flare_daemon` push and OrcaSlicer transition purge tuning
- [ ] **Phase 4: Host Sync Simulation Coverage** - Finalize `flare_sim` scenario derivation and regression coverage
- [ ] **Phase 5: Automated Calibration & Live Tuning** - Deterministic sensor calibration routines and live serial tuning CLI
- [ ] **Phase 6: Advanced Toolchange & RELOAD Automation** - Mechanical cutter sequencing, spool failover, and filament bypass mode

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
**Plans**: TBD

### Phase 6: Advanced Toolchange & RELOAD Automation
**Goal**: Production-grade MMU multi-colour printing and spool runout reliability
**Depends on**: Phase 2, Phase 3
**Requirements**: REQ-toolchange-orchestration, REQ-cutter-feed-timeout, REQ-filament-bypass
**Success Criteria** (what must be TRUE):
  1. Toolchanges execute with servo/stepper cutter synchronization
  2. Standby spool pre-heats and loads seamlessly upon primary runout
  3. Filament bypass switch allows external spool feeding without MMU lock
**Plans**: TBD

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Hardware Validation & Audit Closeout | 0/1 | Not started | - |
| 2. Buffer Retract & Catch Hardening | 0/1 | Not started | - |
| 3. Klipper Event-Driven Mirror & Slicer Purge | 1/1 | Complete | 2026-09-11 |
| 4. Host Sync Simulation Coverage | 0/1 | Not started | - |
| 5. Automated Calibration & Live Tuning | 0/1 | Not started | - |
| 6. Advanced Toolchange & RELOAD Automation | 0/1 | Not started | - |
