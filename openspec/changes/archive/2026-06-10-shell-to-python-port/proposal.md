## Why

3 bash scripts remain in `scripts/` while 26 other files are Python. Bash creates implicit platform assumptions (GNU coreutils, `lsblk`, `diskutil`, bash arrays/`set -euo pipefail`) and forces contributors to know two languages. Porting to Python unifies tooling language, enables consistent stdlib cross-platform support (macOS + Linux), and removes inline Python heredoc hacks in `flash_flare.sh`.

## What Changes

- **Port `validate_regression.sh` → `validate_regression.py`**: `subprocess.run()` orchestration of gen_config, cmake/ninja build, py_compile, ruff, unittest, git diff. Drop bash entirely.
- **Port `flash_flare.sh` → `flash_flare.py`**: Pico SDK discovery, cmake build, BOOTSEL trigger (already Python heredoc), picotool flash, UF2 mass-storage fallback (mount/copy/unmount), firmware version verify (already Python heredoc). Replace heredoc-embedded Python with direct `import serial_utils` calls. Platform branching via `platform.system()`, `subprocess`, `shutil`, `pathlib`.
- **Port `install_daemon.sh` → `install_daemon.py`**: systemd service templating, pyserial dependency check, Klipper extras copy, `flare_mmu.cfg` install. Requires root — `os.getuid()` check replaces `$EUID`. ANSI color output preserved.
- **Remove shell wrappers**: remove `.sh` files entirely (`validate_regression.sh`, `flash_flare.sh`, `install_daemon.sh`) as backwards compatibility is not required at this phase of the project.
- **Doc updates**: all references in MANUAL.md, BUILD_FLASH.md, README.md, KLIPPER.md, TEST_CASES.md, TUNING.md, WORKFLOW.md, STYLE.md, AGENTS.md, CONTEXT.md updated to prefer `python3 scripts/foo.py`.

## Capabilities

### New Capabilities

- `cross-platform-script-tooling`: cross-platform Python host script contract — all `scripts/*.sh` ported to Python and removed, MUST work on both Linux (Raspberry Pi, Debian/Ubuntu) and macOS, using only stdlib + pyserial

### Modified Capabilities

- `static-regression-validation`: scenario references change from `scripts/validate_regression.sh` to `scripts/validate_regression.py`; functional behavior unchanged
- `python-host-tooling-style`: ruff/lint scope now covers 3 new `.py` files; `.sh` wrappers removed from tree

## Impact

- **Scripts affected**: `scripts/validate_regression.sh`, `scripts/flash_flare.sh`, `scripts/install_daemon.sh` (deleted)
- **Docs affected**: BUILD_FLASH.md, README.md, MANUAL.md, KLIPPER.md, TEST_CASES.md, TUNING.md, WORKFLOW.md, STYLE.md, AGENTS.md, CONTEXT.md
- **Specs affected**: `static-regression-validation`, `python-host-tooling-style` (scenario text updates)
- **No firmware changes**: pure host-side tooling port
- **Dependencies**: unchanged — Python 3 + pyserial already required
- **Backward compat**: none — shell scripts removed entirely
