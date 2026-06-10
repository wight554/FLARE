> [!IMPORTANT]
> **DEVELOPER AND AI AGENT GUIDE ONLY**
> This document contains internal developer guidelines, codebase navigation tips, and firmware structural contexts designed for developers and AI agents. If you are a printer operator, please refer to the operator guides in the [README](README.md) instead.

# FLARE — Project Context

Quick ref for firmware work. The validated architecture contract lives in
`openspec/specs/project-architecture/spec.md`; this file remains a compact
navigation guide for agents. Current durable behavior lives under
`openspec/specs/`; migrated historical phase/task prose is intentionally not
kept in-tree and can be recovered from git history when needed.

---

## Firmware Architecture

FLARE = cooperative firmware for RP2040, no RTOS. Main loop calls non-blocking module ticks each iteration.

### Module ownership

- `firmware/src/main.c`: Hardware bring-up, shared globals, autopreload detection, LED state, main-loop ordering.
- `firmware/src/motion.c`: Debounced IN/OUT sensors, stepper PWM, per-lane task execution (`AUTOLOAD`, `UNLOAD`, `LOAD_FULL`, `MOVE`, `FEED`).
- `firmware/src/sync.c`: Buffer sensing, estimator-driven sync controller, auto-start/stop, boot-time stabilization, drift/integral correction.
- `firmware/src/toolchange.c`: Cutter state machine, toolchange state machine, RELOAD approach/follow logic.
- `firmware/src/protocol.c`: USB CDC parser, `OK`/`ER` replies, `EV` events, `GET`/`SET` handling, driver access.
- `firmware/src/settings_store.c`: Flash-backed settings schema, defaults/save/load/reset, TMC re-apply.

### Host Tooling

- `scripts/flare_cmd.py`: Serial helper for commands and live config dumps.
- `scripts/flare_live_tuner.py`: observe-only by default; guarded SET writes via --allow-* flags.
- `scripts/flare_analyze.py`: Offline calibration analyzer; emits deterministic schedules and review patches.
- `scripts/flare_baseline_recommender.py`: Advisory print guidance; pure stdlib + pyserial.
- `scripts/gcode_marker.py`: G-code metadata injector and sidecar JSON generator.
- `scripts/gen_config.py`: Generates `tune.h` from `config.ini`.
- `scripts/validate_regression.py`: One-command static regression gate.

---

## Where to Look

- **Capture:** `gcode_marker.py` (preparation) + `flare_live_tuner.py` (active print).
- **Analyze:** `flare_analyze.py` (offline schedule/patch emission).
- **Sync Control:** `firmware/src/sync.c` (estimator and control law) + `firmware/src/motion.c` (stepper drive).
- **Buffer:** `firmware/src/sync.c` (zone logic and position integration).
- **Toolchange:** `firmware/src/toolchange.c` (sequencing) + `firmware/src/cutter.c` (blade control).
- **Config Generation:** `scripts/gen_config.py` + `firmware/include/tune.h`.
- **Tests:** `scripts/test_*.py` (host logic) + `TEST_CASES.md` (hardware validation).

---

## Technical Contracts

