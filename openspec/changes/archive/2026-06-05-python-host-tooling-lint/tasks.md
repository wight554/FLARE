## 1. Lint config

- [x] 1.1 Add `pyproject.toml` `[tool.ruff]`: `line-length = 100`, `target-version = "py39"`, `select = ["E","F","W","I","N","UP","B"]`, `ignore = ["E501"]` (line wrapping deferred to a future `ruff format`)
  - 2026-06-05: also ignored `UP006/UP007/UP035` (PEP 585/604 typing rewrites are runtime-version-coupled; host tooling may run under 3.9 Klipper Python).
- [x] 1.2 Confirm `ruff` (0.7.x) resolves the config: `ruff check scripts/ --statistics`
  - 2026-06-05: ruff 0.7.3; 201 findings under config pre-fix.

## 2. Fix findings

- [x] 2.1 Apply auto-fixes: `ruff check scripts/ --fix` (unused imports/vars, empty f-strings, import sort, `UP` modernizations, whitespace, fixable bugbear)
  - 2026-06-05: 153 safe + 14 unsafe (W291/B007/B011/UP022/UP031/F841, reviewed) + 1 cascading UP032 fixed.
- [x] 2.2 Manual fixes for the non-auto findings: one-line `with: stmt` (E701), naming `N801/N802/N806/N818`, `B904` raise-from, ambiguous name `E741`
  - 2026-06-05: split 20 E701 one-liners; `B904` -> `raise SystemExit(2) from None`; `E741` `l`->`ln`. Naming findings are framework/domain contracts (http.server `do_*`, pyserial-compat `Serial`/`SerialException`/`serial_utils` stubs, Kalman `K`/`R`, lane `A`/`B`) -> documented `per-file-ignores`, not renamed.
- [x] 2.3 `ruff check scripts/` is clean (0 violations in the selected set)
  - 2026-06-05: `All checks passed!`

## 3. Verify behavior

- [x] 3.1 `python3 -m py_compile scripts/*.py` passes
- [x] 3.2 `python3 -m unittest discover -s scripts -p "test_*.py"` stays 46/46 green

## 4. Gate + docs

- [x] 4.1 Add `ruff check scripts/` to `scripts/validate_regression.sh` after the `py_compile` step
  - 2026-06-05: added a "Python Lint (ruff)" step; errors with an install hint if ruff is absent.
- [x] 4.2 Note the Python lint convention in `STYLE.md` (firmware = clang-format/clang-tidy; host tooling = ruff)
  - 2026-06-05: added STYLE.md §6 "Host Tooling (Python)".

## 5. Final verify

- [x] 5.1 Run `scripts/validate_regression.sh` (or its python steps); confirm lint clean, suite green, no behavior change
  - 2026-06-05: full `validate_regression.sh` passed end-to-end (gen_config, ninja build, py_compile, ruff, 46 unittests, mock MMU self-test 26/26, diff hygiene). "Static Regression Gate Passed".
