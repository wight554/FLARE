---
phase: 16-tmc-tension-current-boost
plan: 01
subsystem: firmware-motion-sync
tags: [tmc2209, current-boost, tension-boost, sync-feedback, type-p]

# Dependency graph
requires:
  - phase: 13-type-p-sync-relief-fault-trip
    provides: Type-P buffer control law and persistent tension trip hooks
provides:
  - Dynamic TMC tension current boost actuation on deep Type-P tension
  - Strict edge-triggered UART writes with hysteresis release
  - State exit unwinds restoring baseline current on SYNC_OFF, SYNC_FAULT_HOLD, stop_all, and sync_disable
  - Atomic shadow register synchronization maintaining boost across brownout auto-recovery
  - Dedicated host unit test suite `test_tmc_boost`
affects: [16-02-telemetry-parity-docs]

# Actuals
actuals:
  tasks: 3
  commits: 1

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Dynamic run current actuation helper with hardware safety ceiling (1200 mA clamp)"
    - "Hysteresis edge detection with zero redundant UART writes in steady-state tension"
    - "Centralized state-machine exit unwinds in sync_set_state() and stop_all()"
    - "Atomic shadow register synchronization (`g_shadow_ihold_irun`, `g_shadow_vsense`) to prevent heartbeat brownout recovery desync"

key-files:
  created:
    - tests/host/test_tmc_boost.c
  modified:
    - firmware/src/motion.c
    - firmware/include/motion.h
    - firmware/src/settings_store.c
    - firmware/src/sync.c
    - firmware/include/sync.h
    - firmware/include/controller_shared.h
    - firmware/src/main.c
    - tests/host/CMakeLists.txt
    - tests/host/sim_fakes.c
    - tests/host/sim_fakes.h

key-decisions:
  - "Clamp active run current to 1200 mA in tmc_apply_active_run_current() to strictly protect TMC2209 driver and motor windings"
  - "Emit edge events EV:TMC:BOOST and EV:TMC:NORMAL exclusively on state transitions, preventing serial link saturation"
  - "Unconditionally unwind boost to baseline current upon any exit from SYNC_ACTIVE (including SYNC_FAULT_HOLD, stop_all, sync_disable)"
  - "Preserve active boosted current across heartbeat brownout recovery by querying sync_get_active_run_current_ma() in sync_tmc_settings()"

requirements-completed: [R1, R2, R3, R4, R6]
---

# Phase 16 Plan 01: Summary

Implemented core firmware tension boost actuation, edge detection, hysteresis release, state unwind, and shadow register synchronization with a dedicated host unit test suite.

## Accomplishments
1. **Actuation & Safety Ceiling (R1, R3)**: Added `tmc_apply_active_run_current(lane_num, current_ma)` in `firmware/src/motion.c` with a hard hardware clamp to 1200 mA max (`clamp_i(current_ma, 0, 1200)`).
2. **Hysteresis & Edge Detection (R1, R2)**: Implemented `sync_check_tension_boost()` in `firmware/src/sync.c`, engaging boost at `g_buf_pos <= g_sync_tension_boost_on` (-0.50f) and releasing back to baseline at `g_buf_pos >= g_sync_tension_boost_off` (-0.30f). Verified zero redundant UART writes during sustained tension.
3. **State Exit Unwinds (R3, R4)**: Wired centralized unwind to baseline run current in `sync_set_state()` for any transition out of `SYNC_ACTIVE`, with defense-in-depth unwind calls in `sync_disable()` and `stop_all()`.
4. **Heartbeat Brownout Recovery Parity (R6)**: Updated `sync_tmc_settings()` in `firmware/src/settings_store.c` to query `sync_get_active_run_current_ma(lane)` and synchronize `g_shadow_ihold_irun` and `g_shadow_vsense` so brownout recovery restores active boosted current while in boost, and baseline current when released.
5. **Host Unit Test Suite**: Added `test_tmc_boost.c` with 4 test suites covering activation, hysteresis, exit resets, brownout recovery, and safety prohibitions (Type-D and compression guards). All tests pass cleanly.

## Verification
- `ninja -C build_local` passes cleanly with dev-tuning enabled.
- `cmake --build build_sim --target test_tmc_boost && ./build_sim/test_tmc_boost` passes 100%.
- All 6 host test suites pass cleanly (`test_cutter`, `test_forensics`, `test_persistence`, `test_tmc_recovery`, `test_toolchange`, `test_tmc_boost`).
- `python3 scripts/test_settings_parity.py` passes.
