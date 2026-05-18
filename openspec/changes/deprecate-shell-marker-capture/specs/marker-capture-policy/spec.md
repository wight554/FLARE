## ADDED Requirements

### Requirement: Sidecar is the supported live-capture path

The Klipper sidecar path SHALL be the single supported and recommended
live-capture path for tuning. Operator documentation SHALL present sidecar
as the path; it SHALL NOT present shell-marker capture as first-class or
as an equal alternative.

#### Scenario: Docs present sidecar as the path

- **WHEN** a user reads the capture section of `TUNING.md`
- **THEN** sidecar is presented as the supported path and shell-marker is
  marked deprecated, not first-class

### Requirement: Shell-marker capture is deprecated with stated rationale

The shell-marker capture path SHALL be documented as deprecated, and the
documentation SHALL state the reason in operator terms: it injects
per-layer `RUN_SHELL_COMMAND`/`M118` into the printed G-code, blocking
Klipper's G-code queue and causing print lag, whereas sidecar injects
nothing into the printed file.

#### Scenario: Deprecation rationale is explicit

- **WHEN** a user reads the shell-marker section
- **THEN** it is labelled deprecated and explains the print-lag cause and
  the sidecar alternative

### Requirement: Deprecation does not remove or break shell-marker

Deprecation SHALL be communicated, not enforced. `gcode_marker.py --emit
m118|mark|file|both`, `flare_live_tuner.py --klipper-mode off`, and
`--marker-file` SHALL continue to function with unchanged behavior and no
removed flags. Existing shell-marker workflows MUST keep working.

#### Scenario: Shell-marker still works

- **WHEN** a user runs a shell-marker `--emit` mode or the tuner with
  `--klipper-mode off` / `--marker-file`
- **THEN** it produces the same output as before, only with an added
  deprecation warning

### Requirement: Runtime deprecation warnings

Selecting a shell-marker path SHALL emit exactly one clear stderr
deprecation warning per process. `gcode_marker.py`'s existing shell-mode
warning SHALL include the print-lag rationale. `flare_live_tuner.py` SHALL
emit one deprecation warning when started with `--klipper-mode off` or
with `--marker-file` and no Klipper UDS. Warnings SHALL NOT be emitted
per-layer or repeated.

#### Scenario: gcode_marker warns once with rationale

- **WHEN** `gcode_marker.py` is run with a shell `--emit` mode
- **THEN** a single stderr line states the mode is deprecated and that it
  causes print lag, and output is still written

#### Scenario: tuner warns once on shell-marker input

- **WHEN** `flare_live_tuner.py` starts with `--klipper-mode off` or
  `--marker-file` without UDS
- **THEN** it prints one stderr deprecation warning and then runs normally
