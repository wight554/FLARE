## ADDED Requirements

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
