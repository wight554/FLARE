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
  `RL:`, `CU:`, `CX:`, or `MV:`
- **THEN** the script waits for that command's completion or error event after
  the initial `OK`

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
- **AND** loads/picks up the new filament into the extruder only after the
  sensor reports filament detected again

### Requirement: Reusable Toolhead Unload Macro
The include SHALL provide a standalone `FLARE_UNLOAD_TOOLHEAD` macro.

#### Scenario: Standalone Unload
- **WHEN** `FLARE_UNLOAD_TOOLHEAD` is invoked
- **THEN** it executes `_FLARE_TIP_FORMING` followed by `G1 E-{gear_retract}` to clear the extruder gears
- **AND** restores the G-code state safely without initiating any subsequent MMU tool change or lane swap

### Requirement: Toolhead Sensor Optional For TC Completion
`TC:` load completion SHALL NOT require an explicit host `TS:1` command.

#### Scenario: Geometry Load Completion
- **WHEN** the FLARE `TASK_LOAD_FULL` task reaches its loaded condition from `TS:1`, `TS_BUF_MS`, or sane buffer geometry
- **THEN** toolchange proceeds to `TC:DONE`
- **AND** the macro does not need to send `TS:1`, `TS:0`, or `SM:`
