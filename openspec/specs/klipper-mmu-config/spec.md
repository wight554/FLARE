# klipper-mmu-config Specification

## Purpose
Specifies the bundled Klipper MMU config surface that lets users include one file for FLARE macros and UI state.
## Requirements
### Requirement: Single-file Klipper MMU config
`klipper/flare_mmu.cfg` SHALL provide a complete Klipper MMU integration
that users can activate with a single `[include flare_mmu.cfg]` line in
`printer.cfg`, with no other macro files required.

#### Scenario: Single include activates all macros
- **WHEN** user adds `[include flare_mmu.cfg]` to `printer.cfg`
- **THEN** macros `T0`, `T1`, `FLARE_LOAD`, `FLARE_PRELOAD`, `FLARE_UNLOAD`,
  `FLARE_EJECT`, `FLARE_TEST_TIP_FORMING`, `_FLARE_CHANGE_LANE`,
  `_FLARE_CG28`, `_FLARE_TIP_FORMING`, `_FLARE_LOAD_HOTEND`, `_FLARE_PARK`,
  `_FLARE_PURGE`, `_FLARE_SET_PURGE` are all available without further
  configuration

### Requirement: Variables block with SP-compatible distance names
`[gcode_macro _FLARE_VARS]` SHALL expose all user-configurable distances
using the same names as the LH-Stinger Pico MMU wiki
(`dist_sensor_to_extruder`, `dist_filament_park`,
`dist_extruder_to_meltzone`) and SHALL add `dist_meltzone_to_nozzle_tip` for
the hotend length needed by FLARE's tip-forming MMU assist.

#### Scenario: Distance variable names match SP wiki
- **WHEN** a user measures distances following the SP toolhead distance
  calibration guide
- **THEN** they can set `variable_dist_sensor_to_extruder`,
  `variable_dist_filament_park`, and `variable_dist_extruder_to_meltzone` in
  `_FLARE_VARS` with no name translation required

#### Scenario: Tip-forming MMU retract uses full hotend path
- **WHEN** `_FLARE_TIP_FORMING` starts the ignore-buffer MMU retract
- **THEN** the distance is derived as `dist_sensor_to_extruder +
  dist_extruder_to_meltzone + dist_meltzone_to_nozzle_tip`

#### Scenario: dist_filament_park constraint documented
- **WHEN** `dist_filament_park` is set to a value ≥ `dist_extruder_to_meltzone`
- **THEN** a comment in the file warns that `dist_filament_park` MUST be
  less than `dist_extruder_to_meltzone`

#### Scenario: Purge speed is converted to Klipper feedrate
- **WHEN** `_FLARE_VARS.variable_purge_speed` is set to `30.0`
- **THEN** pickup, meltzone approach, and purge extrusion use Klipper
  feedrates derived as `purge_speed * 60`, so the effective extrusion speed is
  30 mm/s rather than F30

### Requirement: Tip forming macro with cooldown and dip phases
`_FLARE_TIP_FORMING` SHALL implement the full tip forming sequence
(post-pause push, cooldown pull, optional secondary moves, optional dip,
final fast retract to park position) reading parameters from
`_FLARE_TIP_FORMING_DEFAULTS`.

#### Scenario: Default tip forming executes without error
- **WHEN** `_FLARE_TIP_FORMING` is called with hotend at temperature
- **THEN** macro executes cooldown and park moves, leaves filament at
  `dist_filament_park` from meltzone

#### Scenario: Dip phase uses tuned defaults
- **WHEN** `_FLARE_TIP_FORMING` runs with the shared defaults
- **THEN** it uses `dip_melt_gap=0.1`, `dip_speed=30.0`, and `dip_pause=10`

#### Scenario: Secondary cooldown moves enabled by default
- **WHEN** `_FLARE_TIP_FORMING` runs with the shared defaults
- **THEN** it executes the secondary cooldown moves after the first cooldown
  pull

