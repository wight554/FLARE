# Coding Conventions and Quality Standards

**Analysis Date:** 2026-09-11

This document defines the coding style, architectural conventions, error-handling patterns, protocol synchronization contracts, and pre-commit review gates across the FLARE firmware (C) and host tooling (Python).

---

## 1. C Firmware Conventions

The firmware runs on an RP2040 microcontroller (FYSETC ERB V2.0 board) using the Raspberry Pi Pico SDK, targeting C11 standards with `-Wall -Wextra` clean code in simulation and production builds.

### 1.1 Tooling & Automated Enforcement

Style enforcement and static analysis are decoupled from the ARM cross-compiler:

- **Format Tool**: `clang-format` version 22.x
- **Linter / Static Analysis**: `clang-tidy` version 22.x
- **Configuration Files**: `.clang-format`, `.clang-tidy`
- **Canonical Environment**: macOS (`brew install llvm@22`, binaries in `/opt/homebrew/opt/llvm/bin`)

#### Command Invocations

```bash
# In-place formatting of all firmware sources and headers
clang-format -i firmware/src/*.c firmware/include/*.h

# Dry-run formatting check (exits non-zero on format violation)
clang-format --dry-run -Werror firmware/src/*.c firmware/include/*.h

# Run static analysis (requires compile_commands.json from CMake build)
SYSROOT=$(arm-none-eabi-gcc -print-sysroot)
GCCINC=$(arm-none-eabi-gcc -print-file-name=include)
clang-tidy -p build_local firmware/src/*.c \
  --extra-arg=--target=arm-none-eabi \
  --extra-arg=-isystem$GCCINC \
  --extra-arg=-isystem$SYSROOT/include \
  --extra-arg=-DFLARE_DEV_TUNING=1
```

#### Clang-Tidy Exceptions & Rationales

The project configuration in `.clang-tidy` intentionally suppresses specific checks for embedded hardware realities:
- `clang-analyzer-core.FixedAddressDereference`: Disabled because RP2040 and Pico SDK register operations dereference memory-mapped hardware peripheral addresses directly.
- `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling`: Disabled because standard embedded Newlib provides bounded `snprintf`; C11 Annex K `snprintf_s` is non-portable and unavailable.
- `bugprone-unchecked-string-to-number-conversion`: Disabled for legacy serial command parsers; hardening `atoi`/`sscanf` requires formal protocol revisions to avoid breaking accepted wire payloads.
- `bugprone-easily-swappable-parameters`: Disabled because firmware hardware APIs intentionally take adjacent hardware pin numbers, timestamps, and rate scalars of identical primitive types.

---

### 1.2 Identifier Naming Conventions

All identifiers must reveal intent. Single-letter or opaque identifiers are prohibited except in trivial loop indices (`i`, `j`).

| Identifier Category | Pattern | Example | Notes |
|---------------------|---------|---------|-------|
| Functions | `lower_case` | `motion_tick()`, `sync_apply_floor()` | Verb-first preferred |
| Local Variables | `lower_case` | `now_ms`, `current_rate`, `pin` | Descriptive scope |
| Function Parameters | `lower_case` | `lane_id`, `target_sps`, `dt_ms` | Include physical units |
| Global Variables | `g_lower_case` | `g_feed_sps`, `g_buf_pos`, `g_lane_l1` | Mandatory `g_` prefix |
| Typedefs & Structs | `lower_case_t` | `lane_t`, `settings_t`, `sync_state_t` | Mandatory `_t` suffix |
| Enums & Constants | `UPPER_CASE` | `TASK_FEED`, `SYNC_ACTIVE`, `CONF_FEED_SPS` | Prefix with domain |
| File-Scope Constants | `UPPER_CASE` | `static const uint32_t BL_TIMEOUT_MS = 30000;` | Enforced via `.clang-tidy` |
| Wire Protocol Keys | `UPPER_CASE` | `"BUF_GOAL"`, `"FEED_RATE"`, `"RELOAD_MODE"` | C identifiers do not alter wire keys |

