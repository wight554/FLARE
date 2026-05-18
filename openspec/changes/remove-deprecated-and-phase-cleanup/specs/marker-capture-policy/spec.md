## ADDED Requirements

### Requirement: Sidecar is the only capture path

The Klipper sidecar path SHALL be the single live-capture mechanism. The
shell-marker capture path SHALL NOT exist: `gcode_marker.py` SHALL NOT
provide shell `--emit` modes (`m118`/`mark`/`file`/`both`) and SHALL NOT
provide `--every-layer`; `flare_live_tuner.py` SHALL NOT provide
`--klipper-mode off`, `--marker-file`, or `--keep-marker-file`. The
legacy `scripts/flare_marker.py` and `scripts/flare_logger.py` SHALL be
removed.

#### Scenario: No shell-marker surface remains

- **WHEN** `gcode_marker.py --help` and `flare_live_tuner.py --help` are
  inspected
- **THEN** no shell `--emit` mode, `--every-layer`, `--klipper-mode off`,
  `--marker-file`, or `--keep-marker-file` option is present

#### Scenario: Legacy scripts are gone

- **WHEN** the `scripts/` directory is listed
- **THEN** `flare_marker.py` and `flare_logger.py` do not exist and no
  tracked file references them as usable tools

### Requirement: No deprecation notices remain

Because the deprecated features are removed, no deprecation notice SHALL
remain in tracked code or docs. There SHALL be no `deprecated` stderr
warning and no `**DEPRECATED**` documentation label for these paths, and
no documentation section SHALL describe a removed path.

#### Scenario: Deprecation notices absent

- **WHEN** tracked scripts and docs (excluding `openspec/changes/` and git
  history) are searched for deprecation wording tied to the removed paths
- **THEN** none is found, and the surviving capture documentation
  describes only the sidecar path

### Requirement: Docs describe current behavior without phase labels

Operator and context documentation SHALL describe the system as it exists
now. Internal "Phase 2.x"-style milestone labels SHALL NOT appear in
`CONTEXT.md`, `BEHAVIOR.md`, `KLIPPER.md`, or `MANUAL.md`. Surviving
technical content SHALL be reworded as current behavior rather than
deleted.

#### Scenario: No phase labels in docs

- **WHEN** the tracked docs are searched for an internal phase label
- **THEN** none is present and the equivalent behavior is stated as
  current

#### Scenario: CONTEXT.md is a phase-free expanded nav

- **WHEN** a contributor opens `CONTEXT.md`
- **THEN** it contains no phase prose, presents a current module/directory
  map and a "where to look" index, and cross-links the specs, `TUNING.md`,
  and `BEHAVIOR.md` instead of restating durable contracts
