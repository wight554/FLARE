## ADDED Requirements

### Requirement: Slicer-driven per-toolchange purge
`flare_mmu.cfg` SHALL let the slicer set the next toolchange's purge length.
`_FLARE_TC_STATE` SHALL hold a `next_purge` runtime variable (mm, default `-1`
meaning unset). `_FLARE_SET_PURGE PURGE=<mm>` SHALL store its argument, clamped
to `>= 0`, into `next_purge`. An explicit `PURGE=0` SHALL be honored as "no
purge for this change" (the slicer emits `0` when no flush is needed) and SHALL
NOT fall back to `_FLARE_VARS.purge_len`; the static `purge_len` fallback applies
ONLY to manual loads, where `next_purge` is left unset (`-1`). The argument
SHALL be a filament length in mm: OrcaSlicer's `[flush_length]` placeholder (the
full flush length, `purge_volume / filament_area`) is passed directly, with no
multiplier. `[first_flush_volume]` is half of `flush_length` and SHALL be
doubled slicer-side (`{first_flush_volume * 2}`) if used instead.

The value SHALL be consumed exactly once: `_FLARE_LOAD_HOTEND` resets
`next_purge` to `-1` after using it, and the no-load toolchange paths SHALL also
reset it so a slicer-set value cannot leak into a later load.

#### Scenario: Slicer sets next purge length
- **WHEN** the slicer Change-Filament G-code runs `_FLARE_SET_PURGE PURGE=107.7`
  before the tool change
- **THEN** `_FLARE_TC_STATE.next_purge` is set to `107.7`
- **AND** the following `_FLARE_LOAD_HOTEND` purges that amount and resets
  `next_purge` to `-1`

#### Scenario: Explicit zero purge is honored
- **WHEN** `_FLARE_SET_PURGE PURGE=0` is called (slicer emits `0` when no flush
  is needed) and the following `_FLARE_LOAD_HOTEND` runs
- **THEN** `next_purge` is set to `0` and no purge extrusion is performed

#### Scenario: Unset default uses static purge_len
- **WHEN** no `_FLARE_SET_PURGE` has run since the last load
- **THEN** `_FLARE_LOAD_HOTEND` uses `_FLARE_VARS.purge_len`

#### Scenario: Skipped change drops slicer purge
- **WHEN** `_FLARE_SET_PURGE` set `next_purge` but `_FLARE_CHANGE_LANE` finds the
  target lane already loaded and skips the toolchange
- **THEN** `next_purge` is reset to `-1` so the skipped value is not consumed by
  a later load

#### Scenario: Print boundary clears stale purge
- **WHEN** `_FLARE_SYNC_TOOLHEAD` runs at PRINT_START, PRINT_END, or on
  CANCEL/error
- **THEN** `next_purge` is reset to `-1`, so a manual swap or the next print's
  first load cannot fire a leftover purge amount

## MODIFIED Requirements

### Requirement: Variables block with SP-compatible distance names
`[gcode_macro _FLARE_VARS]` SHALL expose all user-configurable distances
using the same names as the LH-Stinger Pico MMU wiki
(`dist_sensor_to_extruder`, `dist_filament_park`,
`dist_extruder_to_meltzone`).

#### Scenario: Distance variable names match SP wiki
- **WHEN** a user measures distances following the SP toolhead distance
  calibration guide
- **THEN** they can set `variable_dist_sensor_to_extruder`,
  `variable_dist_filament_park`, and `variable_dist_extruder_to_meltzone` in
  `_FLARE_VARS` with no name translation required

#### Scenario: dist_filament_park constraint documented
- **WHEN** `dist_filament_park` is set to a value ≥ `dist_extruder_to_meltzone`
- **THEN** a comment in the file warns that `dist_filament_park` MUST be
  less than `dist_extruder_to_meltzone`

#### Scenario: Purge speed is converted to Klipper feedrate
- **WHEN** `_FLARE_VARS.variable_purge_speed` is set to `30.0`
- **THEN** pickup and purge extrusion use Klipper feedrates derived as
  `purge_speed * 60` (30 mm/s rather than F30)