#### Global Symbol Taxonomy: Tunable vs Internal State

All global variables must be prefixed with `g_`. Disambiguation is determined by **location**, not casing:

1. **Config-Backed Tunables** (e.g. `g_feed_sps`, `g_buf_goal`):
   - Declared `extern` in `firmware/include/controller_shared.h` within the tunables section.
   - Mirrored in `settings_t` inside `firmware/src/settings_store.c`.
   - Seeded at boot from `CONF_*` macros generated in `firmware/include/tune.h`.
   - Exposed over serial via `SET:` and `GET:` commands.
   - Saved and loaded to RP2040 flash memory.
2. **Internal Runtime State** (e.g. `g_buf`, `g_lane_l1`, `g_sync_state`):
   - Declared `extern` in `firmware/include/controller_shared.h` or unit headers.
   - Represents ephemeral machine state, motion status, or filter buffers.
   - Never exposed directly to the `config.ini` / `settings_t` persistence surface.

#### Whitelisted Domain Abbreviations

The following domain abbreviations are standardized across firmware, protocols, and tooling:
- `sps`: Steps Per Second (stepper motor speed)
- `mm`: Millimeters (displacement / length)
- `tmc`: Trinamic Motion Control (stepper driver API references)
- `buf`: Buffer (filament loop and sensor state)
- `psf`: Position/Sync/Feedback (or Phase/State/Feedback) sensor state
- `adc`: Analog to Digital Converter
- `pio`: Programmable Input/Output peripheral (RP2040 hardware)

---

### 1.3 Structural & Modular Norms

#### Sizing Limits
- **Translation Units (`.c` files)**: Hard limit of **800 lines**. TUs exceeding 800 lines must be decomposed into cohesive architectural modules (e.g., `protocol.c` split into `protocol_status.c` and `protocol_tmc.c`; `sync.c` split into `sync_buf.c`, `sync_relay.c`, and `sync_analog.c`).
- **Functions**: Hard limit of **100 lines** of active code. Longer functions must be refactored into focused `static` helper functions.

#### Header Include Hierarchy
Headers must be ordered with blank line separation between groups:
1. Standard C library headers (`#include <stdio.h>`, `#include <stdbool.h>`)
2. Pico SDK and hardware headers (`#include "pico/stdlib.h"`, `#include "hardware/gpio.h"`)
3. Generated project configuration (`#include "tune.h"`, `#include "config.h"`)
4. Shared internal declarations (`#include "controller_shared.h"`, `#include "sync_internal.h"`)

All headers must use `#pragma once` as the file guard.

#### Static Functions & Encapsulation
- Any function not invoked outside its translation unit **must** be marked `static`.
- Unit-private headers (`firmware/include/protocol_internal.h`, `firmware/include/sync_internal.h`) share functions across tightly coupled split units without exposing them to the global firmware scope.

---

### 1.4 Magic Numbers & Constant Management

- **No raw numeric literals in control logic**: Magic numbers (e.g. `125000000`, `30000`, `0.95f`) are strictly prohibited in algorithm bodies.
- **Single Definition Rule (DRY)**: Constants used across multiple files must be defined once in `firmware/include/firmware_constants.h` or `firmware/include/config.h`. Never duplicate definitions across `.c` files.
- **Runtime Tunable Pipeline**:
  ```
  config.ini / config.ini.example
        │ (scripts/gen_config.py)
        ▼
  firmware/include/tune.h (CONF_* definitions)
        │
        ▼
  firmware/src/main.c (initializes g_* globals)
        │
        ▼
  firmware/src/settings_store.c (settings_defaults / load / save)
  ```

---

### 1.5 Error Handling & Asynchronous Feedback

The firmware employs deterministic, non-blocking error handling:

#### Serial Command Responses
Commands processed over USB CDC serial yield synchronous replies:
- Success: `OK:...` (e.g., `OK:LN:1`, `OK:SAVED`)
- Error: `ER:<CODE>` (e.g., `ER:BAD_CMD`, `ER:PARAM_INVALID`, `ER:PERSIST_BUSY`, `ER:BUSY`)

