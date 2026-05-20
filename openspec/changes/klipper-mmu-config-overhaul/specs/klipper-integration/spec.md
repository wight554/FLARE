## MODIFIED Requirements

### Requirement: KLIPPER.md scope is integration-only
KLIPPER.md SHALL cover: serial port setup, shell command helper,
toolhead sensor wiring, reference to `flare_mmu.cfg`, and the
troubleshooting table. It SHALL NOT contain buffer sync tuning,
calibration print workflows, gcode_marker usage, or telemetry/analyzer
instructions — those belong exclusively in `TUNING.md`.

#### Scenario: Tuning content removed
- **WHEN** a user reads KLIPPER.md
- **THEN** they find no `BASELINE_RATE`, `SYNC_KP_RATE`, `BUF_ALPHA`,
  `flare_analyze.py`, `gcode_marker.py`, or calibration print instructions;
  a pointer to `TUNING.md` is present instead

#### Scenario: Integration content retained
- **WHEN** a user reads KLIPPER.md
- **THEN** they find serial port setup, `gcode_shell_command flare`
  config, toolhead sensor wiring, `flare_mmu.cfg` include instructions,
  and the troubleshooting table

### Requirement: Toolhead sensor section presents one path with fallback note
KLIPPER.md SHALL document the physical sensor as the primary path.
The buffer-geometry fallback (TS_BUF_MS) SHALL appear as a brief note
explaining it is automatic — not as a parallel "Option B" requiring
user configuration.

#### Scenario: Option B removed as separate section
- **WHEN** a user reads the toolhead sensor section of KLIPPER.md
- **THEN** there is no "Option B" heading; a single note explains
  that without a physical sensor FLARE falls back to buffer geometry
  and `dist_sensor_to_extruder: 0` should be set in `_FLARE_VARS`
