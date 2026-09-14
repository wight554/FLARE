---
phase: 16-tmc-tension-current-boost
plan: 02
subsystem: host-tooling-telemetry-docs
tags: [telemetry, config, tlv, persistence, plant-sim, manual, requirements]

# Dependency graph
requires:
  - plan: 16-01-PLAN.md
    provides: Firmware boost actuation, hysteresis release, and host unit tests
provides:
  - Extended status telemetry token TB:%c with verified character budget headroom (759/760 chars)
  - End-to-end configuration and persistence parity across config.ini, flash TLV storage (SETTINGS_VERSION 65u), and SET/GET protocol
  - Strict threshold ordering validation (BOOST_ON < BOOST_OFF) and 1200 mA current clamping in SET: commands
  - Host daemon parsing and CLI monitoring of live tension boost state
  - Plant simulation scenario sem_psf_tension_boost and automated sim test suite in scripts/test_sync_sim.py
  - Complete user and operator documentation in MANUAL.md
  - Registered requirement definitions and traceability matrix in .planning/REQUIREMENTS.md and .planning/ROADMAP.md
affects: [milestone-audit, hardware-validation]

# Actuals
actuals:
  tasks: 3
  commits: 1

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Single-character status token (,TB:%c) preserved within strict STATUS_LINE_MAX character budget"
    - "Flash TLV migration to SETTINGS_VERSION 65u with tags 67, 68, 69 and backward compatibility for V64 sectors"
    - "Bidirectional threshold order enforcement rejecting invalid hysteresis configurations"
    - "End-to-end plant simulation integration asserting boost engagement and clean reset on Phase 13 fault hold"

key-files:
  created: []
  modified:
    - firmware/src/protocol_status.c
    - scripts/test_status_line_budget.py
    - config.ini
    - config.ini.example
    - scripts/gen_config.py
    - firmware/include/settings_store.h
    - firmware/src/settings_store.c
    - firmware/src/protocol.c
    - scripts/test_settings_parity.py
    - scripts/flare_daemon.py
    - scripts/flare_cmd.py
    - tests/host/sim_scenario.c
    - tests/host/sim_scenario.h
    - tests/host/sim_main.c
    - scripts/test_sync_sim.py
    - MANUAL.md
    - .planning/REQUIREMENTS.md
    - .planning/ROADMAP.md
    - .planning/STATE.md

key-decisions:
  - "Budgeted TB as single-character token ,TB:%c in extended status dump tail, preserving exactly 1 char headroom (759/760 chars)"
  - "Bumped SETTINGS_VERSION from 64u to 65u with tags 67 (IRUN), 68 (ON), 69 (OFF), preserving V64 sector compatibility"
  - "Enforced BOOST_ON < BOOST_OFF ordering invariant strictly via ER:INVALID_PARAM in protocol.c and fallback defaults in settings_apply_clamps"
  - "Added sem_psf_tension_boost plant simulation scenario verifying TMC:BOOST on tension and clean TMC:NORMAL reset on FAULT_HOLD"

requirements-completed: [R4, R5, R7]
---

# Phase 16 Plan 02: Summary

Implemented status telemetry, end-to-end configuration and flash TLV persistence parity, host daemon and CLI integration, plant simulation scenario with unit test assertions, documentation sync in MANUAL.md, and formal requirement registration.

## Accomplishments
1. **Telemetry & Budget Verification (R5)**: Wired `,TB:%c` into the extended status tail in `firmware/src/protocol_status.c`. Verified line budget via `scripts/test_status_line_budget.py` (759/760 chars, 1 char margin).
2. **Config & TLV Persistence Parity (R7)**: Added `sync_tension_boost_irun`, `sync_tension_boost_on`, and `sync_tension_boost_off` to `config.ini`, `config.ini.example`, and `scripts/gen_config.py`. Bumped `SETTINGS_VERSION` to 65u in `firmware/include/settings_store.h` with tags 67, 68, 69. Implemented defaults, serialization, deserialization, and bounds clamping in `firmware/src/settings_store.c`. Verified parity via `scripts/test_settings_parity.py`.
3. **Protocol GET/SET Validation (R7)**: Added GET and SET handlers in `firmware/src/protocol.c` for all three knobs, rejecting negative currents, enforcing the 1200 mA safety clamp, and rejecting inverted hysteresis bands (`BOOST_ON >= BOOST_OFF`) with `ER:INVALID_PARAM`.
4. **Host Daemon & CLI Integration (R5)**: Updated `scripts/flare_daemon.py` to parse the `TB` token into status data, and updated `scripts/flare_cmd.py` to support live status display and `--dump` emission.
5. **Plant Simulation Integration (R4)**: Added `sem_psf_tension_boost` in `tests/host/sim_scenario.c` and unit test `TmcTensionBoostSimTests` in `scripts/test_sync_sim.py`, validating boost activation (`TMC:BOOST,1`), subsequent Phase 13 distance fault trip, and clean baseline current restoration (`TMC:NORMAL,1`).
6. **Documentation & Requirement Registration (R4, R5, R7)**: Updated `MANUAL.md` parameter tables, diagnostic status fields, and event tables. Registered definitions and traceability entries for R1 through R7 in `.planning/REQUIREMENTS.md` and updated `.planning/ROADMAP.md` and `.planning/STATE.md`.

## Verification
- `ninja -C build_local` passes cleanly with dev-tuning enabled.
- `cmake --build build_sim` passes cleanly.
- `python3 scripts/test_status_line_budget.py` exits 0 (759/760 chars).
- `python3 scripts/test_settings_parity.py` exits 0.
- `python3 -m unittest scripts/test_sync_sim.py` passes 71/71 tests cleanly.
- `python3 scripts/validate_regression.py` passes the entire static regression gate (88/88 Klipper tests, lint, syntax, sim).
