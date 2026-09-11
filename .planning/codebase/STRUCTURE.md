# FLARE Repository Structure & Directory Organization

**Analysis Date:** 2026-09-11

This document outlines the physical directory layout of the FLARE repository, defines component responsibilities, documents conventions, and provides prescriptive placement guidance for future modifications.

---

## 1. Directory Tree Overview

```
FLARE/
├── firmware/                   # Bare-metal RP2040 C firmware (Pico SDK + CMake)
│   ├── cmake/                  # Clang/ARM toolchain presets & SDK shims
│   ├── include/                # Firmware public and internal C headers
│   ├── src/                    # Firmware C implementation and PIO programs
│   └── third_party/            # Embedded third-party drivers (encoders, display)
├── scripts/                    # Host Python daemons, CLI tools, analysis & tests
│   └── webui/                  # Embedded Web UI assets served by flare_daemon
├── tests/                      # Testing infrastructure
│   ├── fixtures/               # Test fixtures (sensor CSVs, JSON states, G-code)
│   └── host/                   # C host simulation harness (flare_sim)
├── klipper/                    # Klipper integration modules and macros
├── .planning/                  # Project specifications, roadmap, and requirements
│   ├── codebase/               # Architecture and codebase reference documentation
│   ├── phases/                 # Historical and active milestone execution plans
│   └── specs/                  # Technical requirement and architecture specs
└── [Root Documentation]        # Comprehensive operator & developer references
```

---

## 2. Directory Breakdown & Subsystem Mapping

### 2.1 `firmware/` — Embedded Microcontroller Firmware
The core RP2040 firmware targets the FYSETC ERB v2.0 board. It is built using CMake and Ninja with the Raspberry Pi Pico SDK.

```
firmware/
├── CMakeLists.txt              # Primary CMake target definitions (flare_controller)
├── CMakePresets.json           # Ninja and Clang build presets
├── pico_sdk_import.cmake       # Standard Pico SDK bootstrap shim
├── cmake/
│   ├── pico_arm_cortex_m0plus_clang_atfe.cmake # Clang bare-metal cross toolchain
│   ├── picolibc_compat.h       # Picolibc math/string compatibility wrappers
│   └── include_patches/hardware/sync.h        # SDK interrupt sync overrides
├── include/
│   ├── config.h                # Hardware pin definitions, clock rates, board geometry
│   ├── firmware_constants.h    # Low-level protocol, buffer, and servo limits
│   ├── controller_shared.h     # 161 shared extern globals, lane_t, tc_ctx_t structs
│   ├── buf_signal.h            # Normalized buffer signal definitions (buf_signal_t)
│   ├── motion.h                # Stepper PWM, lane task prototypes, debouncer API
│   ├── cutter.h                # Blade cutter servo sequencing API
│   ├── toolchange.h            # Toolchange & RELOAD state machine API
│   ├── sync.h                  # Public sync controller and buffer API
│   ├── sync_internal.h         # Shared internal definitions for sync split units
│   ├── protocol.h              # Serial command parser & event dispatch API
│   ├── protocol_internal.h     # Sub-handlers for protocol commands
│   ├── settings_store.h        # Flash persistence and settings_t API
│   ├── tmc2209.h               # TMC2209 UART driver interface
│   ├── neopixel.h              # WS2812 status LED interface
│   └── tune.h                  # Generated default tunables (via gen_config.py)
└── src/
    ├── main.c                  # Hardware setup, boot sequencing, superloop runner
    ├── motion.c                # Lane tasks (AUTOLOAD, UNLOAD, LOAD, FEED), ramp, dry-spin
    ├── cutter.c                # Servo blade sequencing and feed-wait handling
    ├── toolchange.c            # Toolchange & autonomous RELOAD state machines
    ├── sync.c                  # Sync coordinator, auto-toggle, flow schedule, drift EWMA
    ├── sync_buf.c              # Virtual position integrator, switch re-anchoring, stab
    ├── sync_relay.c            # Type-D microswitch hysteretic relay law & AIMD floor
    ├── sync_analog.c           # Type-P analog Hall control law & distance slew filter
    ├── protocol.c              # USB CDC command receiver, line parser, event dispatcher
    ├── protocol_status.c       # Telemetry status string formatting (?: and ST:)
    ├── protocol_tmc.c          # TMC register inspection and runtime configuration
    ├── settings_store.c        # NOR flash sector erase/program, CRC32, wear counter
    ├── tmc2209.c               # Bit-banged / PIO UART driver for Trinamic stepper ICs
    ├── tmc_uart.pio            # PIO program for half-duplex single-wire UART
    └── neopixel.c              # WS2812 NeoPixel driver using PIO
```

