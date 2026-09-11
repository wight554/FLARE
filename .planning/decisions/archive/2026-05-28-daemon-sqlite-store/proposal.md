## Why

`flare_mmu_vars.json` is written by both `flare_daemon.py` and `klipper/mmu.py`, creating a shared-file race condition with no transaction guarantees. The file also lives in `~/printer_data/config/` — a config directory, not a data directory — making it invisible to backup/restore tooling and confusing to operators.

## What Changes

- Replace `flare_mmu_vars.json` with a SQLite database at `~/printer_data/database/flare.db` (follows Moonraker convention)
- Daemon becomes the **sole owner** of the database; all reads and writes go through the daemon
- `klipper/mmu.py` reads and writes gate config via the daemon HTTP API (`GET /config`, `POST /config`) instead of touching the file directly
- MMU stats tracking moves from in-memory (daemon) to SQLite (persistent across restarts)
- `flare_mmu_vars.json` is fully retired; the lookup/fallback chain is removed
- `mmu.py` without daemon running returns graceful defaults (no crash); daemon is a hard runtime dependency

## Capabilities

### New Capabilities
- `daemon-state-db`: SQLite-backed persistent store for gate config (status, spool_id, color, material) and MMU stats (swap counts, load/unload counts, last error); daemon is sole writer; HTTP API exposes read/write surface to Klipper and future consumers

### Modified Capabilities
- `klipper-integration`: `mmu.py` no longer reads or writes any local file; all state access goes through daemon HTTP API; daemon is now a hard runtime dependency for mmu.py to function

## Impact

- `scripts/flare_daemon.py`: add SQLite schema init, migrate reads/writes from JSON to SQLite, add `GET /config` and `POST /config` HTTP endpoints
- `klipper/mmu.py`: replace `_load_vars` / `_save_vars` file I/O with HTTP calls to daemon; handle daemon-offline gracefully (log warning, use defaults)
- `~/printer_data/config/flare_mmu_vars.json`: retired (can be deleted after migration)
- No firmware changes required
- No `config.ini` / `tune.h` changes required
