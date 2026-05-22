# Klipper Integration Specification

## Purpose
Durable contract for FLARE Klipper integration (`flare_cmd.py`), extracted from `KLIPPER.md` and script sources.
## Requirements
### Requirement: Host Serial Control
The Klipper host MUST interact with FLARE via single-command CDC serial transactions.

#### Scenario: Script Invocation
- **WHEN** a Klipper macro calls `flare_cmd.py`
- **THEN** the script opens the serial port, sends the formatted command
- **AND** blocks until an `OK:` or `ER:` response is received
- **AND** returns the result to Klipper via stdout

#### Scenario: Long-Running Completion Waits
- **WHEN** a Klipper macro calls `flare_cmd.py` with `FL:`, `UL:`, `UM:`,
  `RL:`, `CU:`, or `CX:`
- **THEN** the script waits for that command's completion or error event after
  the initial `OK`

#### Scenario: MV Returns After Acceptance
- **WHEN** a Klipper macro calls `flare_cmd.py MV:...`
- **THEN** the script returns after the initial firmware `OK`
- **AND** firmware continues the finite move asynchronously
- **AND** Klipper may immediately run the coordinated printer-side motion

#### Scenario: TC Returns After Acceptance
- **WHEN** a Klipper macro calls `flare_cmd.py TC:<lane>`
- **THEN** the script returns after the initial firmware `OK`
- **AND** delayed Klipper toolhead-sensor polling owns post-TC hotend loading

### Requirement: Motion Tracking Sidecar
The sidecar (`--uds`) SHALL track Klipper's print state and forward speed events to FLARE.

#### Scenario: UDS Stream
- **WHEN** Klipper's Unix Domain Socket emits toolhead or print_stats changes
- **THEN** the sidecar translates them into `FLARE_TUNE` events and `BASELINE_SPS` updates
- **AND** sends them over serial without blocking normal macro commands

### Requirement: Macro Orchestration
Toolchange macros (`_FLARE_CHANGE_LANE` / `T1` / `T2`) SHALL coordinate the
extruder, MMU, and toolhead state.

#### Scenario: Full TC Macro
- **WHEN** a toolchange is triggered
- **THEN** Klipper runs a finite `MV:-distance:feed:I` old-lane retract during
  tip forming via `FLARE_UNLOAD_TOOLHEAD` so the MMU ignores buffer state only for that exact move
- **AND** calls `TC:` only after the gear-clear printer retract drains
- **AND** calls `flare_cmd.py TC:lane` without explicit `TS:` or `SM:` helper commands
- **AND** arms delayed toolhead-sensor polling before `TC:`
- **AND** waits 2 seconds after the sensor reports filament detected again
- **AND** loads/picks up the new filament into the extruder via a two-stage
  approach: MMU alone push past the sensor (`MV:{dist_sensor_to_synced_move}:I`),
  followed by a synchronized grab and park move (`G1 E{load_park_dist}`)

### Requirement: Reusable Toolhead Unload Macro
The include SHALL provide a standalone `FLARE_UNLOAD_TOOLHEAD` macro.

#### Scenario: Standalone Unload
- **WHEN** `FLARE_UNLOAD_TOOLHEAD` is invoked
- **THEN** it calls `_FLARE_PARK` before heating and tip forming
- **AND** it executes `_FLARE_TIP_FORMING` followed by `G1 E-{gear_retract}` to clear the extruder gears
- **AND** restores the G-code state safely without initiating any subsequent MMU tool change or lane swap

### Requirement: Dashboard load and eject target selected gates
Dashboard `MMU_LOAD` and `MMU_EJECT` commands SHALL use the currently selected
gate, not only the board's active lane.

#### Scenario: Selected gate load
- **WHEN** `MMU_LOAD` is invoked for gate 1
- **THEN** Klipper runs `FLARE_LOAD LANE=2`
- **AND** the FLARE macro selects lane 2 before issuing `FL:`
- **AND** after firmware reports loaded, Klipper runs `_FLARE_POST_TC_LOAD
  LANE=2` to perform the same extruder grab, hotend load, and purge handoff
  used after toolchanges

#### Scenario: Selected preloaded gate eject
- **WHEN** `MMU_EJECT` is invoked for a selected gate that is preloaded but not
  loaded to the toolhead
- **THEN** Klipper skips `FLARE_UNLOAD_TOOLHEAD`
- **AND** runs `FLARE_EJECT LANE=<selected lane>` so firmware receives
  `UM:<selected lane>`

#### Scenario: Selected loaded gate eject
- **WHEN** `MMU_EJECT` is invoked for a selected gate loaded to the toolhead
- **THEN** Klipper runs `FLARE_UNLOAD_TOOLHEAD` before `FLARE_EJECT LANE=<selected lane>`

### Requirement: Toolhead Sensor Optional For TC Completion
`TC:` load completion SHALL NOT require an explicit host `TS:1` command.

#### Scenario: Geometry Load Completion
- **WHEN** the FLARE `TASK_LOAD_FULL` task reaches its loaded condition from `TS:1`, `TS_BUF_MS`, or sane buffer geometry
- **THEN** toolchange proceeds to `TC:DONE`
- **AND** the macro does not need to send `TS:1`, `TS:0`, or `SM:`

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