### 2.2 `scripts/` — Host Tooling, Daemons & Analysis
All host-side software is written in Python (requiring `python3` with standard library and optional `pyserial`).

```
scripts/
├── flare_daemon.py             # Systemd background daemon (HTTP + SSE + Serial multiplexer)
├── flare_daemon.service        # Systemd unit definition for Linux hosts
├── install_daemon.py           # Installer script for systemd daemon and Klipper symlinks
├── flash_flare.py              # Interactive/automated UF2 firmware flasher over BOOTSEL
├── gen_config.py               # Transpiler: config.ini -> firmware/include/tune.h
├── flare_cmd.py                # Direct CLI utility for sending serial commands
├── flare_live_tuner.py         # Real-time console observer and guarded parameter tuner
├── flare_analyze.py            # Offline print log analyzer, schedule optimizer, patch maker
├── flare_sync_check.py         # Buffer telemetry validation & branch-test analyzer
├── flare_baseline_recommender.py # Advisory feed rate estimator from print metrics
├── klipper_motion_tracker.py   # Daemon tracking Klipper extruder moves via Moonraker
├── gcode_marker.py             # Slicer post-processor injecting telemetry markers
├── serial_utils.py             # Robust serial discovery, locking, and communication
├── path_utils.py               # Cross-platform path and directory resolution helpers
├── validate_regression.py      # Comprehensive 9-stage local verification gate
├── test_*.py                   # Python test suite (unit tests, mock MMU, regression)
└── webui/                      # Web dashboard assets (HTML, JS, CSS) served by daemon
```

### 2.3 `tests/` — Test Scenarios & Host Simulation Harness
FLARE maintains a comprehensive host simulation harness (`flare_sim`) that compiles the actual C source code natively on the host workstation against hardware mocks:

```
tests/
├── fixtures/                   # Real printer logs, traces, and test data
│   ├── high_sigma_run_*.csv    # Estimator uncertainty test logs
│   ├── consistency_run_*.csv   # Baseline rate validation runs
│   ├── orca_sample.gcode       # Test G-code sample
│   └── *.json                  # State test snapshots
└── host/                       # C host simulation harness (flare_sim)
    ├── CMakeLists.txt          # Native host build configuration (links real C sources)
    ├── sim_main.c              # Simulation superloop entry point (mimics main.c)
    ├── sim_plant.c / .h        # Physical filament, Bowden, and buffer physics models
    ├── sim_scenario.c / .h     # Pre-programmed test scenarios (runout, reload, toolchange)
    ├── sim_trace.c / .h        # CSV and event trace emitter
    ├── sim_fakes.c / .h        # Pico SDK and hardware register stubs (GPIO, PWM, Flash)
    └── shims/                  # Header shims replacing hardware headers on host
```

### 2.4 `klipper/` — Klipper Firmware Integration
Provides seamless integration with Klipper-based 3D printers:

```
klipper/
├── flare_mmu.cfg               # Klipper G-code macros (T0, T1, MMU_LOAD, MMU_UNLOAD, etc.)
├── mmu.py                      # Klipper extras module: Happy-Hare API compatibility facade
└── mmu_sensors.py              # Sensor monitoring and status reporting into Moonraker
```

### 2.5 `.planning/` — GSD Planning, Architecture & Specifications

