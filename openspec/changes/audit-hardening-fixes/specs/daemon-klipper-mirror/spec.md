# Delta: daemon-klipper-mirror

## ADDED Requirements

### Requirement: API binds loopback by default

The daemon HTTP/SSE API SHALL bind `127.0.0.1` unless the operator passes an
explicit non-default `--host`. The API forwards raw firmware commands (motion,
cutter, settings, bootloader), so non-local exposure MUST be a deliberate
opt-in, never the default.

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
