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

### Requirement: Host-busy backpressure on gcode/script pushes

The daemon SHALL NOT queue mirror traffic behind a long-running blocking Klipper command.
When a gcode/script push (`SET_MMU`, `MMU_GATE_MAP`, or `_FLARE_SYNC_BOARD`) fails because
the Klipper gcode lock is busy, the daemon SHALL enter a host-busy state, suppress all
further gcode/script pushes, and poll a lock-free `objects/query` for `idle_timeout` until
`idle_timeout.state` reports Idle/Ready before resuming pushes. The daemon SHALL
distinguish host-busy (gcode lock held, host reachable) from Moonraker-offline; the
offline path retains its existing backoff and SHALL NOT be replaced by the busy path.

On resume the daemon SHALL reconcile via the existing full-resync recovery path so the
Klipper mock returns to the desired field set regardless of changes missed while busy.

#### Scenario: Blocking command holds the gcode lock

- **WHEN** a `SET_MMU` push times out while a blocking command (e.g. `MPC_CALIBRATE`) holds
  the gcode lock
- **THEN** the daemon enters host-busy state and emits no further gcode/script pushes
- **AND** queued requests stop accumulating in the Klipper gcode queue

#### Scenario: Lock-free probe while busy

- **WHEN** the daemon is host-busy
- **THEN** it polls `objects/query {idle_timeout}` (which does not take the gcode lock) and
  emits no gcode/script
- **AND** when `idle_timeout.state` returns to Idle/Ready the daemon resumes pushes

#### Scenario: Resume reconciles missed state

- **WHEN** the daemon resumes after a host-busy period during which mirrored fields changed
- **THEN** the next push restores the full desired field set via the full-resync path

#### Scenario: Moonraker offline is not host-busy

- **WHEN** a push fails because Moonraker is unreachable
- **THEN** the daemon uses its existing offline backoff, not the host-busy probe path

