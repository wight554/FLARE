## 1. Lint config

- [ ] 1.1 Add `pyproject.toml` `[tool.ruff]`: `line-length = 100`, `target-version = "py39"`, `select = ["E","F","W","I","N","UP","B"]`, `ignore = ["E501"]` (line wrapping deferred to a future `ruff format`)
- [ ] 1.2 Confirm `ruff` (0.7.x) resolves the config: `ruff check scripts/ --statistics`

## 2. Fix findings

- [ ] 2.1 Apply auto-fixes: `ruff check scripts/ --fix` (unused imports/vars, empty f-strings, import sort, `UP` modernizations, whitespace, fixable bugbear)
- [ ] 2.2 Manual fixes for the non-auto findings: one-line `with: stmt` (E701), naming `N801/N802/N806/N818`, `B904` raise-from, ambiguous name `E741`
- [ ] 2.3 `ruff check scripts/` is clean (0 violations in the selected set)

## 3. Verify behavior

- [ ] 3.1 `python3 -m py_compile scripts/*.py` passes
- [ ] 3.2 `python3 -m unittest discover -s scripts -p "test_*.py"` stays 46/46 green

## 4. Gate + docs

- [ ] 4.1 Add `ruff check scripts/` to `scripts/validate_regression.sh` after the `py_compile` step
- [ ] 4.2 Note the Python lint convention in `STYLE.md` (firmware = clang-format/clang-tidy; host tooling = ruff)

## 5. Final verify

- [ ] 5.1 Run `scripts/validate_regression.sh` (or its python steps); confirm lint clean, suite green, no behavior change