#### Persistence Busy Guards
Flash erase/write operations block interrupts and cannot occur during motion. Flash commands (`SV:`, `LD:`, `RS:`) check motion state and return:
```c
if (motion_busy() || tc_busy()) {
    cmd_write("ER:PERSIST_BUSY\n");
    return;
}
```

#### Event Emission
Asynchronous state changes and fault warnings are emitted via `cmd_event()` or `cmd_event_critical()`:
- Format: `EV:<SUBSYSTEM>:<EVENT_DATA>` (e.g., `EV:SYNC:FAULT_HOLD`, `EV:RUNOUT:1`, `EV:RELOAD:LOADED`)
- Critical events (`cmd_event_critical`) bypass rate clamps to ensure emergency notifications reach host software.

#### Internal Fault Classification
Motor and toolchange tasks transition to `TASK_IDLE` upon errors and record failure taxonomy in `lane_t.fault`:
```c
typedef enum {
    FAULT_NONE = 0,
    FAULT_TIMEOUT,
    FAULT_SENSOR,
    FAULT_BUF,
    FAULT_CUT,
    FAULT_DRY_SPIN
} fault_t;
```

---

### 1.6 Protocol Parameter Parity & Persistence Invariants

Adding or modifying any runtime parameter requires synchronizing the complete pipeline across firmware, tooling, and documentation (AGENTS.md Non-Negotiable Rules 4, 7, and 8).

#### The 8-Point Parameter Checklist
1. `config.ini` and `config.ini.example`: Add key and descriptive comment.
2. `scripts/gen_config.py`: Ingest key, validate range, and map to `CONF_<NAME>`.
3. `firmware/include/tune.h`: Verify generated macro default.
4. `firmware/include/controller_shared.h`: Declare `extern <type> g_<name>;`.
5. `firmware/src/main.c`: Define and seed `g_<name> = CONF_<NAME>;`.
6. `firmware/src/settings_store.c`:
   - Add field to `settings_t` struct.
   - Increment `SETTINGS_VERSION` if struct layout changes.
   - Wire `s-><field> = g_<name>;` in `settings_save()`.
   - Wire `g_<name> = s-><field>;` in `settings_load()`.
   - Wire `g_<name> = CONF_<NAME>;` in `settings_defaults()`.
7. `firmware/src/protocol.c`: Add `SET:<PARAM>` parser and `GET:<PARAM>` response handler.
8. Host Tooling & Docs:
   - Add entry to `scripts/flare_cmd.py --dump`.
   - Update parameter tables in `MANUAL.md`, `BEHAVIOR.md`, `KLIPPER.md`, and `TUNING.md`.

#### Parameter Buffer Width Constraints
All parameter token parsing in `firmware/src/protocol.c` is bounded:
- `CMD_PARAM_MAX` = 32 bytes (includes terminating NUL)
- `CMD_PARAM_SCAN_WIDTH` = 31 characters
- Guarded by `scripts/test_protocol_param_width.py`. Every parameter string (e.g., `SYNC_COMPRESSION_DRAIN_BUDGET_MM`) must not exceed 31 characters.

---

### 1.7 Documentation & Rationale Preservation

- Use triple-slash `///` comments for all public functions, structs, and configuration macros in Doxygen format:
  ```c
  /// @brief Advance the kinematic sync controller by one time step.
  /// @param now_ms Monotonic system time in milliseconds.
  /// @param dt_ms Time elapsed since previous tick in milliseconds.
  /// @return Commanded feed speed in steps per second.
  ```
- **Rationale Preservation**: Existing comments describing hardware mechanics, physics models, ADC characteristics, or timing workarounds must be maintained verbatim in technical meaning during any refactoring.

---

## 2. Python Tooling Conventions

Host tooling under `scripts/` bridges FLARE to Klipper, manages configuration generation, handles firmware flashing, and executes the regression test harness.

### 2.1 Tooling & Linting

