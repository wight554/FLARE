# daemon-klipper-mirror Specification

## Purpose
Contract for the `flare_daemon.py` -> Klipper `SET_MMU` mirror push: delta pushes of
only changed fields (cmd_SET_MMU keeps absent params), full pushes for restart recovery,
and gate-state diagnostics — keeping the mock in sync without spamming the full payload.
## Requirements
### Requirement: Delta SET_MMU mirror push

The daemon SHALL push only the `SET_MMU` fields whose formatted value changed since the
last successful push, relying on `cmd_SET_MMU` keeping the current value for any absent
param. The resulting Klipper mock state SHALL be identical to a full push.

#### Scenario: Only the piston moves during a print

- **WHEN** sync feedback / buffer state change but structural fields do not
- **THEN** the emitted `SET_MMU` carries only the changed fields, not the full set

#### Scenario: No field changed

- **WHEN** no mirrored field changed since the last push
- **THEN** no `SET_MMU` is emitted that tick

### Requirement: Full resync recovery

The daemon SHALL emit a full `SET_MMU` (all fields) on the first push and on a
board-online transition. On the periodic resync tick the daemon SHALL read the Klipper
`mmu` object and emit a full `SET_MMU` only when the reported mock state diverges from the
desired field set; when they match the daemon SHALL emit nothing that tick. A restarted
Klipper/Moonraker SHALL recover complete state within one tick of the divergence becoming
observable.

#### Scenario: Klipper restarts mid-print

- **WHEN** the resync tick reads the `mmu` object and finds it diverged (or absent) from
  the desired field set, or the board comes online
- **THEN** the next push contains the full field set

#### Scenario: Idle resync tick with mock in sync

- **WHEN** the resync tick reads the `mmu` object and every mirrored field already matches
  the desired value
- **THEN** no `SET_MMU` is emitted that tick

### Requirement: Gate-state diagnostics

The daemon SHALL, when `FLARE_GATE_DEBUG` is set, log the gate-relevant mirror inputs
(`active_gate`, per-lane IN/OUT, computed gate status, toolchange state) when they change.
When the flag is unset there SHALL be no behavior or output change.

#### Scenario: Reproducing the gate-dot transient

- **WHEN** `FLARE_GATE_DEBUG` is set and a load runs
- **THEN** each gate-field change is logged with a timestamp for root-cause analysis

#### Scenario: Flag unset

- **WHEN** `FLARE_GATE_DEBUG` is not set
- **THEN** the daemon emits no diagnostic log and behaves exactly as before

