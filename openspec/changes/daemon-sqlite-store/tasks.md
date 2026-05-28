## 1. Daemon: SQLite Init

- [x] 1.1 Add `db_init()` to `scripts/flare_daemon.py`: open `~/printer_data/database/flare.db` (create dir if absent), create `gate_config` and `stats` tables with `CREATE TABLE IF NOT EXISTS`.
- [x] 1.2 Call `db_init()` at daemon startup before any other state load.
- [x] 1.3 Add `db_get(table, key, default=None)` and `db_set(table, key, value)` helpers using `sqlite3` with JSON serialization for values.

## 2. Daemon: Stats Migration

- [x] 2.1 Replace `load_mmu_stats()` / `save_mmu_stats()` JSON file I/O with SQLite reads/writes via `db_get` / `db_set` on the `stats` table.
- [x] 2.2 Remove `flare_mmu_stats.json` path resolution (`_get_stats_path`, fallback chain) and all references.
- [x] 2.3 Verify stats persist across daemon restart: increment a counter, kill/restart daemon, confirm counter resumes from persisted value.

## 3. Daemon: Config HTTP API

- [x] 3.1 Add `GET /config` handler: reads all rows from `gate_config` table, returns JSON object `{key: value, ...}`.
- [x] 3.2 Add `POST /config` handler: accepts JSON body, writes each key to `gate_config` table via `db_set`, returns `{"ok": true}`.
- [x] 3.3 Add `gate_config` read/write helpers to daemon's own internal gate-map sync path (replace `flare_mmu_vars.json` reads/writes).
- [x] 3.4 Remove `flare_mmu_vars.json` path resolution (`_get_vars_path`, fallback chain) from `scripts/flare_daemon.py` and all references.

## 4. klipper/mmu.py: HTTP API Integration

- [x] 4.1 Replace `_load_vars()` file I/O with `GET http://127.0.0.1:8088/config` (2s timeout); on error log warning and use defaults.
- [x] 4.2 Replace `_save_vars()` file I/O with `POST http://127.0.0.1:8088/config` (2s timeout); on error log warning and continue.
- [x] 4.3 Remove `_get_vars_path()` and the full path resolution/fallback chain from `klipper/mmu.py`.
- [x] 4.4 Remove `import json` / `import os` usages that were exclusively for the file path; keep any remaining uses.

## 5. Validation

- [x] 5.1 `python3 -m py_compile scripts/flare_daemon.py klipper/mmu.py` — both compile clean.
- [x] 5.2 Start daemon, call `GET /config` via curl — returns `{}` on fresh DB.
- [x] 5.3 `POST /config` with a gate map, then `GET /config` — returns the posted values.
- [x] 5.4 Kill and restart daemon, `GET /config` — persisted values survive restart.
- [x] 5.5 Confirm `~/printer_data/database/flare.db` exists and contains `gate_config` + `stats` tables.
- [x] 5.6 Confirm neither `flare_mmu_vars.json` nor `flare_mmu_stats.json` is written anywhere.
