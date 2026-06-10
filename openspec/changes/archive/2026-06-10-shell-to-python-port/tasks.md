## 1. Port `validate_regression.sh`

- [x] 1.1 Create `scripts/validate_regression.py`: `subprocess.run()` orchestration matching exact bash sequence (gen_config → cmake → ninja → py_compile → ruff → unittest → mmu_status self-test → git diff --check). Use `pathlib.Path(__file__).resolve().parent.parent` for repo root. `shutil.which("ruff")` for tool detection. `glob.glob()` for `*.py` expansion in py_compile step.
- [x] 1.2 Add color output helper with `sys.stdout.isatty()` + `NO_COLOR` detection. Stage headers (`=== Generate Config ===`) match existing output.
- [x] 1.3 Remove `scripts/validate_regression.sh` entirely.
- [x] 1.4 Verify: run `python3 scripts/validate_regression.py` — all gates pass same as old `.sh`

## 2. Port `flash_flare.sh`

- [x] 2.1 Create `scripts/flash_flare.py` with `argparse` (`--gcc` flag). Port `find_pico_sdk_path()` — iterate candidate `Path` dirs checking `pico_sdk_init.cmake` existence. Respect `PICO_SDK_PATH` env var.
- [x] 2.2 Port `find_picotool()`: `PICOTOOL` env → `shutil.which("picotool")` → candidate build dirs. Port `picotool_supports_load()`: `subprocess.run([bin, "help"], capture_output=True)` + `"load"` search in output.
- [x] 2.3 Port `find_rp2_mountpoint()` and `find_rp2_device()`: branch via `platform.system()`. Linux path: `lsblk` parse. macOS path: `diskutil list` / `diskutil info` parse. Iterate candidate mount paths.
- [x] 2.4 Port `find_and_mount_rp2()`: already-mounted check → device detect → mount. Linux: `subprocess.run(["sudo", "mount", ...])`. macOS: `subprocess.run(["diskutil", "mount", ...])`.
- [x] 2.5 Port serial functions as direct Python — replace heredoc pattern. `find_serial_port()` → `from serial_utils import find_port`. `send_bootsel(port)` → direct `import serial`, same logic as heredoc. `verify_fw_version(port)` → direct `import serial`, same VR: polling loop.
- [x] 2.6 Port main flow: cmake configure/build → BOOTSEL trigger → picotool load (primary path) → UF2 fallback (mount/copy/unmount) → serial wait → VR verify. Preserve all stage output.
- [x] 2.7 Remove `scripts/flash_flare.sh` entirely.
- [ ] 2.8 Verify: `python3 scripts/flash_flare.py --help` shows `--gcc` flag. Build path logic matches old bash. (HW: full flash cycle on RPi — defer to hardware validation)

## 3. Port `install_daemon.sh`

- [x] 3.1 Create `scripts/install_daemon.py` with `argparse` (`--no-klipper` flag). Root check via `os.getuid() != 0`. User resolution via `os.environ.get("SUDO_USER", getpass.getuser())` with `"pi"` fallback.
- [x] 3.2 Port pyserial dep check: `try: import serial` → if missing, `subprocess.run(["apt-get", ...])` on Linux, error message on macOS (pyserial install via pip).
- [x] 3.3 Port systemd service setup: `Path.read_text().replace("{{USER}}", ...).replace("{{DIR}}", ...)` → write to `/etc/systemd/system/flare_daemon.service`. `subprocess.run(["systemctl", "daemon-reload"])` etc. Add `platform.system() == "Linux"` guard — error on macOS explaining systemd requirement.
- [x] 3.4 Port Klipper extras copy: iterate candidate `klippy/extras` dirs. `shutil.copy2()` + `os.chown()`. Preserve Happy Hare conflict detection (`"FLARE MMU Mock"` / `"FLARE MMU Sensors Mock"` grep via `Path.read_text()`).
- [x] 3.5 Port Klipper config install: `flare_mmu.cfg` copy with existence-preservation check. Same candidate dir probing.
- [x] 3.6 ANSI color output with terminal detection (reuse helper from validate_regression.py or factor into shared module).
- [x] 3.7 Remove `scripts/install_daemon.sh` entirely.
- [ ] 3.8 Verify: `python3 scripts/install_daemon.py --help` shows `--no-klipper`. Root check works. (HW: full install cycle on RPi — defer to hardware validation)

## 4. Shared utilities

- [x] 4.1 Factor color output helper into reusable module or inline pattern. Decide: shared `script_utils.py` or inline in each script. Prefer inline if < 10 lines — avoid new module for trivial helper.
- [x] 4.2 Ensure all 3 new `.py` files pass `ruff check` and `python3 -m py_compile`

## 5. Documentation updates

- [x] 5.1 Update BUILD_FLASH.md: `bash scripts/flash_flare.sh` → `python3 scripts/flash_flare.py`
- [x] 5.2 Update README.md: flash command reference
- [x] 5.3 Update MANUAL.md: flash command reference
- [x] 5.4 Update KLIPPER.md: `bash scripts/install_daemon.sh` → `sudo python3 scripts/install_daemon.py`
- [x] 5.5 Update TEST_CASES.md: flash + regression references
- [x] 5.6 Update TUNING.md: flash reference
- [x] 5.7 Update WORKFLOW.md: regression script reference
- [x] 5.8 Update STYLE.md: regression script reference
- [x] 5.9 Update AGENTS.md: `scripts/validate_regression.sh` reference → `.py`
- [x] 5.10 Update CONTEXT.md: script references

## 6. Spec updates (durable)

- [x] 6.1 Update `openspec/specs/static-regression-validation/spec.md`: scenario references from `.sh` to `.py`
- [x] 6.2 Update `openspec/specs/python-host-tooling-style/spec.md`: gate script reference from `.sh` to `.py`
- [x] 6.3 Create `openspec/specs/cross-platform-script-tooling/spec.md` from change spec

## 7. Final validation

- [x] 7.1 Run `python3 scripts/validate_regression.py` — full gate green
- [x] 7.2 Verify deprecated `.sh` wrappers are completely removed from tree
- [x] 7.3 `ruff check scripts/` clean
- [ ] 7.4 HW: Flash cycle via `python3 scripts/flash_flare.py` on Raspberry Pi
- [ ] 7.5 HW: `sudo python3 scripts/install_daemon.py` on Raspberry Pi — daemon starts, Klipper extras installed
