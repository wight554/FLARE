#!/usr/bin/env python3
"""Static regression gate — Python port of validate_regression.sh.

Runs the full pre-commit validation sequence:
  1. gen_config.py           (generate tune.h from config.ini)
  2. cmake configure         (dev-tuning superset)
  3. ninja build
  4. host sync simulation    (build flare_sim, run the scenario suite)
  5. py_compile all scripts
  6. ruff lint
  7. unittest discover
  8. mock MMU status self-test
  9. git diff --check
"""

from __future__ import annotations

import glob
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------------
# Color helpers
# ---------------------------------------------------------------------------

_USE_COLOR = sys.stdout.isatty() and not os.environ.get("NO_COLOR")

_BOLD_CYAN = "\033[1;36m" if _USE_COLOR else ""
_BOLD_GREEN = "\033[1;32m" if _USE_COLOR else ""
_RESET = "\033[0m" if _USE_COLOR else ""


def _header(text: str) -> None:
    print(f"{_BOLD_CYAN}=== {text} ==={_RESET}")


def _success(text: str) -> None:
    print(f"{_BOLD_GREEN}{text}{_RESET}")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------


def _run(*cmd: str, **kwargs: object) -> None:
    """Run a command in REPO_ROOT; abort on failure (mirrors set -e)."""
    subprocess.run(cmd, cwd=REPO_ROOT, check=True, **kwargs)  # noqa: S603


def main() -> None:
    # 1 — Generate config
    _header("Generate Config")
    _run("python3", "scripts/gen_config.py")

    # 2+3 — Firmware build (dev-tuning superset)
    _header("Firmware Build (FLARE_DEV_TUNING=ON superset)")
    _run(
        "cmake",
        "-S",
        "firmware",
        "-B",
        "build_local",
        "-DFLARE_DEV_TUNING=ON",
        stdout=subprocess.DEVNULL,
    )
    _run("ninja", "-C", "build_local")

    # 4 — Host sync simulation. Compiles the real sync/motion/toolchange
    # sources against tests/host/'s fakes and plant, then runs the scenario
    # suite. A build break here is a firmware source defect (the simulation
    # links firmware/src/*.c verbatim), not an optional/skippable test — see
    # design.md "Risks": sim rots as sync evolves unless wired into this gate.
    _header("Host Sync Simulation")
    _run(
        "cmake",
        "-S",
        "tests/host",
        "-B",
        "build_sim",
        "-G",
        "Ninja",
        stdout=subprocess.DEVNULL,
    )
    _run("ninja", "-C", "build_sim")
    result = subprocess.run(
        ["python3", "-m", "unittest", "scripts.test_sync_sim", "-v"],
        cwd=REPO_ROOT,
    )
    if result.returncode != 0:
        print("Host sync simulation scenario suite failed.", file=sys.stderr)
        sys.exit(result.returncode)

    # 5 — Python syntax
    _header("Python Syntax")
    py_files = sorted(glob.glob(str(REPO_ROOT / "scripts" / "*.py")))
    if not py_files:
        print("no scripts/*.py found", file=sys.stderr)
        sys.exit(1)
    _run("python3", "-m", "py_compile", *py_files)

    # 6 — Python lint (ruff)
    _header("Python Lint (ruff)")
    if shutil.which("ruff") is None:
        print(
            "ruff not found; install it (pip install ruff / brew install ruff)",
            file=sys.stderr,
        )
        sys.exit(1)
    _run("ruff", "check", "scripts/")

    # 7 — Python unit test suite (includes scripts/test_sync_sim.py again —
    # cheap, ~8s total per design.md's Runtime Budget, and keeps this step
    # self-sufficient for anyone running just `unittest discover`)
    _header("Python Unit Test Suite")
    _run("python3", "-m", "unittest", "discover", "-s", "scripts", "-p", "test_*.py")

    # 8 — Mock MMU status self-test
    _header("Mock MMU Status Self-Test")
    _run("python3", "scripts/test_flare_mmu_status.py")

    # 9 — Diff hygiene
    _header("Diff Hygiene")
    _run("git", "diff", "--check")

    # Done
    _success("=== Static Regression Gate Passed ===")
    print(
        "Run the hardware validation cases in TEST_CASES.md"
        " for motion, sync, toolchange, or RELOAD changes."
    )


if __name__ == "__main__":
    main()