### Requirement: Load hotend macro with 3-stage meltzone approach
`_FLARE_LOAD_HOTEND` SHALL advance filament from the park position to the
meltzone in three stages (50% fast / 25% normal / 25% slow) and call
`_FLARE_PURGE` when `purge_len` is greater than zero.

#### Scenario: Push distance matches SP formula
- **WHEN** `_FLARE_LOAD_HOTEND` is called after PICKUP
- **THEN** total push distance equals
  `dist_extruder_to_meltzone - dist_filament_park - tip_length_below_cut`

#### Scenario: Purge skipped when purge_len is zero
- **WHEN** `variable_purge_len: 0`
- **THEN** no purge extrusion is performed after meltzone approach

### Requirement: Purge helper is simple plain purge
`_FLARE_PURGE` SHALL own purge extrusion separately from `_FLARE_LOAD_HOTEND`.
It SHALL implement the simple upstream `_SP_PURGE` core shape: purge the
requested relative extrusion amount at `purge_speed`, then perform a small
0.4 mm retract. It SHALL call `_FLARE_HEAT_HOTEND` before purge extrusion and
call an empty `_FLARE_PARK` hook for user-provided park macros. Purge chute
parking, blob splitting, and brush moves SHALL be left to user-provided
`_FLARE_PARK` customization or wrapper macros.

#### Scenario: Default plain purge
- **WHEN** `_FLARE_PURGE PURGE=30` is called
- **THEN** it extrudes 30 mm at `_FLARE_VARS.purge_speed * 60` without XY
  parking
- **AND** it retracts 0.4 mm at 35 mm/s after the purge
- **AND** it calls the default empty `_FLARE_PARK` hook before heating

#### Scenario: Purge verifies hotend temperature
- **WHEN** `_FLARE_PURGE PURGE=30` is called while the extruder target is below
  `_FLARE_VARS.min_extrude_temp`
- **THEN** `_FLARE_HEAT_HOTEND` heats to `_FLARE_VARS.load_temp` and waits
  before purge extrusion starts

### Requirement: Manual load and eject route selected lanes
`FLARE_LOAD` and `FLARE_EJECT` SHALL preserve active-lane behavior when called
without `LANE`, and SHALL target a selected lane when `LANE=1` or `LANE=2` is
provided.

#### Scenario: Selected lane load
- **WHEN** `FLARE_LOAD LANE=2` is invoked
- **THEN** the macro sends `T:2` before `FL:`

#### Scenario: Selected lane eject
- **WHEN** `FLARE_EJECT LANE=2` is invoked
- **THEN** the macro sends `UM:2`

#### Scenario: Direct active-lane behavior preserved
- **WHEN** `FLARE_LOAD` or `FLARE_EJECT` is invoked without `LANE`
- **THEN** the macro sends the existing active-lane command (`FL:` or `UM:`)

### Requirement: Toolchange macro with derived gear retract
`_FLARE_CHANGE_LANE` SHALL execute the full toolchange sequence: tip forming
with an ignore-buffer FLARE `MV:` retract → gear retract (derived) →
nonblocking `TC:` → toolhead-sensor-gated PICKUP → load hotend.
Gear retract distance SHALL be computed as
`dist_filament_park + dist_sensor_to_extruder + 5` with no separate
variable. Gear retract speed SHALL use `_FLARE_VARS.speed_hub_to_extruder`
converted to Klipper feedrate (`* 60`).

#### Scenario: Full toolchange sequence completes
- **WHEN** `_FLARE_CHANGE_LANE LANE=2` is called during a print
- **THEN** tip forming runs a derived `MV:-...:I` old-lane retract before the
  final park move, gear retract clears the sensor, `TC:2` is started, and the
  delayed toolhead-sensor gate waits 2 seconds after filament is detected again
  before performing Stage 1 `MV:{dist_sensor_to_synced_move}:...:I`, Stage 2
  `G1 E{load_park_dist}`, and `_FLARE_LOAD_HOTEND`
