# Daemon State DB Specification

## Purpose
Durable contract for the FLARE daemon SQLite-backed persistent state store
covering gate configuration and MMU statistics.

## Requirements

### Requirement: Single Database Owner
The daemon (`scripts/flare_daemon.py`) MUST be the sole process that opens or
writes the SQLite database. No other process (mmu.py, scripts, etc.) may open
the database file directly.

#### Scenario: Concurrent access attempt
- **GIVEN** the daemon is running
- **WHEN** any other process attempts to read state
- **THEN** it MUST do so via the daemon HTTP API, not by opening the file

### Requirement: Database Location
The database MUST be located at `~/printer_data/database/flare.db`.
The daemon MUST create the directory on first run if it does not exist.

#### Scenario: Fresh install
- **WHEN** the daemon starts and `~/printer_data/database/` does not exist
- **THEN** the directory is created and `flare.db` is initialized with empty tables

### Requirement: Schema
The database MUST contain two tables:

```sql
CREATE TABLE IF NOT EXISTS gate_config (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS stats (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

### Requirement: Config HTTP API
The daemon MUST expose two HTTP endpoints for gate config access:

#### Scenario: Read config
- **WHEN** `GET /config` is received
- **THEN** daemon returns HTTP 200 with JSON object containing all gate config keys

#### Scenario: Write config
- **WHEN** `POST /config` is received with a JSON body
- **THEN** daemon persists each key from the body to `gate_config` table
- **AND** returns HTTP 200 with `{"ok": true}`

### Requirement: Stats Persistence
MMU stats (swaps_total, swaps_success, swaps_failed, loads_success,
unloads_success, last_error) MUST be persisted to SQLite and survive
daemon restarts.

#### Scenario: Daemon restart
- **WHEN** the daemon restarts
- **THEN** all stats counters are loaded from SQLite
- **AND** continue incrementing from the last persisted value

### Requirement: JSON File Retirement
`flare_mmu_vars.json` and `flare_mmu_stats.json` MUST NOT be written by any
component after this change is deployed. The lookup/fallback chain for these
files MUST be removed.