- **Validated Design:** `openspec/specs/project-architecture/spec.md`
- **Tuning Workflow:** [TUNING.md](./TUNING.md)
- **Detailed Behavior:** [BEHAVIOR.md](./BEHAVIOR.md)
- **Command Reference:** [MANUAL.md](./MANUAL.md)
- **Coding Style Standards:** [STYLE.md](file:///Users/Volodymyr_Zhdanov/playground/FLARE/STYLE.md)
- **Klipper Integration:** [KLIPPER.md](./KLIPPER.md)

---

## Key Runtime Structures

### `lane_t`

Per-lane motion and sensor state.

Important fields:

- `in_sw`, `out_sw`: debounced IN / OUT inputs
- `m`: step/dir/enable motor handle
- `task`: current lane task
- `task_limit_mm`, `task_dist_mm`: distance-based task limits and progress
- `target_sps`, `current_sps`: ramp target and current command speed
- `fault`: `FAULT_NONE`, `FAULT_TIMEOUT`, `FAULT_SENSOR`, `FAULT_BUF`, `FAULT_CUT`, `FAULT_DRY_SPIN`
- `unload_to_in`, `unload_buf_recover_done`: unload-path bookkeeping

### `tc_ctx_t`

Toolchange / RELOAD state.

Important fields:

- `state`: `TC_IDLE` through `TC_ERROR`
- `target_lane`, `from_lane`
- `phase_start_ms`: current phase timing origin
- `reload_tick_ms`, `reload_current_sps`, `last_compression_ms`: RELOAD follow control state

### `buf_tracker_t`

Buffer-zone transition history used by estimator.

Important fields:

- `state`: `BUF_NEUTRAL`, `BUF_TENSION`, `BUF_COMPRESSION`, `BUF_FAULT`
- `entered_ms`, `dwell_ms`
- `arm_vel_mm_s`
- `mmu_sps_at_entry`, `mmu_sps_dwell_sum`, `mmu_sps_dwell_samples`

---

## Runtime Parameter Pattern

Persistent tunables follow this path:

1. Add/update key in `config.ini.example` and `config.ini`.
2. Add default and generated `CONF_*` macro in `scripts/gen_config.py`.
3. Regenerate `firmware/include/tune.h` with `python3 scripts/gen_config.py`.
4. Add/update owning runtime variable in appropriate module (`main.c`, `motion.c`, `sync.c`, `toolchange.c`, or another owner; shared externs in `controller_shared.h`).
5. Add field to `settings_t` in `firmware/src/settings_store.c` if value must persist.
6. Wire defaults/save/load/reset in `settings_store.c`.
7. If value affects hardware registers, update TMC apply path in `settings_store.c`.
8. Add `SET:` / `GET:` handling in `firmware/src/protocol.c`.
9. Update relevant docs (`MANUAL.md`, `BEHAVIOR.md`, `README.md`, etc.).
10. Bump `SETTINGS_VERSION` in `firmware/src/settings_store.c` when `settings_t` layout changes.

Current `SETTINGS_VERSION`: `60` in `firmware/src/settings_store.c`.

---

## Critical Gotchas

### 1. Sync only runs in `TC_IDLE`

`sync_tick()` guarded — normal sync never runs during toolchange or RELOAD. Don't expect normal sync tick to rescue RELOAD behavior changes.

### 2. RELOAD is buffer-driven now

- `TC_RELOAD_APPROACH` waits for `BUF_COMPRESSION` contact.
- `TC_RELOAD_FOLLOW` reuses estimator with `RELOAD_LEAN_FACTOR`.
- No driver-load or DIAG-based stall handling in current firmware flow.

### 3. Load / unload safety is distance-based

- `AUTOLOAD_MAX`, `LOAD_MAX`, `UNLOAD_MAX` = travel limits.
- Toolchange phases like `TC_LOAD_WAIT_TH` or `TC_UNLOAD_WAIT_OUT` observe underlying lane task, react when it stops.
- Old names `TC_LOAD_MS` / `TC_UNLOAD_MS` = legacy protocol aliases, not real time-based limits.

### 4. Persistence is activity-gated

`SV:`, `LD:`, `RS:` rejected with `ER:PERSIST_BUSY` while motion, toolchange, cutter activity, or boot stabilization active.

### 5. Speed conversion helpers are shared

`mm_per_min_to_sps*()` and `sps_to_mm_per_min*()` implemented in `main.c`, declared in `controller_shared.h`. Protocol and settings code rely on them.

### 6. Board pins live in `config.h`

`firmware/include/config.h` = source of truth for pin assignment and board constants. DIAG pins defined for board completeness; current firmware doesn't attach DIAG IRQ handling.

---

## Common Operations

### Add a new `SET:` / `GET:` parameter

- Add config key and generated macro.
- Update runtime variable and persistence path if needed.
- Add `SET` / `GET` branches in `firmware/src/protocol.c`.
- Regenerate `tune.h` and update docs.

### Add a new serial command

- Implement in `cmd_execute()` in `firmware/src/protocol.c`.
- Use module APIs from `motion.h`, `sync.h`, `toolchange.h`, `settings_store.h`.
- Document in `MANUAL.md`.

### Run the static regression gate

- `python3 scripts/validate_regression.py`
- Run before hardware testing — catches config, build, script, and diff-integrity regressions fast.

### Emit a reply or event

- `cmd_reply("OK", data)` / `cmd_reply("ER", reason)`
- `cmd_event("TYPE", data)`

`EV:` output is best-effort and rate-limited.

---

## Navigation Guide

| Task | Read |
|------|------|
| Add or modify a runtime parameter | This file + `protocol.c` + `settings_store.c` + `config.ini.example` |
| Change motion, load, unload, or runout behavior | `motion.c` + `BEHAVIOR.md` |
| Change sync / buffer behavior | `sync.c` + `BEHAVIOR.md` |
| Change RELOAD or toolchange flow | `toolchange.c` + `BEHAVIOR.md` |
| Change serial protocol behavior | `protocol.c` + `MANUAL.md` |
| Change board pins or hardware assumptions | `config.h` + `HARDWARE.md` |
| Run or extend bring-up / regression validation | `TEST_CASES.md` + `BUILD_FLASH.md` |
| Change agent workflow / repo rules | `AGENTS.md` + `WORKFLOW.md` |
| Need validated architecture contract | `openspec/specs/project-architecture/spec.md` |