- **AND** `load_park_dist` is derived as
  `(dist_filament_park + dist_sensor_to_synced_move) * 1.1`

#### Scenario: Stage 1 pickup follows sensor-to-synced distance
- **WHEN** `variable_dist_sensor_to_synced_move` is tuned
- **THEN** Stage 1 uses that exact distance for the MMU-only approach move
- **AND** Stage 2 includes that distance in `load_park_dist`

### Requirement: Boot delayed_gcode sets RELOAD_MODE transiently
`[delayed_gcode _FLARE_BOOT]` SHALL send `SET:RELOAD_MODE:{enable_reload}`
to FLARE on every Klipper start without `SV:`, so FLARE reverts to
persisted flash default when running standalone.

#### Scenario: RELOAD_MODE applied on Klipper start
- **WHEN** Klipper starts with `flare_mmu.cfg` included
- **THEN** `_FLARE_BOOT` fires after 2 seconds, sends
  `SET:RELOAD_MODE:{enable_reload}`, logs the value

#### Scenario: Standalone FLARE revert
- **WHEN** FLARE reboots without Klipper running
- **THEN** RELOAD_MODE reverts to the value last persisted in flash
  (unaffected by the Klipper-side variable)

### Requirement: Tip forming test macro
`FLARE_TEST_TIP_FORMING` SHALL allow manual tip quality testing without
a full toolchange by loading the hotend, simulating a print pause, running
tip forming, and retracting for inspection. It SHALL accept SP-style
tip-forming override parameters and write them into
`_FLARE_TIP_FORMING_DEFAULTS` before running `_FLARE_TIP_FORMING`.

#### Scenario: Test executes when hotend is hot
- **WHEN** `FLARE_TEST_TIP_FORMING` is called with hotend above
  `min_extrude_temp`
- **THEN** sequence runs: load hotend → print simulation → tip forming →
  staged extruder unload → respond to inspect tip

#### Scenario: Test macro supports SP-style tuning overrides
- **WHEN** `FLARE_TEST_TIP_FORMING DIP_MELT_GAP=2 PARK_SPEED=130` is called
- **THEN** the corresponding `_FLARE_TIP_FORMING_DEFAULTS` variables are
  updated before `_FLARE_TIP_FORMING` runs

#### Scenario: Test aborts when hotend is cold
- **WHEN** `FLARE_TEST_TIP_FORMING` is called with hotend below
  `min_extrude_temp`
- **THEN** macro responds with an error message and does not move

### Requirement: Removed development macros
`FLARE_CUT`, `FLARE_CUT_BARE`, and `FLARE_CUT_TEST` SHALL NOT be present in
`flare_mmu.cfg`. The cutter cycle is driven by the firmware toolchange (`TC:`),
so no standalone Klipper cut macro is needed.

#### Scenario: Development macros absent
- **WHEN** `flare_mmu.cfg` is loaded
- **THEN** calling `FLARE_CUT`, `FLARE_CUT_BARE`, or `FLARE_CUT_TEST` results in
  a Klipper "unknown command" error

### Requirement: Preload macro routes selected lanes to the gate
`FLARE_PRELOAD` SHALL advance a selected lane to its gate (OUT) without loading
the toolhead. With `LANE=1` or `LANE=2` it SHALL send `T:{lane}` before `LO:`;
with `LANE=0` (or no `LANE`) it SHALL send `LO:` for the active lane; any other
`LANE` SHALL be rejected with an error and no command.

#### Scenario: Selected lane preload
- **WHEN** `FLARE_PRELOAD LANE=2` is invoked
- **THEN** the macro sends `T:2` then `LO:`

#### Scenario: Active-lane preload
- **WHEN** `FLARE_PRELOAD` is invoked with `LANE=0` or no `LANE`
- **THEN** the macro sends `LO:` for the active lane

#### Scenario: Invalid lane rejected
- **WHEN** `FLARE_PRELOAD LANE=5` is invoked
- **THEN** the macro responds with an error and sends no command

