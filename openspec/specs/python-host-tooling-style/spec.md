# python-host-tooling-style Specification

## Purpose
Enforced ruff lint contract + clean baseline for the `scripts/` Python host tooling,
pinned in `pyproject.toml` and wired into the regression gate, so the host code a human
maintainer touches stays lint-clean and behavior-preserving.
## Requirements
### Requirement: Ruff lint configuration

Repo SHALL carry a `pyproject.toml` `[tool.ruff]` config pinning `line-length`,
`target-version`, and an explicit rule `select` set, and `scripts/*.py` SHALL pass
`ruff check` under it.

#### Scenario: Scripts pass lint

- **WHEN** `ruff check scripts/` runs under the repo `pyproject.toml`
- **THEN** it reports no violations in the selected rule set

#### Scenario: Config is pinned

- **WHEN** a contributor reads `pyproject.toml`
- **THEN** `[tool.ruff]` pins `line-length`, `target-version`, and `select`
- **AND** any deferred rule (e.g. `E501`) is listed in `ignore` with intent

### Requirement: Python lint in the regression gate

`scripts/validate_regression.sh` SHALL run `ruff check scripts/` so lint regressions
fail the gate alongside the existing `py_compile` and `unittest` steps.

#### Scenario: A lint regression is introduced

- **WHEN** a script change introduces a selected-rule violation and the gate runs
- **THEN** `validate_regression.sh` fails at the ruff step

### Requirement: Behavior-preserving lint cleanup

Lint fixes SHALL be behavior-preserving; the `scripts/` `unittest` suite SHALL stay
green and host-tooling behavior unchanged.

#### Scenario: Lint cleanup is applied

- **WHEN** the lint fixes are applied
- **THEN** `python3 -m unittest discover -s scripts -p "test_*.py"` passes
- **AND** no host-tooling output or protocol behavior changes

