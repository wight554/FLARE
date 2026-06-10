# python-host-tooling-style Specification (Delta)

## MODIFIED Requirements

### Requirement: Python lint in the regression gate

`scripts/validate_regression.py` SHALL run `ruff check scripts/` so lint regressions
fail the gate alongside the existing `py_compile` and `unittest` steps.

#### Scenario: A lint regression is introduced

- **WHEN** a script change introduces a selected-rule violation and the gate runs
- **THEN** `validate_regression.py` fails at the ruff step
