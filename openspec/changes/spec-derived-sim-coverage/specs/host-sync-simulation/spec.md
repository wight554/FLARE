## ADDED Requirements

### Requirement: Scenario Catalogue Traceable To Spec Scenarios

Where a `tests/host/sim_scenario.c` scenario exists specifically to exercise behavior described by an `openspec/specs/*` `#### Scenario:` block, the C scenario's comment SHALL name the source spec and scenario title, and `scripts/test_sync_sim.py`'s corresponding test SHALL assert on the real observed event/state text, not the spec's stated text, if they diverge.

#### Scenario: Scenario traces to its spec source

- **WHEN** a `tests/host/sim_scenario.c` entry is added to cover a spec
  `#### Scenario:` block
- **THEN** its comment names the spec and scenario title
- **AND** the corresponding python test asserts against the real firmware
  event/state text, with a docstring note if it differs from the spec's

### Requirement: Spec/Code Mismatches Are Flagged, Not Silently Fixed

Where a scenario built to test a spec's stated behavior instead observes different real firmware behavior, the harness SHALL NOT hand-edit the live `openspec/specs/*` file, since those are merged specs outside this change's authority; the finding SHALL be recorded in `memories/repo/host-sync-sim.md`, and, where the affected spec has its own catalogue entry in `TEST_CASES.md`, noted there too.

#### Scenario: Mismatch recorded, spec untouched

- **WHEN** a scenario's real observed behavior contradicts its source spec's
  stated `#### Scenario:` text
- **THEN** `openspec/specs/*` is not edited by this change
- **AND** the mismatch is recorded in `memories/repo/host-sync-sim.md` with
  the real vs. stated behavior

### Requirement: Non-Runtime-Observable Scenarios Are Skipped With Reason

Spec scenarios that are not observable from `flare_sim`'s CSV trace or exit code SHALL be skipped rather than approximated, with the reason recorded — this covers protocol.c wire-format scenarios, pure code-structure requirements such as "no early return" or "isolated static function", and scenarios requiring an unlinked subsystem such as Klipper or the python daemon/tuner.

#### Scenario: Unreachable scenario documented, not faked

- **WHEN** a spec scenario cannot be observed via `flare_sim`'s trace/exit
  code
- **THEN** no `tests/host/sim_scenario.c` entry is built for it
- **AND** the reason is recorded (in the spec's disposition note or
  `memories/repo/host-sync-sim.md`)
