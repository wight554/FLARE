#!/usr/bin/env python3
"""
install_daemon.py — FLARE Background Daemon Systemd Service Installer.

Installs the flare_daemon systemd unit, enables/starts the service,
and optionally integrates Klipper mock extras + config.

Usage:
    sudo python3 scripts/install_daemon.py [--no-klipper]
"""
from __future__ import annotations

import argparse
import getpass
import os
import platform
import pwd
import shutil
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# ANSI colors (respects NO_COLOR and non-tty)
# ---------------------------------------------------------------------------
_use_color = sys.stdout.isatty() and not os.environ.get("NO_COLOR")
RED = "\033[0;31m" if _use_color else ""
GREEN = "\033[0;32m" if _use_color else ""
YELLOW = "\033[1;33m" if _use_color else ""
BLUE = "\033[0;34m" if _use_color else ""
NC = "\033[0m" if _use_color else ""

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
PROJECT_DIR = Path(__file__).resolve().parent.parent
SCRIPT_DIR = PROJECT_DIR / "scripts"
SERVICE_NAME = "flare_daemon.service"
SERVICE_TEMPLATE = SCRIPT_DIR / "flare_daemon.service"
SERVICE_DEST = Path("/etc/systemd/system") / SERVICE_NAME


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def info(msg: str) -> None:
    print(msg)


def banner(text: str, color: str = BLUE) -> None:
    bar = f"{color}=================================================={NC}"
    print(bar)
    print(f"{color}{text}{NC}")
    print(bar)


def resolve_user() -> str:
    """Return the real (non-root) user who invoked sudo."""
    user = os.environ.get("SUDO_USER") or getpass.getuser()
    if user == "root":
        user = "pi"  # fallback for Raspberry Pi / standard Klipper images
    return user


def _chown_to_user(path: Path, user: str) -> None:
    """Set file ownership to *user*:*user*, silently ignore errors."""
    try:
        pw = pwd.getpwnam(user)
        os.chown(path, pw.pw_uid, pw.pw_gid)
    except (KeyError, OSError):
        pass


def _file_contains(path: Path, marker: str) -> bool:
    """Return True if *path* contains *marker* anywhere."""
    try:
        return marker in path.read_text()
    except OSError:
        return False


def _find_dir(candidates: list[Path]) -> Path | None:
    """Return the first existing directory from *candidates*, or None."""
    for p in candidates:
        if p.is_dir():
            return p
    return None


def _install_klipper_extra(
    src: Path,
    dest: Path,
    marker: str,
    user: str,
) -> None:
    """Install or update a single Klipper extra module."""
    if dest.is_file():
        if _file_contains(dest, marker):
            info(f"Found existing {marker} at {dest}. Updating to latest version.")
            shutil.copy2(src, dest)
            _chown_to_user(dest, user)
        else:
            info(
                f"{YELLOW}Warning: A custom {dest.name} already exists at {dest} "
                f"but does NOT contain '{marker}' (e.g. Happy Hare). "
                f"Preserving it to avoid conflicts.{NC}"
            )
    else:
        info(f"Installing {marker} to {dest}")
        shutil.copy2(src, dest)
        _chown_to_user(dest, user)


# ---------------------------------------------------------------------------
# Steps
# ---------------------------------------------------------------------------
def step_install_deps() -> None:
    """[1/4] Install Python dependencies (pyserial)."""
    info(f"\n{BLUE}[1/4] Installing Python dependencies...{NC}")
    try:
        import serial  # noqa: F401
        info(f"{GREEN}pyserial is already installed.{NC}")
    except ImportError:
        info("Installing 'pyserial' package...")
        if shutil.which("apt-get"):
            try:
                subprocess.run(
                    ["apt-get", "update", "-y"],
                    check=True,
                )
                subprocess.run(
                    ["apt-get", "install", "-y", "python3-serial"],
                    check=True,
                )
                return
            except subprocess.CalledProcessError:
                pass
        # Fallback to pip
        for pip_args in (
            ["pip3", "install", "pyserial", "--break-system-packages"],
            ["pip3", "install", "pyserial"],
        ):
            try:
                subprocess.run(pip_args, check=True)
                return
            except (subprocess.CalledProcessError, FileNotFoundError):
                continue


def step_systemd_unit(user: str) -> None:
    """[2/4] Generate and install the systemd unit file."""
    info(f"\n{BLUE}[2/4] Setting up systemd unit file...{NC}")

    if not SERVICE_TEMPLATE.is_file():
        info(f"{RED}Error: Service template not found at {SERVICE_TEMPLATE}{NC}")
        sys.exit(1)

    template = SERVICE_TEMPLATE.read_text()
    unit = template.replace("{{USER}}", user).replace("{{DIR}}", str(PROJECT_DIR))
    SERVICE_DEST.write_text(unit)
    os.chmod(SERVICE_DEST, 0o644)

    info(f"{GREEN}Systemd service file installed to {SERVICE_DEST}{NC}")


