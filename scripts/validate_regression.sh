#!/usr/bin/env bash
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"

cd "$REPO"

echo "=== Generate Config ==="
python3 scripts/gen_config.py

echo "=== Firmware Build ==="
ninja -C build_local

echo "=== Python Syntax ==="
python3 -m py_compile scripts/*.py

echo "=== Python Lint (ruff) ==="
if command -v ruff >/dev/null 2>&1; then
    ruff check scripts/
else
    echo "ruff not found; install it (pip install ruff / brew install ruff)" >&2
    exit 1
fi

echo "=== Python Unit Test Suite ==="
python3 -m unittest discover -s scripts -p "test_*.py"

echo "=== Mock MMU Status Self-Test ==="
python3 scripts/test_flare_mmu_status.py

echo "=== Diff Hygiene ==="
git diff --check

echo "=== Static Regression Gate Passed ==="
echo "Run the hardware validation cases in TEST_CASES.md for motion, sync, toolchange, or RELOAD changes."
