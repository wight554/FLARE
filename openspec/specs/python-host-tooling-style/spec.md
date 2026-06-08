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

### Requirement: Diagnostic scripts shall stay referenced

Every script under `scripts/` SHALL have at least one live (non-archived) reference: a
Python import, a live doc mention, or a backing spec. A standalone diagnostic whose only
references are archived OpenSpec changes is dead and SHALL be removed; its history remains
recoverable via `git show <rev>:scripts/<name>.py`. The regression gate (`py_compile`,
`ruff`, and `unittest discover -p test_*.py`) SHALL stay green across the removal, since a
dead script has no live importer or test.

#### Scenario: A diagnostic loses its last live reference

- **WHEN** a `scripts/` diagnostic is imported by nothing, mentioned in no live doc, and
  backed by no spec — referenced only from archived changes
- **THEN** the script is deleted from the tree and the regression gate still passes with
  the same `unittest` count

#### Scenario: A referenced diagnostic is retained

- **WHEN** a `scripts/` diagnostic is imported by a live module, named in a live doc, or
  backed by a live spec
- **THEN** it is retained regardless of how recently it was last edited

