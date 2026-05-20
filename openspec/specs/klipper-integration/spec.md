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

### Requirement: Motion Tracking Sidecar
The sidecar (`--uds`) SHALL track Klipper's print state and forward speed events to FLARE.

#### Scenario: UDS Stream
- **WHEN** Klipper's Unix Domain Socket emits toolhead or print_stats changes
- **THEN** the sidecar translates them into `FLARE_TUNE` events and `BASELINE_SPS` updates
- **AND** sends them over serial without blocking normal macro commands

### Requirement: Macro Orchestration
Toolchange macros (`_FLARE_TC`) SHALL coordinate the extruder, MMU, and toolhead state.

#### Scenario: Full TC Macro
- **WHEN** a toolchange is triggered
- **THEN** Klipper enables `RA:1` retract assist gate before tip forming
- **AND** clears retract assist with `RA:0` after tip forming drains
- **AND** starts `MV:-gear_retract:gear_retract_spd` before the long
  gear-clear printer retract
- **AND** calls `TC:` only after the gear-clear printer retract drains
- **AND** calls `flare_cmd.py TC:lane` without explicit `TS:` or `SM:` helper commands
- **AND** waits for `EV:TC:DONE`
- **AND** loads/picks up the new filament into the extruder

### Requirement: Toolhead Sensor Optional For TC Completion
`TC:` load completion SHALL NOT require an explicit host `TS:1` command.

#### Scenario: Geometry Load Completion
- **WHEN** the FLARE `TASK_LOAD_FULL` task reaches its loaded condition from `TS:1`, `TS_BUF_MS`, or sane buffer geometry
- **THEN** toolchange proceeds to `TC:DONE`
- **AND** the macro does not need to send `TS:1`, `TS:0`, or `SM:`