def step_enable_start() -> None:
    """[3/4] Enable and start the flare_daemon service."""
    info(f"\n{BLUE}[3/4] Enabling and starting flare_daemon...{NC}")

    subprocess.run(["systemctl", "daemon-reload"], check=True)
    subprocess.run(["systemctl", "enable", SERVICE_NAME], check=True)
    subprocess.run(["systemctl", "restart", SERVICE_NAME], check=True)

    result = subprocess.run(
        ["systemctl", "is-active", "--quiet", SERVICE_NAME],
    )
    if result.returncode == 0:
        info(f"{GREEN}FLARE daemon is active and running successfully!{NC}")
    else:
        info(f"{RED}Warning: FLARE daemon service failed to start cleanly.{NC}")
        info(f"Check logs with: journalctl -u {SERVICE_NAME} -n 50")


def step_klipper(user: str) -> None:
    """[4/4] Integrate Klipper extras and config."""
    info(f"\n{BLUE}[4/4] Finalizing integration...{NC}")
    info(f"{GREEN}Klipper Mode active.{NC}")

    home = Path(f"/home/{user}")

    # --- klippy/extras ---
    extras_dir = _find_dir([
        home / "klipper" / "klippy" / "extras",
        Path("/home/pi/klipper/klippy/extras"),
        Path("/home/klipper/klipper/klippy/extras"),
    ])

    if extras_dir:
        _install_klipper_extra(
            src=PROJECT_DIR / "klipper" / "mmu.py",
            dest=extras_dir / "mmu.py",
            marker="FLARE MMU Mock",
            user=user,
        )
        _install_klipper_extra(
            src=PROJECT_DIR / "klipper" / "mmu_sensors.py",
            dest=extras_dir / "mmu_sensors.py",
            marker="FLARE MMU Sensors Mock",
            user=user,
        )
    else:
        info(
            f"{YELLOW}Warning: Klipper extras directory not found. "
            f"Skipping mmu.py and mmu_sensors.py installation.{NC}"
        )
        info(
            "If Klipper is installed in a non-standard location, please copy "
            "'klipper/mmu.py' and 'klipper/mmu_sensors.py' to your "
            "'klippy/extras/' directory manually."
        )

    # --- printer_data/config ---
    config_dir = _find_dir([
        home / "printer_data" / "config",
        Path("/home/pi/printer_data/config"),
        Path("/home/klipper/printer_data/config"),
    ])

    if config_dir:
        target_cfg = config_dir / "flare_mmu.cfg"
        if target_cfg.is_file():
            info(
                f"Found existing flare_mmu.cfg at {target_cfg}. "
                "Preserving existing configuration."
            )
        else:
            info(f"Installing flare_mmu.cfg to {target_cfg}")
            shutil.copy2(PROJECT_DIR / "klipper" / "flare_mmu.cfg", target_cfg)
            _chown_to_user(target_cfg, user)
    else:
        info(
            f"{YELLOW}Warning: Klipper printer_data/config directory not found. "
            f"Skipping flare_mmu.cfg installation.{NC}"
        )
        info(
            f"Please copy '{PROJECT_DIR}/klipper/flare_mmu.cfg' "
            "to your Klipper config directory manually."
        )

    info("Ensure Klipper config includes flare macro file.")
    info("Dashboard will be served on http://localhost:8088")


def step_klipper_skip() -> None:
    """[4/4] Standalone mode — skip Klipper integration."""
    info(f"\n{BLUE}[4/4] Finalizing integration...{NC}")
    info(f"{YELLOW}Standalone Mode (--no-klipper) active.{NC}")
    info("Skipping Klipper & Moonraker configurations.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    parser = argparse.ArgumentParser(
        description="FLARE Background Daemon Systemd Service Installer",
    )
    parser.add_argument(
        "--no-klipper",
        action="store_true",
        help="Skip Klipper/Moonraker integration (standalone mode)",
    )
    args = parser.parse_args()

    # Header banner
    banner("      FLARE Background Daemon Installer            ", GREEN)

    # Platform gate — systemd is Linux-only
    if platform.system() != "Linux":
        info(
            f"{RED}Error: This installer requires Linux (systemd). "
            f"Current platform: {platform.system()}{NC}"
        )
        sys.exit(1)

    # Root check
    if os.getuid() != 0:
        info(f"{RED}Error: Please run this script with sudo or as root:{NC}")
        info("  sudo python3 scripts/install_daemon.py")
        sys.exit(1)

    real_user = resolve_user()
    info(f"Installing under user: {YELLOW}{real_user}{NC}")
    info(f"Project path:         {YELLOW}{PROJECT_DIR}{NC}")

    step_install_deps()
    step_systemd_unit(real_user)
    step_enable_start()

    if args.no_klipper:
        step_klipper_skip()
    else:
        step_klipper(real_user)

    # Final banner
    print()
    bar = f"{GREEN}=================================================={NC}"
    print(bar)
    print(f"{GREEN}             FLARE Service Ready!                 {NC}")
    print("  - WebUI URL:     http://localhost:8088")
    print(f"  - View Logs:     journalctl -u {SERVICE_NAME} -f")
    print(f"  - Start Service: systemctl start {SERVICE_NAME}")
    print(f"  - Stop Service:  systemctl stop {SERVICE_NAME}")
    print(bar)


if __name__ == "__main__":
    main()
