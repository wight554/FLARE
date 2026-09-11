# Phase 6: Advanced Toolchange & RELOAD Automation — Specification

**Goal**: Production-grade MMU multi-colour printing, spool runout reliability, mechanical cutter protection, and external spool bypass.
**Scope**: Firmware (`toolchange.c`, `cutter.c`, `protocol.c`), Klipper integration (`klipper/mmu.py`, `scripts/flare_cmd.py`), and host simulation.

---

## 1. Cutter Stall / Watchdog Recovery & Servo Interlocks

### Requirements
1. **Mid-Cut Guard**: `CP:<us>` (servo pulse test) command SHALL be rejected with `ER:BUSY` while any cut sequence is active (`cutter_busy()`).
2. **Watchdog Bounds**: Toolchange cut phase watchdog SHALL encompass configured feed distance, repeat count, servo settle phases, plus `CUTTER_WATCHDOG_SLACK_MS` (1000 ms).
3. **Safe Abort on Failure**:
   - On cut feed timeout, blade stall, or motion error:
     - Immediately halt lane motor feed (`stop_all()`).
     - Command servo to park position (`g_cut_servo_open_us`).
     - De-energize servo PWM (`servo_idle()`) to prevent motor burnout or gear stripping.
     - Mark `g_cut.failed = true` and transition `tc_state` to `TC_ERROR`.
     - Emit critical events `EV:CUT:ERROR:<reason>` and `EV:TC:ERROR:<reason>`.

---

## 2. External Spool Bypass Semantics

### Requirements
1. **Firmware State & Command**:
   - Expose `SET:BYPASS <0|1>` and `GET:BYPASS` runtime parameters and serial commands.
   - Session-toggleable without unnecessary flash wear.
2. **Firmware Interlocks when `BYPASS=1`**:
   - Lock out MMU lane motors: all lane motion commands (`FL`, `LO`, `MV`, `TC`, `RL`) return `ER:BYPASS_ACTIVE`.
   - Disable sync-feedback buffer controller and tension/compression watchdogs.
   - Mask lane `IN` and `OUT` sensor events from triggering auto-preload or runout RELOAD.
   - Toolhead sensor (`TS`) remains fully active; state updates and events (`EV:TS:...`) pass to host.
3. **Host / Klipper Integration**:
   - Gate / tool sentinel mapped to `-2` in `klipper/mmu.py`.
   - Autoload trigger: Manual insertion through bypass tube trips `TS:1`, triggering extruder-only hotend load macro `MMU_LOAD`.
   - Suppress MMU serial moves (`FL:`, `LO:`, `TC:`, `UM:`) during bypass load/unload/eject operations.

---

## 3. Pause & Escalation Boundary (Klipper vs FLARE MCU)

### Requirements
1. **Event-Driven Escalation**:
   - When FLARE encounters an unrecoverable fault (`TC:ERROR`, `CUT:ERROR`, feed watchdog, runout exhaustion), MCU halts all motion immediately via `stop_all()`.
   - Emits `EV:TC:ERROR:<reason>` or `EV:ERR:<reason>`.
   - Host daemon mirrors event to Klipper; Klipper executes standard print `PAUSE`.
2. **Klipper Command Actuation (`cmd_MMU_PAUSE`)**:
   - Replace empty stub in `klipper/mmu.py`: `MMU_PAUSE` dispatches `PA` / `STOP` to FLARE MCU.
   - Firmware enters safe idle/hold state without resetting position counters or active lane designation.
3. **Recovery Flow**:
   - Operator clears obstruction or changes spool.
   - Klipper resume/recover macro handles re-priming or re-issuing `TC:<target>` / `FLARE_LOAD`.

---

## 4. Toolhead Sensor Load Retries & Parking

### Requirements
1. **Configurable Retries**:
   - Add runtime parameters `tc_ts_retries` (default: 2 retries, 3 total attempts) and `tc_ts_retry_retract_mm` (default: 50.0 mm).
   - If `TS` is not triggered within configured bowden forward travel:
     - Lane executes reverse retraction of `tc_ts_retry_retract_mm`.
     - Re-advances toward toolhead at search speed.
     - If all retries exhausted without `TS:1`: halt motor, emit `EV:TC:ERROR:TS_NOT_HIT`, transition to `TC_ERROR`.
2. **Extruder Gear Parking**:
   - Add parameter `tc_ts_park_mm` (default: 25.0 mm).
   - Once `TS` transitions low-to-high (`TS:1`), lane motor continues feeding exactly `tc_ts_park_mm` to positively engage extruder drive gears.
   - On completion, emit `EV:TC:TS_PARKED` and transition to `TC_LOAD_DONE` / handoff to Klipper extruder.