- **Linter**: `ruff`
- **Config**: `pyproject.toml` (`[tool.ruff]`)
- **Target Version**: Python 3.9 (`target-version = "py39"`)
- **Rule Selection**: `select = ["E", "F", "W", "I", "N", "UP", "B"]` (Pyflakes, pycodestyle, isort, pep8-naming, pyupgrade, flake8-bugbear)
- **Line Length**: 100 characters

#### Ruff Ignored Rules
- `E501`: Line length errors are ignored to avoid premature rewrapping before a unified formatting pass.
- `UP006`, `UP007`, `UP035`: Modern PEP 585 / PEP 604 type syntax (`list[str]`, `str | None`) is disabled. The scripts run on older embedded host controllers (e.g. Raspberry Pi running Klipper on Python 3.9) that require `typing.List` and `typing.Optional`.

#### Per-File Exceptions
- `scripts/flare_daemon.py`: `N801`, `N802` ignored to allow standard `http.server.BaseHTTPRequestHandler` method overrides (`do_GET`, `do_POST`).
- `scripts/flare_live_tuner.py`: `N802`, `N806`, `N818` ignored for mathematical matrix notation (`K`, `R`, `P`, `Q` in Kalman filters) and pyserial exception compatibility.

### 2.2 Dependencies & Structure

- **Runtime Dependencies**: Must remain restricted to the Python standard library and `pyserial`. Never introduce heavy external third-party dependencies (`numpy`, `scipy`, `requests`) into production host scripts.
- **Script Template Pattern**:
  ```python
  #!/usr/bin/env python3
  """Module-level docstring summarizing utility purpose and invocation."""

  from __future__ import annotations

  import argparse
  import sys
  from pathlib import Path

  REPO_ROOT = Path(__file__).resolve().parent.parent


  def parse_args() -> argparse.Namespace:
      parser = argparse.ArgumentParser(description=__doc__)
      return parser.parse_args()


  def main() -> None:
      args = parse_args()
      # Execution logic here


  if __name__ == "__main__":
      main()
  ```

---

## 3. Pre-Commit Review Gates

Before committing any non-doc changes, developers and coding agents must complete the review checklist from `REVIEW.md` and satisfy all 13 Non-Negotiable Rules from `AGENTS.md`.

### 3.1 Self-Review Checklist

- [ ] **Dev-Tuning Superset Build**: `ninja -C build_local` passes with `-DFLARE_DEV_TUNING=ON` configured. (Rule 1)
- [ ] **Python Compilation**: `python3 -m py_compile scripts/*.py` succeeds without syntax errors. (Rule 2)
- [ ] **No Mock Hardware in Firmware**: Real Pico SDK targets are used; no mock/stub paths compile into firmware binaries. (Rule 5)
- [ ] **Schema Versioning**: `SETTINGS_VERSION` bumped in `firmware/src/settings_store.c` if `settings_t` changed. (Rule 4)
- [ ] **Tunable End-to-End Wiring**: `config.ini` -> `gen_config.py` -> `tune.h` -> `CONF_*` consumer. (Rule 7)
- [ ] **Protocol Parity**: `SET:`, `GET:`, `flare_cmd.py --dump`, and documentation updated for all new tunables. (Rule 8)
- [ ] **Diff Hygiene**: `git diff --check` emits zero whitespace or formatting warnings.
- [ ] **No Committed AI Config**: Verify `.agents/`, `.claude/`, `.codex/`, `.gemini/`, or `skills-lock.json` are untracked. (Rule 11)
- [ ] **Hardware Gates Unchecked**: All hardware validation tasks remain unchecked with `HW:` tags intact. (Rule 12)

### 3.2 Commit Message & Attribution Format

Commits must use imperative, concise titles (<= 72 chars), explain rationale in the body, and include mandatory agent/model attribution:

```
<short description in lowercase imperative>

<body explaining what changed and why>

Generated-By: <Agent Name> (<Model>)
```

*Quality and conventions analysis: 2026-09-11*
