## ADDED Requirements

### Requirement: Gate indicator reflects filament past the gate
`mmu.py` `get_status` SHALL report the UI "Gate" indicator (`sensors.mmu_gate`)
as "filament at or past the gate", not as the raw hub/`y_split` sensor. FLARE's
hub sensor is transit-only — it clears once filament settles past the Y junction
— so a loaded lane otherwise leaves the Gate dot unchecked while the steady
Toolhead sensor stays checked. `mmu_gate` SHALL be set whenever the active lane
is loaded, any downstream sensor shows presence, or filament is at the toolhead,
and SHALL clear when the lane is unloaded.

#### Scenario: Gate stays checked while loaded
- **WHEN** a lane is loaded to the toolhead and filament has settled past the
  transit-only hub sensor
- **THEN** the UI Gate indicator stays checked alongside the Toolhead indicator
- **AND** it clears after the lane is unloaded

### Requirement: Load/unload checkpoint latch
`mmu.py` SHALL report the filament checkpoint position (`filament_pos`) as
monotonic progress through the active load/unload phase, not solely from
instantaneous sensor reads, so a fast sensor transition cannot skip a checkpoint
in the UI. During a load, once the gate has been passed (gear/gate sensor seen,
or implied by the load phase) the Gate checkpoint SHALL remain set through to the
Toolhead checkpoint; it SHALL NOT revert while the filament is at or past the
toolhead. An unload SHALL step the checkpoints back down in reverse order, and a
completed unload SHALL clear them to unloaded.

`get_status` SHALL NOT compute a synthesized filament distance (mm) and SHALL NOT
read the removed `bowden_length` / `extruder_to_nozzle` config variables.

#### Scenario: Gate checkpoint survives a fast load
- **WHEN** a load advances filament from the gate to the toolhead faster than one
  mirror sample interval
- **THEN** the UI shows both the Gate and Toolhead checkpoints set (the Gate
  checkpoint is not skipped or left unchecked)

#### Scenario: Unload steps checkpoints down
- **WHEN** an unload runs to completion
- **THEN** the Toolhead checkpoint clears, then the Gate checkpoint clears, ending
  at the unloaded position

#### Scenario: No synthetic distance computed
- **WHEN** `get_status` runs
- **THEN** it produces no synthesized filament-mm value and does not read
  `bowden_length` or `extruder_to_nozzle`
