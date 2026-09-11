# Roadmap: FLARE

## Overview

Ongoing development and stabilization roadmap for FLARE firmware, sync controls, and host tooling.

## Phases

- [ ] **Phase 1: Core Architecture & Safety** - Firmware safety loops, watchdog, flash persistence, and memory bounds
- [ ] **Phase 2: Sync Buffer & Signal Sensing** - Type-D and Type-P buffer state machines, PSF control laws, and estimators
- [ ] **Phase 3: Toolchange & RELOAD Automation** - Cutter sequencing, runout failover, approach/follow synchronization
- [ ] **Phase 4: Calibration & Deterministic Tuning** - Live tuning CLI, analyzer rigor, and automated sensor calibration
- [ ] **Phase 5: Klipper Integration & Host Daemon** - Serial ownership, event-driven mirror, and macro parity
- [ ] **Phase 6: Simulation & Regression Hardening** - Host sync simulation (`flare_sim`) and dev-tuning verification gates

## Phase Details

### Phase 1: Core Architecture & Safety
**Goal**: Ensure unbreakable motor safety, thermal/stall protection, and safe settings persistence
**Depends on**: Nothing
**Requirements**: REQ-project-architecture, REQ-motion-safety, REQ-persistence-contract
**Success Criteria** (what must be TRUE):
  1. Firmware shuts down lane motors safely upon timeout or stall
  2. Flash writes avoid wear exhaustion and enforce settings versioning
  3. Memory layout and stack bounds remain deterministic across RP2040 cores
**Plans**: TBD

### Phase 2: Sync Buffer & Signal Sensing
**Goal**: Perfect synchronization between extruder feed rate and MMU feeder
**Depends on**: Phase 1
**Requirements**: REQ-sync-refactor, REQ-psf-type-p-sensor, REQ-type-d-dynamic-flow
**Success Criteria** (what must be TRUE):
  1. Virtual buffer position tracks filament reserve accurately in real time
  2. Buffer-lock state machine prevents catch slips and overrun
  3. Dynamic flow adaptation prevents tension spikes
**Plans**: TBD

### Phase 3: Toolchange & RELOAD Automation
**Goal**: Flawless filament changes and automatic spool runout reloading
**Depends on**: Phase 2
**Requirements**: REQ-toolchange-orchestration, REQ-cutter-feed-timeout, REQ-filament-bypass
**Success Criteria** (what must be TRUE):
  1. RELOAD transitions seamlessly through WAIT_Y → APPROACH → FOLLOW
  2. Cutter motor and feed timeouts prevent mechanical jams
  3. Filament bypass mode allows single-spool printing without reload logic
**Plans**: TBD

### Phase 4: Calibration & Deterministic Tuning
**Goal**: Easy operator calibration and reproducible tuning workflows
**Depends on**: Phase 3
**Requirements**: REQ-calibration-workflow, REQ-live-tuner, REQ-analyzer-rigor
**Success Criteria** (what must be TRUE):
  1. Calibration commands automatically discover sensor range and neutral points
  2. Live tuner allows real-time PID/PSF adjustments over serial
  3. Analyzer tools generate actionable metrics from serial trace logs
**Plans**: TBD

### Phase 5: Klipper Integration & Host Daemon
**Goal**: Transparent, non-intrusive integration with Klipper ecosystems
**Depends on**: Phase 4
**Requirements**: REQ-klipper-integration, REQ-daemon-klipper-mirror, REQ-klipper-mmu-config
**Success Criteria** (what must be TRUE):
  1. Host daemon mirrors MMU state directly into Klipper printer objects
  2. Macro execution handles toolchange commands with proper backpressure
  3. Serial port disconnects recover cleanly without stalling prints
**Plans**: TBD

### Phase 6: Simulation & Regression Hardening
**Goal**: Comprehensive host simulation and static validation before flashing hardware
**Depends on**: Phase 5
**Requirements**: REQ-static-regression-validation, REQ-acceptance-gate-semantics
**Success Criteria** (what must be TRUE):
  1. Host sim (`flare_sim`) validates sync state transitions without physical rig
  2. Dev-tuning superset build passes cleanly under clang-tidy and GCC
  3. Python host tools pass strict static analysis and smoke tests
**Plans**: TBD

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Core Architecture & Safety | 0/1 | Not started | - |
| 2. Sync Buffer & Signal Sensing | 0/1 | Not started | - |
| 3. Toolchange & RELOAD Automation | 0/1 | Not started | - |
| 4. Calibration & Deterministic Tuning | 0/1 | Not started | - |
| 5. Klipper Integration & Host Daemon | 0/1 | Not started | - |
| 6. Simulation & Regression Hardening | 0/1 | Not started | - |
