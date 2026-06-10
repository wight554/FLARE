#!/usr/bin/env python3
"""flash_flare.py — build, flash, and verify FLARE firmware.

Port of flash_flare.sh.  Supports picotool (primary) and UF2 mass-storage
(fallback).  Works on Linux (RPi / Debian / Ubuntu) and macOS.
"""

from __future__ import annotations

import argparse
import errno
import os
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Color helpers
# ---------------------------------------------------------------------------

_USE_COLOR = sys.stdout.isatty() and not os.environ.get("NO_COLOR")


def _red(msg: str) -> str:
    return f"\033[31m{msg}\033[0m" if _USE_COLOR else msg


def _yellow(msg: str) -> str:
    return f"\033[33m{msg}\033[0m" if _USE_COLOR else msg


def _green(msg: str) -> str:
    return f"\033[32m{msg}\033[0m" if _USE_COLOR else msg


def _bold(msg: str) -> str:
    return f"\033[1m{msg}\033[0m" if _USE_COLOR else msg


# ---------------------------------------------------------------------------
# Path constants
# ---------------------------------------------------------------------------

REPO = Path(__file__).resolve().parent.parent


# ---------------------------------------------------------------------------
# SDK / tool discovery
# ---------------------------------------------------------------------------


def find_pico_sdk_path() -> Optional[Path]:
    """Locate the Pico SDK.  Checks PICO_SDK_PATH env then common dirs."""
    candidates: list[Path] = []

    env_val = os.environ.get("PICO_SDK_PATH")
    if env_val:
        candidates.append(Path(env_val))

    candidates.extend([
        REPO / "pico-sdk",
        Path.home() / "pico-sdk",
        Path("/opt/pico-sdk"),
        Path("/usr/local/pico-sdk"),
    ])

    for path in candidates:
        if (path / "pico_sdk_init.cmake").is_file():
            return path
    return None


def find_picotool() -> Optional[Path]:
    """Locate the picotool binary."""
    env_val = os.environ.get("PICOTOOL")
    if env_val:
        p = Path(env_val)
        if p.is_file() and os.access(p, os.X_OK):
            return p

    which = shutil.which("picotool")
    if which:
        return Path(which)

    candidates = [
        REPO / "build_clang" / "_deps" / "picotool" / "picotool",
        REPO / "build_clang" / "picotool" / "picotool",
        REPO / "build_local" / "_deps" / "picotool" / "picotool",
        REPO / "build_local" / "picotool" / "picotool",
        REPO / "build_audit" / "_deps" / "picotool" / "picotool",
    ]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return c
    return None


def picotool_supports_load(binary: Path) -> bool:
    """Return True if *binary* advertises a ``load`` subcommand."""
    try:
        result = subprocess.run(
            [str(binary), "help"],
            capture_output=True,
            text=True,
        )
        combined = result.stdout + result.stderr
        for token in combined.split():
            if token == "load":
                return True
    except Exception:
        pass
    return False


# ---------------------------------------------------------------------------
# RPI-RP2 device / mount helpers
# ---------------------------------------------------------------------------


def find_rp2_mountpoint() -> Optional[Path]:
    """Return already-mounted RPI-RP2 path, or None."""
    user = os.environ.get("USER", "pi")
    candidates = [
        Path(f"/media/{user}/RPI-RP2"),
        Path("/media/pi/RPI-RP2"),
        Path(f"/run/media/{user}/RPI-RP2"),
        Path("/mnt/RPI-RP2"),
        Path("/Volumes/RPI-RP2"),
    ]
    for p in candidates:
        if p.is_dir():
            return p
    return None


def find_rp2_device() -> Optional[str]:
    """Retry loop to detect the RPI-RP2 block device (Linux only)."""
    max_retries = 10
    for attempt in range(max_retries):
        rp2_dev: Optional[str] = None

        if shutil.which("lsblk"):
            try:
                result = subprocess.run(
                    ["lsblk", "-n", "-d", "-o", "NAME,MODEL"],
                    capture_output=True,
                    text=True,
                )
                for line in result.stdout.splitlines():
                    if "rpi-rp2" in line.lower():
                        name = line.split()[0]
                        rp2_dev = f"/dev/{name}"
                        break
            except Exception:
                pass

        if not rp2_dev and Path("/dev/sda1").is_block_device():
            rp2_dev = "/dev/sda1"

        if rp2_dev:
            return rp2_dev

        if attempt < max_retries - 1:
            time.sleep(0.5)

    return None


