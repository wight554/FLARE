---
phase: 15-daemon-moonraker-lane-data-maintenance-counters
plan: 02
subsystem: host-daemon
tags: [maintenance, consumables, cutter-life, klipper-stats, webui, alerts, pause]

# Dependency graph
requires: [15-01]
provides:
  - "Persistent SQLite maintenance_counters table with default seeding (cutter_cuts, swaps, reload_failovers)"
  - "Event increment hooks for EV:CUT:DONE, TC:DONE, and RELOAD failover events"
  - "Threshold warning emission to /status, log, and Klipper console (M118)"
  - "Optional Klipper PAUSE dispatch on limit exceeding when pause==1"
  - "Happy-Hare MMU_STATS COUNTER=... command parity in klipper/mmu.py"
  - "Interactive Maintenance & Consumables dashboard panel in WebUI with progress bars and reset button"
  - "Unit test coverage in scripts/test_flare_daemon_maintenance.py"
affects: [flare_daemon.py, klipper/mmu.py, webui]

# Actuals
actuals:
  tasks: 3
  files: 6

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "SQLite maintenance_counters schema with last_reset ISO timestamp tracking"
    - "Deduplicated pause execution: in-memory lock prevents repeated PAUSE spam on continuous print events"
    - "Graceful RPC/fallback in klipper/mmu.py for MMU_STATS: queries daemon /maintenance with local in-memory fallback"

key-files:
  created:
    - scripts/test_flare_daemon_maintenance.py
  modified:
    - scripts/flare_daemon.py
    - klipper/mmu.py
    - scripts/webui/index.html
    - scripts/webui/app.js
    - scripts/webui/style.css
    - .planning/REQUIREMENTS.md
    - .planning/ROADMAP.md
    - .planning/STATE.md

key-decisions:
  - "Default counters (cutter_cuts with 1000 limit, swaps, reload_failovers) auto-seed on first boot"
  - "cmd_MMU_STATS in klipper/mmu.py acts as a bridge to daemon /maintenance API, allowing console command resets and threshold changes while supporting offline unit test execution"
  - "WebUI renders responsive progress bars showing percentage of limit reached, switching to alert badges and orange/coral colors when nearing or exceeding limits"

requirements-completed:
  - REQ-maintenance-counters-persistence
  - REQ-maintenance-counter-event-hooks
  - REQ-maintenance-counter-threshold-actions
  - REQ-maintenance-klipper-command-parity
  - REQ-maintenance-webui-dashboard
  - REQ-daemon-maintenance-unit-tests

coverage:
  - id: D1
    description: "Maintenance counters persisted in SQLite and surviving daemon restart"
    requirement: "REQ-maintenance-counters-persistence"
    verification:
      - kind: unit
        ref: "scripts/test_flare_daemon_maintenance.py#test_default_counters_seeded_and_persisted"
        status: pass
    human_judgment: false
  - id: D2
    description: "Events increment counters automatically (EV:CUT:DONE, TC:DONE, RELOAD)"
    requirement: "REQ-maintenance-counter-event-hooks"
    verification:
      - kind: unit
        ref: "scripts/test_flare_daemon_maintenance.py#test_event_hooks_increment_counters"
        status: pass
    human_judgment: false
  - id: D3
    description: "Threshold limit exceeded triggers warning and optional PAUSE"
    requirement: "REQ-maintenance-counter-threshold-actions"
    verification:
      - kind: unit
        ref: "scripts/test_flare_daemon_maintenance.py#test_threshold_limit_warning_and_pause_escalation"
        status: pass
    human_judgment: false
  - id: D4
    description: "MMU_STATS COUNTER= parameter handling and listing in klipper/mmu.py"
    requirement: "REQ-maintenance-klipper-command-parity"
    verification:
      - kind: unit
        ref: "scripts/test_flare_daemon_maintenance.py#test_klipper_mmu_stats_command_parity"
        status: pass
    human_judgment: false
  - id: D5
    description: "WebUI dashboard maintenance card with progress bars and reset button"
    requirement: "REQ-maintenance-webui-dashboard"
    verification:
      - kind: unit
        ref: "scripts/webui/index.html & app.js"
        status: pass
    human_judgment: false
---

# Phase 15 Plan 02: Maintenance Counters & WebUI Summary

Implemented persistent consumable lifecycle tracking, automated event increments, threshold alerts with optional Klipper pause escalation, Happy-Hare `MMU_STATS COUNTER=...` command parity, and an interactive WebUI dashboard panel.

## Key Changes
1. **SQLite Maintenance Table & Seeding**:
   - Added `maintenance_counters` table (`name`, `count`, `limit_val`, `warning`, `pause`, `last_reset`).
   - Default entries auto-seeded: `cutter_cuts` (limit 1000), `swaps`, and `reload_failovers`.
   - Exposed `get_maintenance_counters()`, `update_maintenance_counter()`, and `/maintenance` HTTP endpoints.
2. **Automated Event Hooks & Escalation**:
   - `EV:CUT:DONE` increments `cutter_cuts`.
   - `TC:DONE` increments `swaps`.
   - `EV:RELOAD:*` increments `reload_failovers`.
   - Reaching `limit_val` emits `M118` console warning, sets warning in `/status`, and if `pause==1`, sends `PAUSE` script to Klipper via Moonraker.
3. **Klipper Command Parity**:
   - `cmd_MMU_STATS` supports `COUNTER=<name>`, `INCR=<n>`, `LIMIT=<n>`, `WARNING=<text>`, `PAUSE=<0|1>`, `RESET=<1>`, `DELETE=<1>`.
   - Bare `MMU_STATS` or `SHOWCOUNTS=1` lists maintenance counter states.
4. **WebUI Dashboard**:
   - Added `Maintenance & Consumables` panel to `index.html`.
   - Responsive progress bars and status indicators in `app.js` and `style.css`.
   - One-click Reset button with Bearer token authentication support.
5. **Testing**:
   - Unit tests in `scripts/test_flare_daemon_maintenance.py` assert schema persistence, event accumulation, limit/pause execution, and command syntax.
   - Static regression gate passed cleanly.
