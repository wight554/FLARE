## ADDED Requirements

### Requirement: Manual unload supports explicit standby lane eject

Manual MMU unload SHALL accept `UM`, `UM:`, `UM:1`, and `UM:2`. `UM` and
`UM:` SHALL preserve the existing active-lane behavior. `UM:n` SHALL target
the explicit lane without changing `active_lane`.

When the explicit target lane is inactive, the command SHALL act only as a
standby-preload eject: the target lane must be idle, must have filament at
`IN`, and must not have filament at `OUT`. It SHALL unload until `IN` clears
without clearing active toolhead filament state, disabling active-lane sync,
changing active lane selection, inspecting shared Y-splitter occupancy, or
starting any cutter phase.

#### Scenario: Active-lane manual unload remains unchanged

- **WHEN** `UM`, `UM:`, or `UM:n` where `n` is the active lane is commanded
- **THEN** the active-lane manual MMU unload behavior is unchanged
- **AND** the unload sequence does not start the cutter phase

#### Scenario: Inactive standby lane eject preserves active print state

- **WHEN** lane 1 is active and idle-or-printing state belongs to lane 1
- **AND** lane 2 is idle with `IN=1` and `OUT=0`
- **AND** `UM:2` is commanded
- **THEN** lane 2 unloads until `IN` clears
- **AND** `active_lane`, active sync state, and toolhead filament state are
  preserved

#### Scenario: Unsafe inactive target is rejected

- **WHEN** `UM:n` targets an inactive lane that is busy, lacks filament at
  `IN`, or has filament at `OUT`
- **THEN** the command returns an error and no motion starts

#### Scenario: Invalid lane payload is rejected

- **WHEN** `UM:<payload>` is commanded with a non-empty payload other than
  `1` or `2`
- **THEN** the command returns `ER:ARG`
