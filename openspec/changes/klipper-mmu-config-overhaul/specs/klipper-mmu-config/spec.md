## ADDED Requirements

### Requirement: Single-file Klipper MMU config
`scripts/flare_mmu.cfg` SHALL provide a complete Klipper MMU integration
that users can activate with a single `[include flare_mmu.cfg]` line in
`printer.cfg`, with no other macro files required.

#### Scenario: Single include activates all macros
- **WHEN** user adds `[include flare_mmu.cfg]` to `printer.cfg`
- **THEN** macros `T1`, `T2`, `FLARE_LOAD`, `FLARE_UNLOAD`, `FLARE_CUT`,
  `FLARE_TEST_TIP_FORMING`, `_FLARE_CHANGE_LANE`, `_FLARE_TIP_FORMING`,
  `_FLARE_LOAD_HOTEND` are all available without further configuration

### Requirement: Variables block with SP-compatible distance names
`[gcode_macro _FLARE_VARS]` SHALL expose all user-configurable distances
using the same names as the LH-Stinger Pico MMU wiki
(`dist_sensor_to_extruder`, `dist_filament_park`,
`dist_extruder_to_meltzone`) so users following that calibration guide
can copy measurements directly.

#### Scenario: Distance variable names match SP wiki
- **WHEN** a user measures distances following the SP toolhead distance
  calibration guide
- **THEN** they can set `variable_dist_sensor_to_extruder`,
  `variable_dist_filament_park`, `variable_dist_extruder_to_meltzone` in
  `_FLARE_VARS` with no name translation required

#### Scenario: dist_filament_park constraint documented
- **WHEN** `dist_filament_park` is set to a value ≥ `dist_extruder_to_meltzone`
- **THEN** a comment in the file warns that `dist_filament_park` MUST be
  less than `dist_extruder_to_meltzone`

### Requirement: Tip forming macro with cooldown and dip phases
`_FLARE_TIP_FORMING` SHALL implement the full tip forming sequence
(post-pause push, cooldown pull, optional secondary moves, optional dip,
final fast retract to park position) reading parameters from
`_FLARE_TIP_FORMING_DEFAULTS`.

#### Scenario: Default tip forming executes without error
- **WHEN** `_FLARE_TIP_FORMING` is called with hotend at temperature
- **THEN** macro executes cooldown and park moves, leaves filament at
  `dist_filament_park` from meltzone

#### Scenario: Dip phase skipped when dip_melt_gap is zero
- **WHEN** `variable_dip_melt_gap: 0.0` in `_FLARE_TIP_FORMING_DEFAULTS`
- **THEN** dip moves are not executed

#### Scenario: Secondary cooldown moves skipped when disabled
- **WHEN** `variable_cooldown_secondary_moves: 0`
- **THEN** only the single long pull is executed in the cooldown phase

### Requirement: Load hotend macro with 3-stage meltzone approach
`_FLARE_LOAD_HOTEND` SHALL advance filament from the park position to the
meltzone in three stages (50% fast / 25% normal / 25% slow) and
optionally purge.

#### Scenario: Push distance matches SP formula
- **WHEN** `_FLARE_LOAD_HOTEND` is called after PICKUP
- **THEN** total push distance equals
  `dist_extruder_to_meltzone - dist_filament_park - tip_length_below_cut`

#### Scenario: Purge skipped when purge_len is zero
- **WHEN** `variable_purge_len: 0`
- **THEN** no purge extrusion is performed after meltzone approach

### Requirement: Toolchange macro with derived gear retract
`_FLARE_CHANGE_LANE` SHALL execute the full toolchange sequence: HD:1 →
tip forming → HD:0 → gear retract (derived) → TC: → PICKUP → load hotend.
Gear retract distance SHALL be computed as
`dist_filament_park + dist_sensor_to_extruder + 5` with no separate
variable.

#### Scenario: Full toolchange sequence completes
- **WHEN** `_FLARE_CHANGE_LANE LANE=2` is called during a print
- **THEN** tip forming executes inside HD:1/HD:0, gear retract clears the
  sensor, `TC:2` completes, PICKUP feeds `dist_sensor_to_extruder * 1.2`,
  and `_FLARE_LOAD_HOTEND` primes the meltzone

#### Scenario: PICKUP is zero when sensor_to_extruder is zero
- **WHEN** `variable_dist_sensor_to_extruder: 0` (TS_BUF_MS fallback)
- **THEN** PICKUP move distance is 0 (no extrusion)

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
tip forming, and retracting for inspection — no MMU motor moves required.

#### Scenario: Test executes when hotend is hot
- **WHEN** `FLARE_TEST_TIP_FORMING` is called with hotend above
  `min_extrude_temp`
- **THEN** sequence runs: load hotend → print simulation → tip forming →
  retract to clear gears → respond to inspect tip

#### Scenario: Test aborts when hotend is cold
- **WHEN** `FLARE_TEST_TIP_FORMING` is called with hotend below
  `min_extrude_temp`
- **THEN** macro responds with an error message and does not move

### Requirement: Removed development macros
`FLARE_PRELOAD`, `FLARE_CUT_BARE`, and `FLARE_CUT_TEST` SHALL NOT be
present in `flare_mmu.cfg`.

#### Scenario: Development macros absent
- **WHEN** `flare_mmu.cfg` is loaded
- **THEN** calling `FLARE_PRELOAD`, `FLARE_CUT_BARE`, or `FLARE_CUT_TEST`
  results in a Klipper "unknown command" error