def find_and_mount_rp2() -> Optional[Path]:
    """Detect, mount if needed, and return RPI-RP2 mount path."""
    # Already mounted?
    mounted = find_rp2_mountpoint()
    if mounted:
        return mounted

    is_mac = platform.system() == "Darwin"
    rp2_dev: Optional[str] = None

    if is_mac:
        # macOS: diskutil list scan
        if shutil.which("diskutil"):
            try:
                result = subprocess.run(
                    ["diskutil", "list"],
                    capture_output=True,
                    text=True,
                )
                for line in result.stdout.splitlines():
                    if "RPI-RP2" in line.upper():
                        parts = line.split()
                        if parts:
                            rp2_dev = f"/dev/{parts[-1]}"
                            break
            except Exception:
                pass
        if not rp2_dev:
            import glob

            for dev in sorted(glob.glob("/dev/disk[0-9]s[0-9]*")):
                if Path(dev).is_block_device():
                    try:
                        info = subprocess.run(
                            ["diskutil", "info", dev],
                            capture_output=True,
                            text=True,
                        )
                        if "RPI-RP2" in info.stdout.upper():
                            rp2_dev = dev
                            break
                    except Exception:
                        pass
    else:
        # Linux: lsblk scan
        if shutil.which("lsblk"):
            try:
                result = subprocess.run(
                    ["lsblk", "-n", "-d", "-o", "NAME,MODEL"],
                    capture_output=True,
                    text=True,
                )
                for line in result.stdout.splitlines():
                    if "rpi-rp2" in line.lower():
                        rp2_dev = f"/dev/{line.split()[0]}"
                        break
            except Exception:
                pass
        if not rp2_dev:
            for letter in "abcdef":
                dev = f"/dev/sd{letter}1"
                if Path(dev).is_block_device():
                    try:
                        sz_out = subprocess.run(
                            ["lsblk", "-bn", "-o", "SIZE", dev],
                            capture_output=True,
                            text=True,
                        )
                        size = int(sz_out.stdout.strip() or "0")
                    except Exception:
                        size = 0
                    if 100_000_000 < size < 600_000_000:
                        rp2_dev = dev
                        break

    if not rp2_dev:
        return None

    # Mount
    if is_mac:
        print(
            "Warning: macOS usually auto-mounts RPI-RP2 to /Volumes/RPI-RP2.",
            file=sys.stderr,
        )
        print(f"Attempting diskutil mount on {rp2_dev}...", file=sys.stderr)
        if shutil.which("diskutil"):
            subprocess.run(["diskutil", "mount", rp2_dev], stderr=subprocess.STDOUT)
        rp2_mount = Path("/Volumes/RPI-RP2")
    else:
        rp2_mount = Path("/mnt/RPI-RP2")
        print(f"Mounting {rp2_dev} to {rp2_mount}...", file=sys.stderr)
        subprocess.run(["sudo", "mkdir", "-p", str(rp2_mount)], check=True)
        result = subprocess.run(["sudo", "mount", rp2_dev, str(rp2_mount)])
        if result.returncode != 0:
            return None

    if rp2_mount.is_dir():
        return rp2_mount
    return None


# ---------------------------------------------------------------------------
# Serial helpers
# ---------------------------------------------------------------------------

# Ensure scripts/ is importable for serial_utils.
_scripts_dir = str(Path(__file__).resolve().parent)
if _scripts_dir not in sys.path:
    sys.path.insert(0, _scripts_dir)


def find_serial_port() -> Optional[str]:
    """Return the FLARE serial port using serial_utils.find_port()."""
    from serial_utils import find_port

    return find_port()


