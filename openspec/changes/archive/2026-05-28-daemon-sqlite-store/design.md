## Context

Two JSON files currently persist FLARE host state:

- `flare_mmu_vars.json` — gate config (color, material, spool IDs, names, ttg_map); written by **both** `klipper/mmu.py` and `scripts/flare_daemon.py`
- `flare_mmu_stats.json` — swap/load/unload counters; written by **daemon only**

Both use a fragile lookup chain (`~/printer_data/config/` → `~/` → `/tmp/`) with no locking, no transactions, and no schema versioning. The config-folder location (`~/printer_data/config/`) confuses the data/config boundary and is invisible to Moonraker's database tooling.

## Goals / Non-Goals

**Goals:**
- Single SQLite database at `~/printer_data/database/flare.db` (follows Moonraker convention)
- Daemon is sole writer; `mmu.py` reads/writes via daemon HTTP API
- MMU stats persist across daemon and Klipper restarts
- No external Python dependencies (stdlib `sqlite3`)
- Graceful degradation: `mmu.py` works with defaults when daemon is offline

**Non-Goals:**
- Multi-node / remote database
- Firmware changes
- config.ini / tune.h changes
- Migrating existing JSON data automatically (operator clears old files)

## Decisions

### D1: SQLite via stdlib `sqlite3`

Zero new dependencies. Single file. ACID transactions. Trivially embeddable in the daemon.

Alternatives: TinyDB (external dep), shelve (dbm quirks), keep JSON with locking (complex, still fragile).

### D2: Daemon as sole database owner

Only one process holds the SQLite connection. `mmu.py` never opens the file directly — all access goes through the daemon HTTP API. Eliminates all write races.

Alternative: SQLite WAL mode with multiple openers — possible but requires careful connection management in both processes; more failure modes.

### D3: Two new HTTP endpoints on the daemon

```
GET  /config         → returns full gate config as JSON object
POST /config         → accepts partial or full gate config update, persists to SQLite
```

`mmu.py` replaces `_load_vars()` / `_save_vars()` with `urllib.request` calls to these endpoints. Timeout: 2s. On timeout/connection error: log warning, continue with in-memory defaults.

### D4: Schema

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

`gate_config` stores each field as a JSON-serialized value keyed by name
(`gate_color`, `gate_material`, `gate_spool_id`, `gate_color_rgb`,
`gate_name`, `gate_filament_name`, `ttg_map`, `spoolman_support`).
`stats` stores counters similarly (`swaps_total`, `swaps_success`,
`swaps_failed`, `loads_success`, `unloads_success`, `last_error`).

Flat key-value with JSON values trades normalization for zero migration pain and schema flexibility. Per-gate row normalization is premature for 2 gates.

### D5: DB path resolution

```python
DB_PATH = os.path.expanduser("~/printer_data/database/flare.db")
```

Single path, no fallback chain. Directory created on first run if absent.

### D6: Stats migration

`flare_mmu_stats.json` is retired. Daemon loads stats from SQLite on startup (zero if no prior row). Old JSON file is ignored and can be deleted by the operator.

## Risks / Trade-offs

- **Daemon startup ordering**: if Klipper loads `mmu.py` before the daemon is ready, `_load_vars()` HTTP call will fail → mmu.py starts with defaults. Next `_save_vars()` call will sync. Low impact: gate config is cosmetic, not safety-critical.
- **SQLite file permissions**: daemon creates the file; Klipper process doesn't need file access. Non-issue with D2 (daemon-only writer).
- **No auto-migration from JSON**: operators who had gate config in `flare_mmu_vars.json` lose it on upgrade. Acceptable for pre-release; operator re-enters gate map via UI.

## Migration Plan

1. Deploy new daemon with SQLite init + `/config` endpoints
2. Deploy new `mmu.py` with HTTP `_load_vars` / `_save_vars`
3. Operator restarts Klipper + daemon
4. Gate config starts from defaults; operator re-enters via Fluidd widget or `MMU_GATE_MAP`
5. Old JSON files (`flare_mmu_vars.json`, `flare_mmu_stats.json`) can be deleted

## Open Questions

- None blocking implementation.