- **AND** the `_FLARE_LOAD_HOTEND` meltzone push uses half that
  (`(purge_speed / 2) * 60`), since the meltzone approach seats filament rather
  than flushing it

### Requirement: Load hotend macro with 3-stage meltzone approach
`_FLARE_LOAD_HOTEND` SHALL advance filament from the park position to the
meltzone in three stages (50% fast / 25% normal / 25% slow) at half the purge
speed, bracket the sequence with buffer-state resets, then purge the resolved
per-change amount. It SHALL send a FLARE `BS` buffer-state reset followed by a
settle dwell before the meltzone push and again after the purge step, so the
buffer begins and ends each load in a known state. The purge amount SHALL be
`_FLARE_TC_STATE.next_purge` when that runtime variable is set (`>= 0`),
otherwise `_FLARE_VARS.purge_len`; a set `next_purge` SHALL be consumed (reset
to `-1`) after use. `_FLARE_PURGE` SHALL be called only when the resolved amount
is greater than zero.

#### Scenario: Push distance matches SP formula
- **WHEN** `_FLARE_LOAD_HOTEND` is called after PICKUP
- **THEN** total push distance equals
  `dist_extruder_to_meltzone - dist_filament_park - tip_length_below_cut`
- **AND** the three push stages run at `(purge_speed / 2) * 60` derived feedrates

#### Scenario: Buffer reset brackets the load
- **WHEN** `_FLARE_LOAD_HOTEND` runs
- **THEN** it sends `BS` and dwells before the meltzone push
- **AND** sends `BS` and dwells again after the purge step

#### Scenario: Purge skipped when resolved amount is zero
- **WHEN** `next_purge` is unset (`-1`) and `variable_purge_len: 0`
- **THEN** no purge extrusion is performed after the meltzone approach

#### Scenario: Slicer purge consumed once
- **WHEN** `next_purge` is `107.7` (set by `_FLARE_SET_PURGE`)
- **THEN** `_FLARE_PURGE PURGE=107.7` runs and `next_purge` is reset to `-1`
- **AND** a subsequent load with no new `_FLARE_SET_PURGE` falls back to
  `purge_len`

### Requirement: Toolchange macro with derived gear retract
`_FLARE_CHANGE_LANE` SHALL execute the full toolchange sequence: tip forming
with a buffer-locked (`_FLARE_BL_RETRACT`) retract → derived gear retract (also
buffer-locked) → one `BS` to close the lock chain → nonblocking `TC:` →
toolhead-sensor-gated PICKUP → load hotend.
Gear retract distance SHALL be computed as
`dist_filament_park + dist_sensor_to_extruder + 5` with no separate
variable. Gear retract speed SHALL use `_FLARE_VARS.speed_hub_to_extruder`
converted to Klipper feedrate (`* 60`). In `_FLARE_POST_TC_LOAD`,
`load_park_dist` SHALL be `dist_filament_park + dist_sensor_to_synced_move`
(no safety factor), and the Stage 1 MMU-only `MV:` approach SHALL be disabled by
default — left in the file as a commented, re-enablable line — because the
delayed-TS:1 insert handler already seats the tip during FL.

#### Scenario: Full toolchange sequence completes
- **WHEN** `_FLARE_CHANGE_LANE LANE=2` is called during a print
- **THEN** tip forming runs a buffer-locked retract before the final park move,
  the derived gear retract (buffer-locked) clears the sensor, one `BS` closes the
  lock chain, `TC:2` is started, and the delayed toolhead-sensor gate waits 2
  seconds after filament is detected again before performing Stage 2
  `G1 E{load_park_dist}` and `_FLARE_LOAD_HOTEND`
- **AND** `load_park_dist` is derived as
  `dist_filament_park + dist_sensor_to_synced_move`
- **AND** the Stage 1 `MV:{dist_sensor_to_synced_move}:...` approach is present
  only as a commented-out line

#### Scenario: Stage 1 approach disabled by default
- **WHEN** `flare_mmu.cfg` is loaded unmodified
- **THEN** `_FLARE_POST_TC_LOAD` performs no MMU-only `MV:` approach before the
  Stage 2 extruder grab
- **AND** re-enabling requires uncommenting the documented Stage 1 line
