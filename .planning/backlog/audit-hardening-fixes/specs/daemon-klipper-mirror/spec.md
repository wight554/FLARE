# Delta: daemon-klipper-mirror

## ADDED Requirements

### Requirement: API binds loopback by default — SUPERSEDED, do not fold into the live spec

The daemon HTTP/SSE API SHALL bind `127.0.0.1` unless the operator passes an explicit non-default `--host`. The API forwards raw firmware commands (motion, cutter, settings, bootloader), so non-local exposure MUST be a deliberate opt-in, never the default.

STATUS (2026-07-28): this requirement shipped, then was reverted (commit
`4ae1a19`, 2026-06-19) because the loopback default broke the primary
real-world use case (LAN-reachable WebUI dashboard) and, worse, an
operator's manual `0.0.0.0` override did not survive a systemd
reinstall — the regenerated unit template had no `--host` arg at all, so
the "safe" default provided no durable protection either way. Left above
verbatim for audit-trail purposes only; see `tasks.md` task 4.1 for the
full account and what was fixed instead (preserving manual ExecStart
customizations, e.g. a deliberate `--host 127.0.0.1`, across a reinstall
— `install_daemon.py`'s `step_systemd_unit()` — without touching the
default). The daemon's actual current behavior is the inverse of the
requirement text above: it binds `0.0.0.0` by default.

#### Scenario: Default launch

- **WHEN** `flare_daemon.py` starts without `--host`
- **THEN** the API listens on `127.0.0.1` only
- **AND** LAN hosts cannot reach `/cmd`

#### Scenario: Explicit LAN bind

- **WHEN** operator passes `--host 0.0.0.0`
- **THEN** daemon binds as requested and logs a clear exposure warning at startup

### Requirement: Event-type split covers all two-part event families

The daemon's `EV:` line parser SHALL classify every firmware two-part event
family (`TC`, `CUT`, `FAULT`, `BL`, `BUF_STAB`, `SYNC`, `RELOAD`) as
`FAMILY:SUBTYPE` with remaining segments as data. Downstream event lines
reconstructed for clients MUST use colon separators matching the firmware wire
format.

#### Scenario: RELOAD completion event

- **WHEN** firmware emits `EV:RELOAD:LOADED:1`
- **THEN** daemon classifies type `RELOAD:LOADED`, data `1`
- **AND** `flare_cmd RL` completion-wait matches it and exits 0

### Requirement: Full resync includes gate-map fields

The daemon's force-full `SET_MMU` push SHALL include the persisted gate-map
fields (`GATE_COLOR`, `GATE_MATERIAL`, `GATE_SPOOL_ID`, gate names) in addition
to live state, so a Klipper restart recovers the complete mock state without
relying on the mock's one-shot `/config` pull at klippy init (which races
daemon startup ordering).

#### Scenario: Klipper restarts while daemon is up

- **WHEN** klippy restarts and the next force-full push fires
- **THEN** gate colors, materials, and spool ids reappear in Mainsail/Fluidd
  without operator action

#### Scenario: Daemon starts after Klipper

- **WHEN** the mock's init-time `/config` pull failed (daemon not yet up)
- **THEN** the first force-full push after daemon start restores the gate map
