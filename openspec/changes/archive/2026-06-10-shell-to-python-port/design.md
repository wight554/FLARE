## Context

`scripts/` has 26 Python files + 3 bash scripts. Bash scripts orchestrate cmake/ninja builds, device flashing, and systemd service install. `flash_flare.sh` already embeds Python heredocs for serial I/O — sign bash is fighting Python. Target platforms: Raspberry Pi (Debian/Ubuntu), other Linux distros, macOS. Windows out of scope.

Research: read all 3 `.sh` files, grep'd all doc/spec references, checked platform-specific constructs (`sudo`, `mount`, `diskutil`, `lsblk`, `systemctl`, `apt-get`).

## Goals / Non-Goals

**Goals:**
- Port `validate_regression.sh`, `flash_flare.sh`, `install_daemon.sh` to Python
- Each `.py` MUST work on both Linux (RPi, Debian/Ubuntu/Fedora) and macOS
- Use only stdlib + pyserial (existing project constraint)
- Remove `.sh` files entirely as backwards compatibility is not required at this phase
- Eliminate inline Python heredocs in `flash_flare.sh` — use direct imports
- Update all doc references to prefer `python3 scripts/foo.py`

**Non-Goals:**
- Windows support (no user base, cmake/ninja cross-compile toolchain not set up)
- Changing script behavior — pure port, same flow, same output
- Keeping `.sh` wrappers
- Porting `flare_daemon.service` (systemd unit file, not a script)

## Decisions

### 1. Platform abstraction approach

Use `platform.system()` returning `"Linux"` or `"Darwin"` for branching. All filesystem ops via `pathlib.Path` + `shutil`. Subprocess calls via `subprocess.run(..., check=True)`.

Alternative: `sys.platform` — rejected because `platform.system()` returns cleaner names matching existing `uname` checks in bash.

### 2. `sudo` and privilege escalation

Call `subprocess.run(["sudo", ...])` for mount/umount/mkdir/cp when on Linux, same as bash. Root check via `os.getuid() == 0`. `$SUDO_USER` via `os.environ.get("SUDO_USER")`.

Alternative: use `os.mount()` / ctypes — rejected, not portable, adds complexity. `subprocess` wrapping `sudo` is standard pattern.

### 3. Serial I/O in `flash_flare.py`

Import `serial_utils.find_port()` directly instead of heredoc shelling out. BOOTSEL trigger + VR verify become regular functions using `import serial`.

### 4. Systemd templating in `install_daemon.py`

Replace `sed -e "s|{{USER}}|..."` with `Path.read_text().replace("{{USER}}", ...)`. Cleaner, no subprocess.

### 5. Remove `.sh` wrappers

The `.sh` files are completely deleted from the codebase. All tools and documentations are updated to run the `.py` scripts directly.

### 6. ANSI color output

Reuse existing ANSI codes (same `\033[...m` strings). Add `NO_COLOR` / `TERM=dumb` detection for non-interactive environments:
```python
import os, sys
USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None
```

### Per-file design

### `scripts/validate_regression.py`

Simplest port. Sequence of `subprocess.run()` calls:
1. `python3 scripts/gen_config.py`
2. `cmake -S firmware -B build_local -DFLARE_DEV_TUNING=ON`
3. `ninja -C build_local`
4. `python3 -m py_compile scripts/*.py` (use `glob` to expand)
5. `ruff check scripts/` (with `shutil.which("ruff")` check)
6. `python3 -m unittest discover -s scripts -p "test_*.py"`
7. `python3 scripts/test_flare_mmu_status.py`
8. `git diff --check`

Repo root resolved via `Path(__file__).resolve().parent.parent`.

### `scripts/flash_flare.py`

Functions ported 1:1 from bash:
- `find_pico_sdk_path()` → check `PICO_SDK_PATH` env, then candidate dirs via `Path.is_file()`
- `find_picotool()` → `PICOTOOL` env, `shutil.which("picotool")`, candidate build dirs
- `picotool_supports_load()` → `subprocess.run([bin, "help"], capture_output=True)` + grep
- `find_rp2_mountpoint()` → iterate candidate paths, `Path.is_dir()`
- `find_rp2_device()` → platform-branched: `lsblk` parse on Linux, `diskutil` on macOS
- `find_and_mount_rp2()` → combine mountpoint + device discovery + `subprocess.run(["sudo", "mount", ...])` or `diskutil mount`
- `find_serial_port()` → direct `from serial_utils import find_port` (no heredoc)
- `send_bootsel()` → direct `import serial` (no heredoc)
- `verify_fw_version()` → direct `import serial` (no heredoc)

CLI args: `--gcc` flag via `argparse`. Env vars: `BUILD_DIR`, `PICO_SDK_PATH`, `PICOTOOL`.

### `scripts/install_daemon.py`

Functions:
- Root check: `os.getuid() != 0` → error with `sudo python3` suggestion
- User resolution: `os.environ.get("SUDO_USER", getpass.getuser())`
- Pyserial dep check: `try: import serial` / except → `subprocess.run(["apt-get", ...])`
- Service template: `Path.read_text().replace(...)` → write to `/etc/systemd/system/`
- systemd ops: `subprocess.run(["systemctl", "daemon-reload"])` etc.
- Klipper extras copy: `shutil.copy2()` + `os.chown()`
- Klipper config: `shutil.copy2()` with existence check

CLI args: `--no-klipper` flag via `argparse`.

## Risks / Trade-offs

- **`subprocess` for system tools is same as bash** — no actual portability gain for `cmake`, `ninja`, `picotool`, `systemctl`, `mount`. Gain is language consistency, not removing external deps.
- **`py_compile` glob expansion** — bash `scripts/*.py` auto-expands; Python needs `glob.glob()`. Use `glob.glob("scripts/*.py")` + pass as list. → Low risk.
- **Root/sudo pattern in Python** — `os.getuid()` works on macOS and Linux. `subprocess.run(["sudo", ...])` prompts same as bash. → No regression.
- **Serial port timing** — heredoc approach spawns separate Python process; direct import keeps same process. BOOTSEL reboot timing unchanged (same `time.sleep()` calls). → Low risk, test on hardware.
- **Doc reference churn** — 20+ doc files reference `.sh` scripts. Wrappers prevent breakage but docs should be updated. → Medium effort, low risk.
