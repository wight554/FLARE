## ADDED Requirements

### Requirement: Discrete UI-meaningful mirror field set
The daemon SHALL mirror only discrete, UI-meaningful state to the Klipper mock,
not continuous cosmetic values. The mirrored set SHALL include `gate_status`,
the filament checkpoint position (`filament_pos`), the buffer state
(`sync_feedback_state`: compression / tension / neutral), `action`,
`print_state`, the filament-path sensor flags, `active_gate`, `tool`, and
`num_toolchanges`. The daemon SHALL NOT mirror continuous analog values: the
`sync_feedback` float (cosmetic buffer-piston offset), `sps`, `feed_rate`,
`rev_rate`, nor any synthesized "Filament: X mm" distance readout.

#### Scenario: Buffer state mirrored, piston offset dropped
- **WHEN** the buffer moves under sync so the analog offset changes but its
  discrete state (compression/tension/neutral) does not
- **THEN** no `SET_MMU` is emitted for the offset change
- **AND** when the discrete buffer state transitions, a `SET_MMU` carries the new
  `sync_feedback_state`

#### Scenario: No synthetic filament distance
- **WHEN** a load or unload runs
- **THEN** the daemon emits no synthesized filament-distance (mm) field

### Requirement: Event-driven mirror push
The daemon SHALL push `SET_MMU` when a tracked discrete field changes, rather
than on a fixed-rate tick. A periodic tick MAY remain solely to carry the
existing full-resync recovery and host-busy reconcile; it SHALL NOT emit a
`SET_MMU` when no tracked discrete field has changed and the mock is in sync. In
steady-state printing with no discrete state change, the daemon SHALL emit no
mirror traffic.

#### Scenario: Steady print emits no mirror traffic
- **WHEN** a print is mid-feature with no discrete tracked-field change (only the
  analog buffer offset is moving)
- **THEN** the daemon emits no `SET_MMU`

#### Scenario: Discrete transition pushes immediately
- **WHEN** a tracked discrete field changes (e.g. a checkpoint advance or a
  buffer-state transition)
- **THEN** the daemon emits a `SET_MMU` carrying that change without waiting for a
  fixed tick boundary

### Requirement: A swap counts an unload
The daemon SHALL count an unload on each successful swap (`TC:DONE`) so the
loads/unloads totals stay symmetric. The firmware toolchange emits no standalone
`UNLOADED` event for its internal unload phase (only the trailing `LOADED`), so a
swap is recorded as unload(old) + load(new), matching Happy-Hare. Standalone
`UNLOADED` / `LOADED` events SHALL continue to count manual unloads / loads.

#### Scenario: Swap increments both load and unload
- **WHEN** a `TC:DONE` event is recorded for a successful swap
- **THEN** `swaps_success`, `loads_success` (via the swap's `LOADED`), and
  `unloads_success` each advance by one
- **AND** `loads_success` and `unloads_success` track each other across a print
  of swaps (not loads far exceeding unloads)

## MODIFIED Requirements

### Requirement: Delta SET_MMU mirror push

The daemon SHALL push only the `SET_MMU` fields whose formatted value changed since the
last successful push, relying on `cmd_SET_MMU` keeping the current value for any absent
param. The resulting Klipper mock state SHALL be identical to a full push. The
delta SHALL be computed over the discrete UI-meaningful field set only (no
continuous piston offset or synthetic distance).

#### Scenario: Only discrete state changes during a print

- **WHEN** a discrete tracked field changes (gate status, checkpoint, buffer
  state) but the rest do not
- **THEN** the emitted `SET_MMU` carries only the changed fields, not the full set

#### Scenario: No field changed

- **WHEN** no mirrored field changed since the last push
- **THEN** no `SET_MMU` is emitted that tick