```
.planning/
├── PROJECT.md                  # Project vision, core constraints, hardware specs
├── STATE.md                    # Current milestone status, test coverage, and active focus
├── ROADMAP.md                  # Release phases and milestone schedule
├── REQUIREMENTS.md             # Formal system functional requirements
├── config.json                 # GSD toolchain configuration
├── codebase/                   # Durable architectural documentation
│   ├── ARCHITECTURE.md         # Detailed system architecture, data flow, state machines
│   └── STRUCTURE.md            # Physical layout, file locations, conventions (this file)
├── phases/                     # Structured phase work directories
└── specs/                      # Specific feature specifications
```

---

## 3. Key File Location Matrix

| Responsibility / Purpose | Primary Implementation File(s) | Associated Header(s) |
|---|---|---|
| **Superloop & Boot** | `firmware/src/main.c` | `firmware/include/config.h`, `controller_shared.h` |
| **Motion, Tasks, Stepper PWM** | `firmware/src/motion.c` | `firmware/include/motion.h` |
| **Central Sync Coordinator** | `firmware/src/sync.c` | `firmware/include/sync.h`, `sync_internal.h` |
| **Buffer Modeling & Virtual Pos** | `firmware/src/sync_buf.c` | `firmware/include/sync_internal.h`, `buf_signal.h` |
| **Type-D Relay Law** | `firmware/src/sync_relay.c` | `firmware/include/sync_internal.h` |
| **Type-P Analog Law** | `firmware/src/sync_analog.c` | `firmware/include/sync_internal.h` |
| **Toolchange & RELOAD FSM** | `firmware/src/toolchange.c` | `firmware/include/toolchange.h` |
| **Blade Cutter Servo** | `firmware/src/cutter.c` | `firmware/include/cutter.h` |
| **Serial Protocol & Parser** | `firmware/src/protocol.c` | `firmware/include/protocol.h`, `protocol_internal.h` |
| **Telemetry Status Formatter** | `firmware/src/protocol_status.c` | `firmware/include/protocol_internal.h` |
| **TMC2209 Register Protocol** | `firmware/src/protocol_tmc.c` | `firmware/include/protocol_internal.h` |
| **Flash Persistence** | `firmware/src/settings_store.c` | `firmware/include/settings_store.h` |
| **TMC2209 Driver Interface** | `firmware/src/tmc2209.c` | `firmware/include/tmc2209.h` |
| **Host Systemd Daemon** | `scripts/flare_daemon.py` | — |
| **Klipper MMU Compatibility** | `klipper/mmu.py` | — |
| **Regression Validation Suite** | `scripts/validate_regression.py` | — |

---

## 4. Naming & Coding Conventions

### 4.1 C Firmware Standards (`STYLE.md`)
- **Global Variables**: Prefixed with `g_` (e.g. `g_active_lane`, `g_sync_state`, `g_now_ms`). All shared globals reside in `firmware/src/main.c` and are declared as `extern` in `firmware/include/controller_shared.h` or `firmware/include/sync_internal.h`.
- **Functions**: Lower snake_case prefixed by module name:
  - `motion_*`, `lane_*`: Motion engine (`firmware/src/motion.c`)
  - `sync_*`, `buf_*`, `relay_*`: Buffer and sync subsystem (`firmware/src/sync*.c`)
  - `tc_*`, `reload_*`: Toolchange and reload subsystem (`firmware/src/toolchange.c`)
  - `cutter_*`: Cutter blade subsystem (`firmware/src/cutter.c`)
  - `cmd_*`: Protocol parser and serial dispatch (`firmware/src/protocol.c`)
  - `settings_*`: Persistence and flash store (`firmware/src/settings_store.c`)
- **Types and Enums**:
  - Typedef structs end with `_t` (e.g., `lane_t`, `motor_t`, `buf_signal_t`, `settings_t`).
  - Enum constants are uppercase snake_case: `TASK_*`, `FAULT_*`, `TC_*`, `BUF_*`, `SYNC_*`.
