## ADDED Requirements

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

## MODIFIED Requirements

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

### Requirement: Removed development macros
`FLARE_CUT`, `FLARE_CUT_BARE`, and `FLARE_CUT_TEST` SHALL NOT be present in
`flare_mmu.cfg`. The cutter cycle is driven by the firmware toolchange (`TC:`),
so no standalone Klipper cut macro is needed.

#### Scenario: Development macros absent
- **WHEN** `flare_mmu.cfg` is loaded
- **THEN** calling `FLARE_CUT`, `FLARE_CUT_BARE`, or `FLARE_CUT_TEST` results in
  a Klipper "unknown command" error
