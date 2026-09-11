# Klipper Integration — Delta Spec (daemon-sqlite-store)

## MODIFIED Requirements

### Requirement: Gate Config Persistence
**Previous**: `mmu.py` reads and writes `flare_mmu_vars.json` directly via
`_load_vars()` / `_save_vars()` file I/O.

**Updated**: `mmu.py` MUST read and write gate config exclusively via the
daemon HTTP API.

#### Scenario: Klipper startup — daemon online
- **WHEN** `mmu.py` initializes and the daemon is reachable at `http://127.0.0.1:8088`
- **THEN** `_load_vars()` issues `GET /config` with 2s timeout
- **AND** applies returned values to in-memory gate state

#### Scenario: Klipper startup — daemon offline
- **WHEN** `mmu.py` initializes and the daemon is not reachable
- **THEN** `_load_vars()` logs a warning and continues with compiled-in defaults
- **AND** does NOT crash or block Klipper startup

#### Scenario: Gate config save
- **WHEN** `_save_vars()` is called
- **THEN** `mmu.py` issues `POST /config` with gate state as JSON body
- **AND** on timeout or connection error, logs a warning and continues

## REMOVED Requirements

### Requirement: JSON File Access
**Reason**: Retired in favour of daemon HTTP API (daemon-sqlite-store change).
All file path resolution logic (`_get_vars_path`, fallback chain) is removed.

**Migration**: Operators re-enter gate config via Fluidd widget or
`MMU_GATE_MAP` command after upgrading. Old `flare_mmu_vars.json` files can
be deleted.
