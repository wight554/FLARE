## ADDED Requirements

### Requirement: Automatic Bypass Toolhead Loading on Sensor Insert
When the printer is in bypass mode and filament is manually inserted into the extruder entrance, Klipper macros SHALL automatically trigger the toolhead filament load sequence upon toolhead sensor trigger (insert edge).

#### Scenario: Auto-load on insert
- **WHEN** the toolhead sensor transitions to `detected` (insert edge)
- **AND** `printer.mmu.bypass` is `True`
- **THEN** macro `_FLARE_ON_TOOLHEAD_INSERT` automatically executes `MMU_LOAD` to grab and push filament to the meltzone

### Requirement: Slicer Toolchange Bypass Handling
The `MMU_CHANGE_TOOL` command SHALL gracefully accept and handle bypass sentinel values `-2` for tool or gate transitions, allowing seamless slicer-generated or UI-driven toolchanges to the bypass gate.

#### Scenario: Slicer toolchange to bypass
- **WHEN** G-code command `MMU_CHANGE_TOOL GATE=-2` or `MMU_CHANGE_TOOL TOOL=-2` is received
- **THEN** Klipper extra invokes `self._select_bypass` to set the bypass state
- **AND** returns successfully without raising any G-code validation or index errors
