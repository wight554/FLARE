## ADDED Requirements

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
