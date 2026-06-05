## Why

Host tooling under `scripts/` (25 Python files, ~25k lines: 13 source + 12
`test_*`) had no lint config and was deferred by `code-readability-overhaul`
(firmware-first). A `ruff` scan finds ~523 issues: unused imports/vars, empty
f-strings, one-line `with: stmt`, unsorted imports, dated typing, naming, and
225 line-too-long. The same readability standard the firmware now enforces should
cover the Python a human maintainer will also touch.

## What Changes

- Add `pyproject.toml` `[tool.ruff]`: `line-length = 100`, `target-version = "py39"`
  (portable floor), `select = E,F,W,I,N,UP,B`. `E501` is deferred (line wrapping is a
  formatter job; no reflow this change) and listed in `ignore` with a note.
- Fix every finding the selected set flags (~298 excl. E501): `ruff check --fix` for the
  auto-fixable (unused imports/vars, empty f-strings, import sort, `UP` modernizations,
  whitespace, bugbear) plus manual fixes for the non-auto ones (one-line `with`, naming
  `N801/N802/N806/N818`, `B904`, ambiguous `E741`).
- Wire `ruff check scripts/` into `scripts/validate_regression.sh` (after `py_compile`).
- Note the Python lint convention in `STYLE.md` (firmware = clang; host = ruff).

No behavior change to host tooling; the 46-test `unittest` suite must stay green.
No whole-tree `ruff format` reflow (deferred; `line-length` pinned for when it runs).

## Capabilities

### New Capabilities
- `python-host-tooling-style`: enforced `ruff` lint config + clean baseline for the
  `scripts/` host tooling, wired into the regression gate.

## Impact

- New file: `pyproject.toml`. Touched: most `scripts/*.py` (lint fixes),
  `scripts/validate_regression.sh`, `STYLE.md`.
- Out of scope: `ruff format` reflow (E501 line wrapping), type annotations / `mypy`.
- Risk: lint fixes are mechanical; the `unittest` suite is the behavior guard
  (must stay 46/46 green).