def send_bootsel(port: str) -> None:
    """Send BOOT: command to trigger BOOTSEL reboot."""
    import serial

    try:
        s = serial.Serial(port, 115200, timeout=2)
        time.sleep(0.1)
        s.write(b"BOOT:\n")
        s.close()
        print("Reboot command sent.")
    except OSError as e:
        if e.errno == errno.EIO:
            print("Rebooting into BOOTSEL...")
        else:
            print(f"BOOTSEL trigger skipped ({e})")
    except Exception as e:
        print(f"BOOTSEL trigger skipped ({e})")


def verify_fw_version(port: str) -> None:
    """Poll VR: on *port* and print the first response line."""
    import serial

    try:
        s = serial.Serial(port, 115200, timeout=0.5)
        deadline = time.time() + 8.0
        next_request_at = 0.0
        pending = ""
        out = ""
        time.sleep(0.5)
        s.reset_input_buffer()
        while time.time() < deadline:
            now = time.time()
            if now >= next_request_at:
                s.write(b"VR:\n")
                next_request_at = now + 1.0

            chunk = s.read(max(s.in_waiting, 1)).decode(errors="replace")
            if not chunk:
                continue

            pending += chunk
            lines = pending.replace("\r", "\n").split("\n")
            pending = lines.pop() if lines else ""

            for line in lines:
                line = line.strip()
                if line:
                    out = line
                    break
            if out:
                break

        if not out:
            out = pending.strip()
        print(out or "No VR response")
        s.close()
    except Exception as e:
        print(f"Unable to verify firmware version on {port}: {e}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def _die(msg: str) -> None:
    print(_red(f"Error: {msg}"), file=sys.stderr)
    sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build, flash, and verify FLARE firmware.")
    parser.add_argument("--gcc", action="store_true", help="Use GCC preset (default: clang)")
    args = parser.parse_args()

    if args.gcc:
        build_dir = Path(os.environ.get("BUILD_DIR", str(REPO / "build_local")))
        preset = "gcc"
    else:
        build_dir = Path(os.environ.get("BUILD_DIR", str(REPO / "build_clang")))
        preset = "clang"

    uf2_path = build_dir / "flare_controller.uf2"
    elf_path = build_dir / "flare_controller.elf"

    # ---- Build ----
    print(_bold("=== Build ==="))
    build_dir.mkdir(parents=True, exist_ok=True)

    if not (build_dir / "build.ninja").is_file():
        print(f"Configuring CMake in {build_dir}")
        sdk_path = find_pico_sdk_path()
        if sdk_path is None:
            _die(
                "Pico SDK not found.\n"
                "Set PICO_SDK_PATH and rerun, for example:\n"
                "  PICO_SDK_PATH=$HOME/pico-sdk python3 scripts/flash_flare.py\n"
                "or clone pico-sdk into one of:\n"
                f"  {REPO / 'pico-sdk'}\n"
                f"  {Path.home() / 'pico-sdk'}"
            )

        cmake_args = [
            "cmake",
            "--preset",
            preset,
            "-S",
            str(REPO / "firmware"),
            "-B",
            str(build_dir),
            f"-DPICO_SDK_PATH={sdk_path}",
        ]
        print(f"Using PICO_SDK_PATH={sdk_path} with preset {preset}")
        subprocess.run(cmake_args, check=True)

    subprocess.run(["cmake", "--build", str(build_dir)], check=True)

    # ---- BOOTSEL trigger ----
    serial_port = find_serial_port()
    if serial_port:
        print(_bold(f"=== Trigger BOOTSEL on {serial_port} ==="))
        send_bootsel(serial_port)
    else:
        print(_bold("=== No serial port found; assuming device already in BOOTSEL ==="))

    # ---- Flash ----
    print(_bold("=== Flash ==="))
    picotool_bin = find_picotool()
    if picotool_bin is None:
        _die("picotool not found. Install picotool or set PICOTOOL=/path/to/picotool")

    image_path = uf2_path if uf2_path.is_file() else elf_path
    if not image_path.is_file():
        _die(f"build output not found ({uf2_path} or {elf_path})")

    if picotool_supports_load(picotool_bin):
        subprocess.run([str(picotool_bin), "load", str(image_path), "-f"], check=True)
        subprocess.run([str(picotool_bin), "reboot"], check=True)
    else:
        print(
            _yellow(
                "Warning: picotool has no USB load support; "
                "falling back to UF2 mass-storage copy."
            )
        )
        if not uf2_path.is_file():
            _die(f"UF2 image not found at {uf2_path} (required for mass-storage flashing).")

        # Wait for RPI-RP2 mount
        print(_bold("=== Locating/Mounting RPI-RP2 device ==="))
        rp2_mount: Optional[Path] = None

        for i in range(1, 21):
            rp2_mount = find_and_mount_rp2()
            if rp2_mount:
                print(f"RPI-RP2 successfully located/mounted at: {rp2_mount}")
                break
            print(f"Waiting for RPI-RP2... {i}s")
            time.sleep(1)

        # Legacy raw-mount fallback (Linux only)
        if not rp2_mount:
            print(
                "Already-mounted check / automatic mount failed. "
                "Attempting legacy raw mount fallback..."
            )
            rp2_dev: Optional[str] = None
            for _i in range(1, 11):
                for letter in "abcdef":
                    dev = f"/dev/sd{letter}1"
                    if Path(dev).is_block_device():
                        try:
                            sz_out = subprocess.run(
                                ["lsblk", "-bn", "-o", "SIZE", dev],
                                capture_output=True,
                                text=True,
                            )
                            size = int(sz_out.stdout.strip() or "0")
                        except Exception:
                            size = 0
                        if 100_000_000 < size < 600_000_000:
                            rp2_dev = dev
                            print(f"Found device {dev} ({size} bytes)")
                            break
                if rp2_dev:
                    break
                time.sleep(1)

            if not rp2_dev:
                msg = "Could not find RPI-RP2 device."
                if platform.system() == "Darwin":
                    msg += (
                        "\nNote: On macOS, ensure the RPI-RP2 volume "
                        "is mounted under /Volumes/RPI-RP2."
                    )
                _die(msg)

            rp2_mount = Path("/mnt/RPI-RP2")
            subprocess.run(["sudo", "mkdir", "-p", str(rp2_mount)], check=True)
            result = subprocess.run(["sudo", "mount", rp2_dev, str(rp2_mount)])
            if result.returncode != 0:
                _die(f"Failed to mount {rp2_dev}")

        # Copy UF2
        print(f"Copying UF2 to {rp2_mount}...")
        is_volumes = str(rp2_mount).startswith("/Volumes/")
        try:
            if is_volumes:
                shutil.copy2(str(uf2_path), str(rp2_mount))
            else:
                subprocess.run(
                    ["sudo", "cp", str(uf2_path), str(rp2_mount) + "/"],
                    check=True,
                )
        except Exception:
            if not is_volumes:
                subprocess.run(["sudo", "umount", str(rp2_mount)], capture_output=True)
            _die("Failed to copy UF2 file")

        # Sync + unmount
        subprocess.run(["sync"], check=True)
        print(f"Unmounting {rp2_mount}...")
        if is_volumes:
            if shutil.which("diskutil"):
                subprocess.run(["diskutil", "unmount", str(rp2_mount)])
            else:
                subprocess.run(["umount", str(rp2_mount)])
        else:
            result = subprocess.run(["sudo", "umount", str(rp2_mount)])
            if result.returncode != 0:
                _die(f"Failed to unmount {rp2_mount}")

        # Wait for USB serial re-enumeration
        print(_bold("=== Waiting for USB serial ==="))
        serial_port = None
        for i in range(1, 16):
            serial_port = find_serial_port()
            if serial_port:
                print(f"Serial up after {i}s")
                break
            if i < 15:
                print(f"Waiting... {i}s")
                time.sleep(1)

    # ---- Verify ----
    print(_bold("=== Verify ==="))
    serial_port = find_serial_port()
    if serial_port:
        verify_fw_version(serial_port)
    else:
        print("Serial port not detected; skipping VR check.")

    print(_bold("=== Done ==="))


if __name__ == "__main__":
    main()
