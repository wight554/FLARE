# filament-bypass Specification

## Purpose
Defines the host and UI behavior for selecting filament bypass instead of a numbered MMU lane.

## Requirements
### Requirement: Local Filament Bypass State
The host daemon and Klipper mock SHALL expose a unified `bypass` boolean and status field, mapped to Happy Hare gate/tool sentinel `-2`.

#### Scenario: Selecting bypass in Klipper
- **WHEN** G-code command `MMU_SELECT BYPASS=1` or `MMU_SELECT_BYPASS` is run
- **THEN** Klipper MMU mock sets `bypass = True`, `active_gate = -2`, `gate = -2`, and `tool = -2`
- **AND** exports `bypass` in its status dictionary to Klipper macros and UIs

#### Scenario: Selecting bypass in Standalone WebUI
- **WHEN** the user clicks the virtual Filament Bypass card in the WebUI
- **THEN** the WebUI highlights the card, sets `bypassActive = true` locally
- **AND** sends `POST /cmd {"cmd": "T:0"}` to disengage any active lane on the board

### Requirement: Single-Sensor Bypass Telemetry
Under bypass mode, the system SHALL report exactly one active sensor: the toolhead sensor (`TS`). All other gate, pre-gate, and combiner sensors SHALL be forced to inactive.

#### Scenario: Telemetry status check while bypassed
- **WHEN** Klipper queries the MMU status under bypass mode
- **THEN** Klipper returns `pre_gate_sensor_active = False`, `gate_sensor_active = False`, `hub_sensor_active = False`, `extruder_sensor_active = False`
- **AND** sets `filament_position` to `path_len` if `toolhead_sensor == 1`, else `0.0`
- **AND** reports `mmu_pre_gate = False`, `mmu_gear = False`, `mmu_gate = False`, and `toolhead = True` (if sensor triggered) in the `sensors` dictionary

### Requirement: Manual Feed and Autoload Trigger
Filament SHALL be manually fed through the bypass lane without MMU drive assistance until it triggers the toolhead sensor, which SHALL immediately and automatically invoke the autoload sequence.

#### Scenario: Filament insert triggers autoload
- **WHEN** the operator manually feeds filament through the bypass tube until it triggers the toolhead sensor (`TS:1`)
- **THEN** Klipper automatically executes `MMU_LOAD` (or the equivalent macro/script)
- **AND** the extruder gears grab the filament (`G1 E{load_park_dist}`) and push it to the meltzone (`_FLARE_LOAD_HOTEND`) without any MMU motor interactions

### Requirement: MMU-Free Extruder-Only Actions
Under bypass mode, all load and unload sequences SHALL ignore and completely suppress any physical MMU lane motor serial commands, executing strictly as toolhead extruder-only operations.

#### Scenario: Load while bypassed
- **WHEN** `MMU_LOAD` is executed in Klipper while bypassed
- **THEN** Klipper bypasses the MMU, runs `_FLARE_LOAD_HOTEND` (heating, extruder gear grab `G1 E{load_park_dist}`, and meltzone push)
- **AND** sends no `FL:`, `LO:`, or `MV:` commands to the board

#### Scenario: Unload while bypassed
- **WHEN** `MMU_UNLOAD` is executed in Klipper while bypassed
- **THEN** Klipper bypasses the MMU, runs `FLARE_UNLOAD_TOOLHEAD` (heating, tip-forming `_FLARE_TIP_FORMING`, and gear retract `G1 E-{gear_retract}`)
- **AND** sends no `UL:`, `UM:`, or `MV:` commands to the board

### Requirement: Suppressed Eject under Bypass
All MMU lane eject procedures SHALL be skipped and safely suppressed under bypass mode, with no serial command executed.

#### Scenario: Eject while bypassed
- **WHEN** `MMU_EJECT` or `FLARE_EJECT` is executed while bypassed
- **THEN** Klipper prints a G-code console response "Bypass active; no physical eject needed. Please manually pull the filament strand out."
- **AND** returns immediately without sending `UM:` or any serial command to the board