- **Configuration & Compile Macros**:
  - Default tunable values generated from `config.ini`: `CONF_<NAME>`.
  - Board pin constants: `PIN_<NAME>`.
  - Compile flags: `FLARE_DEV_TUNING`, `PICO_NO_HARDWARE`.

### 4.2 Python Tooling Standards
- Standard PEP 8 naming: snake_case for functions and variables, PascalCase for classes.
- Zero mandatory external dependencies for core CLI tools (`flare_cmd.py`, `flare_baseline_recommender.py`, `gen_config.py`). `pyserial` is imported defensively with fallbacks.
- Cross-platform path handling: Use `pathlib.Path` or helpers in `scripts/path_utils.py` rather than hardcoded `/` or `\` separators.

---

## 5. Architectural Placement Guide for New Code

When introducing changes or extending functionality, follow these concrete placement rules:

### Adding a New Serial Command
1. Add command string and dispatch branch in `firmware/src/protocol.c:cmd_execute()`.
2. Implement specific command handler logic in `protocol.c` (or `protocol_status.c` / `protocol_tmc.c` if status or driver related).
3. If the command interacts with motion or state machines, invoke existing public APIs in `motion.h`, `toolchange.h`, or `sync.h`. Do not manipulate hardware PWM directly from `protocol.c`.
4. Document the command in `MANUAL.md`.
5. Add test coverage in `scripts/test_wire_format.py` or `scripts/test_protocol_param_width.py`.

### Adding a New Tunable Runtime Setting
Adding a persistent tunable touches the full vertical stack and requires a documented 10-step path (`CONTEXT.md:103-118`):
1. **Define in INI**: Add key and default value in `config.ini.example` and `config.ini`.
2. **Generator Support**: Add default mapping and `CONF_*` emission in `scripts/gen_config.py`.
3. **Regenerate Header**: Run `python3 scripts/gen_config.py` to refresh `firmware/include/tune.h`.
4. **Declare Runtime Global**: Define runtime global in `firmware/src/main.c` (or owning module) and add `extern` declaration to `firmware/include/controller_shared.h`.
5. **Flash Schema (`settings_t`)**: Add field to `settings_t` struct in `firmware/src/settings_store.c`. Ensure compile assert `sizeof(settings_t) <= 512` holds.
6. **Flash Wiring**: Implement default assignment in `settings_reset()`, load in `settings_load()`, save in `settings_save()`.
7. **TMC Path (if motor parameter)**: If it alters motor registers, update `apply_tmc_settings()` in `settings_store.c`.
8. **Protocol Interface**: Add `SET:<NAME>` and `GET:<NAME>` handling in `firmware/src/protocol.c`.
9. **Bump Version**: Increment `SETTINGS_VERSION` in `firmware/src/settings_store.c` (required whenever `settings_t` changes).
10. **Documentation & Validation**: Update `MANUAL.md`, run `python3 scripts/test_settings_parity.py`, and run `python3 scripts/validate_regression.py`.

### Modifying Buffer Synchronization or Control Laws
- If modifying **physical position tracking, switch debounce, or virtual position integration**, edit `firmware/src/sync_buf.c`.
- If modifying **Type-D switch hysteretic relay laws, neutral trim, or AIMD floor probing**, edit `firmware/src/sync_relay.c`.
- If modifying **Type-P Hall sensor reading, continuous proportional laws, or distance-based filtering**, edit `firmware/src/sync_analog.c`.
- If modifying **overall sync enable/disable, auto-start/stop thresholds, or flow schedule interpolation**, edit `firmware/src/sync.c`.
- Verify changes by running the host simulation test suite: `python3 scripts/test_sync_sim.py`.

### Adding a New Simulation Scenario
1. Add scenario definition in `tests/host/sim_scenario.c` and `tests/host/sim_scenario.h`.
2. Configure plant conditions, initial sensor states, and switch transition scripts.
3. Add Python runner wrapper in `scripts/test_sync_sim.py`.
4. Execute via `python3 scripts/validate_regression.py` to ensure local gate passes.

---

*Architectural analysis: 2026-09-11*
<!-- refreshed: 2026-09-11 -->
