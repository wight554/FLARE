# Delta: serial-port-ownership

## ADDED Requirements

### Requirement: Exclusive serial opens

Host tools MUST open the FLARE CDC port with `exclusive=True` passed to
`serial.Serial(...)` — applies to `flare_daemon.py`, `flare_cmd.py`,
`flare_live_tuner.py`, `flare_sync_check.py`, `flare_baseline_recommender.py`.
Linux CDC-ACM permits multi-open by default; concurrent readers steal each
other's replies and corrupt both consumers.

#### Scenario: Second opener while daemon holds port

- **WHEN** the daemon owns the port and a direct tool attempts to open it
- **THEN** the open fails immediately with a clear locked-port error
- **AND** the tool reports the conflict instead of silently misparsing replies

### Requirement: Daemon proxy preferred over direct serial

When the daemon is reachable on its API port, `flare_cmd.py` MUST route
commands through the daemon proxy rather than opening the serial port
directly. Direct serial is the fallback for daemon-less setups.

#### Scenario: Daemon running

- **WHEN** `flare_cmd.py` runs and `GET /status` on the daemon succeeds
- **THEN** commands are sent via `POST /cmd`
- **AND** no direct serial open is attempted
