# FLARE C Coding Style Guide

This document defines the coding style and readability standards for the FLARE firmware.

## 1. Tooling & Enforcement

We enforce style standards using `clang-format` and `clang-tidy` version 22.x.

### Canonical Environment
- **Host Platform**: macOS
- **Installation**: `brew install llvm@22` (binaries located in `/opt/homebrew/opt/llvm/bin`)
- **Integration**: Linting is run locally; it is decoupled from the ARM cross-compiler.

### Local Invocation Commands
To format the source code:
```bash
clang-format -i firmware/src/*.c firmware/include/*.h
```

To dry-run format checking (will exit non-zero if changes needed):
```bash
clang-format --dry-run -Werror firmware/src/*.c firmware/include/*.h
```

To run static analysis and naming/readability checks (requires `compile_commands.json` in the root or build directory):
```bash
SYSROOT=$(arm-none-eabi-gcc -print-sysroot)
GCCINC=$(arm-none-eabi-gcc -print-file-name=include)
clang-tidy -p build_local firmware/src/*.c \
  --extra-arg=--target=arm-none-eabi \
  --extra-arg=-isystem$GCCINC \
  --extra-arg=-isystem$SYSROOT/include
```

`clang-analyzer-core.FixedAddressDereference` is disabled in `.clang-tidy`: RP2040/Pico SDK
hardware access intentionally dereferences memory-mapped peripheral addresses.
`clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` is disabled because this
embedded/newlib target uses bounded `snprintf`; C11 Annex K `snprintf_s` is not portable or available.
`bugprone-unchecked-string-to-number-conversion` is disabled for the legacy serial command parser:
hardening `atoi`/`sscanf` parsing can change accepted payload syntax and belongs in a parser-focused
protocol change.
The header filter is limited to `firmware/src` and `firmware/include` so generated Pico/PIO headers
under `build_local/` are not lint-owned by this project.
All firmware global variables use the `g_lower_case` convention, including config-backed runtime
tunables (`g_feed_sps`, `g_buf_goal`). The `g_` prefix is enforced by `.clang-tidy` (no blanket
`GlobalVariableIgnoredRegexp`). The C identifier is independent of the wire surface: the `SET:`/`GET:`
protocol param strings and `config.ini` keys remain `UPPER_CASE` (e.g. `"BUF_GOAL"`) and are
unaffected by the identifier rename. `UPPER_CASE` file-scope constants (`static const`) are preserved
via `Global/StaticConstantCase: UPPER_CASE`.
`bugprone-easily-swappable-parameters` is disabled because existing firmware APIs intentionally pass
adjacent hardware pins, timestamps, and rates of similar C types; replacing those with wrapper structs
would be API churn outside this readability gate.

---

## 2. Naming Conventions

All identifiers must be intention-revealing. No single-letter or opaque identifiers for non-trivial scopes.

- **Functions**: `lower_case`
- **Variables / Parameters**: `lower_case`
- **Global Variables**: `g_lower_case` (must prefix with `g_`)
- **Typedefs / Structs**: `lower_case_t` (must suffix with `_t`)
- **Macros / Enum Constants**: `UPPER_CASE`

### Global Naming Categories
All firmware globals use `g_lower_case`. Read the name to know what a symbol is:

- `g_lower_case`, config-backed **tunable** (e.g. `g_feed_sps`, `g_buf_goal`): an
  `extern` variable seeded from `CONF_*`, settable via `SET:`/`GET:`, persisted in flash.
- `g_lower_case`, internal **state** (e.g. `g_buf`, `g_lane_l1`): runtime state, not a
  tunable and not on the config/protocol surface.
- Distinguish the two by **location**, not casing: tunables are declared in the
  `controller_shared.h` tunables block and mirrored in `settings_t`; state is not.
- `UPPER_CASE` **strings/keys** (e.g. `"BUF_GOAL"` in `SET:`/`GET:`, `config.ini` keys):
  the wire/config surface. Independent of the C identifier — never renamed by the `g_` rule.
- `CONF_*` (e.g. `CONF_FEED_SPS`): a **generated compile-time default** from `tune.h`
  (built from `config.ini`); seeds the matching `g_` tunable at boot.

### Domain Vocabulary Whitelist
The following domain-specific abbreviations are allowed and documented:
- `sps`: Steps Per Second (stepper rate unit)
- `mm`: Millimeters (physical length unit)
- `tmc`: Trinamic Motion Control (stepper driver API references)
- `buf`: Buffer (filament loop/sensor state buffer)
- `psf`: Position/Sync/Feedback (or Phase/State/Feedback) sensor state
- `adc`: Analog to Digital Converter
- `pio`: Programmable Input/Output block (RP2040 hardware)

---

## 3. Structural Norms

### File / Function Sizing
- **Translation Units (.c files)**: Should not exceed **800 lines**. TUs exceeding this must be split into cohesive modules along architectural domain boundaries.
- **Functions**: Should not exceed **100 lines** of active code. Oversized functions must be extracted into helper functions.

### Header / Include Order
Include headers in the following order (separated by a blank line):
1. System/Standard library headers (`#include <stdio.h>`)
2. Pico SDK/Hardware headers (`#include "pico/stdlib.h"`)
3. Project configuration headers (`#include "tune.h"`)
4. Module-specific headers and shared declarations (`#include "controller_shared.h"`)

---

## 4. Magic Numbers & Constants

- Opaque numeric literals in control logic are prohibited.
- Use named constants (`#define` or `static const`) with explanatory comments.
- For values that need to be runtime-tunable, use the `config.ini` -> `tune.h` -> `CONF_*` macro pipeline.
- **Single definition (DRY)**: a constant or small helper used by more than one
  translation unit lives once in a shared header (e.g. `firmware/include/firmware_constants.h`),
  not copied per `.c`. Do not redefine the same constant with divergent style across units.

---

## 5. Doc-Comment Format

Use triple-slash `///` comments for documenting functions, structs, and macros.

### Function Documentation Template
```c
/// @brief Brief description of function purpose.
/// @param param_name Description of parameter.
/// @return Description of return value.
```

### Rationale Preservation
Existing comments explaining hardware behavior, physics, timings, or tuning history must be preserved verbatim in meaning during any refactoring.

---

## 6. Host Tooling (Python)

Firmware C is linted with clang-format/clang-tidy; the Python host tooling under
`scripts/` is linted with **ruff** (config in `pyproject.toml` `[tool.ruff]`).

- Rules: `select = E, F, W, I, N, UP, B`, `line-length = 100`, `target-version = py39`.
- `E501` and the PEP 585/604 typing rewrites (`UP006/UP007/UP035`) are intentionally in
  `ignore`: line wrapping is left to a future `ruff format` reflow, and typing stays
  portable because the tooling may run under an older (3.9) Klipper Python.
- Framework/domain exceptions live in `[tool.ruff.lint.per-file-ignores]` (http.server
  handler names, pyserial-compat stub names, Kalman-filter matrix notation).
- Run `ruff check scripts/`; it is part of `scripts/validate_regression.py`.
- Keep host tooling pure stdlib + pyserial (ruff is a dev-time tool, not a runtime dep).
