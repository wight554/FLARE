# Phase 2: Buffer Retract & Catch Hardening — Specification

**Goal**: Prevent Type-P buffer from slamming into the compression stop during fast print-end retracts (30–60 mm/s).
**Scope**: Type-P (`BUF_SENSOR_TYPE=1`) only. Type-D preserved identically behind guarded branches.

## Root Causes Resolved
1. Host CLI returned on daemon ack (~10ms) while prime was still running (~1.0s), causing extruder retract to race prime.
2. Type-P prime was throttled to `BUF_STAB_SPS` (10 mm/s), losing against typical 35–50 mm/s retracts.
3. Fixed-rate follow had no error escalation as compression built up.
4. No injectable guard macros existed to wrap third-party `END_PRINT` / `CANCEL_PRINT` retracts.

## Requirements
1. **Host Completion Wait**: `flare_cmd.py` blocks until `EV:BL:LOCKED` or `EV:BUF_STAB:DONE` before returning.
2. **Fast Type-P Prime**: Prime Type-P at `SYNC_MAX_SPS` (36.7 mm/s) using predictive rail stop to prevent overshoot.
3. **Servo Catch Escalation**: Proportional rate-servo catch escalates follow speed toward `GLOBAL_MAX_SPS` (83.3 mm/s) if compression builds.
4. **Retract Guard Macros**: `_FLARE_RETRACT_GUARD_BEGIN` and `_FLARE_RETRACT_GUARD_END` with configurable watchdog timeout.
