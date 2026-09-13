# Phase 15: Daemon Moonraker Lane Data & Maintenance Counters — Specification

**Created:** 2026-09-13
**Ambiguity score:** 0.11 (gate: ≤ 0.20)
**Requirements:** 9 locked

## Goal

Extend `flare_daemon.py`, `klipper/mmu.py`, and the WebUI to push OrcaSlicer-compatible `lane_data` into Moonraker DB and track persistent consumable maintenance counters (`cutter_cuts`, `swaps`, `reload_failovers`, custom) with configurable limit/warning/pause thresholds.

## Background

Today `flare_daemon.py` synchronizes gate map parameters with Klipper and queries Spoolman via Moonraker proxy, but does not publish `lane_data` to Moonraker's DB namespace `lane_data`. As a result, OrcaSlicer AMS/MMU multi-material filament sync cannot automatically discover spool names, colors, materials, and recommended printing temperatures.
Furthermore, while `flare_daemon.py` tracks basic swap and load counters in a flat `stats` table, it does not provide consumable lifecycle tracking (such as cutter blade wear from `EV:CUT:DONE`, toolchange cycles, or failover events), nor does it provide limit alerts, pause escalation, or Happy-Hare `MMU_STATS COUNTER=...` command parity.

## Requirements

1. **`REQ-moonraker-lane-data-push`**: Daemon must synchronize `lane_data` to Moonraker DB namespace `lane_data` under keys `lane<N>` (e.g. `lane0`, `lane1`), containing `{vendor_name, name, color, material, bed_temp, nozzle_temp, scan_time, td, lane, spool_id, filament_id}` matching Happy-Hare / OrcaSlicer contract (`mmu_server.py:1746-1820`).
   - Current: `flare_daemon.py` writes nothing to Moonraker DB namespace `lane_data`.
   - Target: On each sync, daemon formats and POSTs/PUTs each configured lane record to `/server/database/item?namespace=lane_data`.
   - Acceptance: Inspecting Moonraker DB `lane_data` namespace returns populated JSON dicts for all active lanes with Spoolman-derived (or gate-map fallback) filament metadata.

2. **`REQ-moonraker-lane-data-sync-lifecycle`**: Synchronization must trigger automatically on daemon startup/connect, whenever gate map metadata is modified (via `/gatemap` or `/config` POSTs), and whenever Spoolman active spools change.
   - Current: No trigger mechanism exists for Moonraker DB namespace writes.
   - Target: Event-driven trigger calls asynchronous/non-blocking sync handler with backoff retry on Moonraker connection failure.
   - Acceptance: Changing gate color/spool ID in WebUI immediately updates Moonraker `lane_data` in DB without blocking daemon event processing.

3. **`REQ-moonraker-lane-data-cleanup`**: Daemon must delete orphaned `lane<N>` keys via Moonraker DB DELETE API (`/server/database/item?namespace=lane_data&key=lane<N>`) when gate count or active gates decrease.
   - Current: No cleanup logic exists.
   - Target: Daemon queries existing keys in `lane_data` namespace and deletes keys beyond active gate count.
   - Acceptance: Reducing `NUM_GATES` or clearing gate entries removes corresponding stale keys from Moonraker DB.

4. **`REQ-maintenance-counters-persistence`**: Maintenance counters must be stored in SQLite table `maintenance_counters` with fields `name` (TEXT PRIMARY KEY), `count` (INTEGER), `limit_val` (INTEGER NULL), `warning` (TEXT), `pause` (INTEGER 0/1), and `last_reset` (TEXT ISO timestamp).
   - Current: Daemon stores only simple monotonically increasing totals in `stats` table (`swaps_total`, `swaps_success`, `loads_success`, etc.) without thresholds or reset history.
   - Target: Dedicated `maintenance_counters` table tracks default counters (`cutter_cuts`, `swaps`, `reload_failovers`) and allows arbitrary user-defined counters.
   - Acceptance: Counters survive daemon restart, maintain independent counts and thresholds, and reflect reset operations.

5. **`REQ-maintenance-counter-event-hooks`**: Daemon event router must increment corresponding maintenance counters on firmware events:
   - `EV:CUT:DONE` -> increments `cutter_cuts`
   - `TC:DONE` -> increments `swaps`
   - RELOAD runout trigger (`EV:RELOAD:APPROACH` or RELOAD-driven swap) -> increments `reload_failovers`
   - Current: `EV:CUT:DONE` is logged but not accumulated for maintenance tracking.
   - Target: Each matching event increments counter by 1, saves to SQLite, and updates cached status.
   - Acceptance: Emitting `EV:CUT:DONE` over USB serial increases `cutter_cuts` count by 1 in SQLite and `/status`.

6. **`REQ-maintenance-counter-threshold-actions`**: When a counter reaches or exceeds its configured `limit_val` (> 0):
   - Daemon logs warning and adds notification to `status_cache["maintenance"]["warnings"]`.
   - Daemon emits warning text to Klipper console via `M118` or `respond_info`.
   - If `pause == 1`, daemon executes Klipper `PAUSE` script via Moonraker `/printer/gcode/script`.
   - Current: No threshold monitoring or pause escalation exists in daemon.
   - Target: Immediate threshold check on every increment with deduplicated warning / optional pause execution.
   - Acceptance: Setting `cutter_cuts` limit to 5 with `PAUSE=1`; on 5th cut, warning is emitted and Klipper PAUSE is triggered.

7. **`REQ-maintenance-klipper-command-parity`**: `klipper/mmu.py` and `flare_daemon.py` must support the Happy-Hare `MMU_STATS` maintenance parameter syntax:
   `MMU_STATS COUNTER=<name> [INCR=<n>] [LIMIT=<n>] [WARNING="<text>"] [PAUSE=0|1] [RESET=1] [DELETE=1]`
   - Current: `klipper/mmu.py` supports only bare `MMU_STATS` and `SHOWCOUNTS=1` reading read-only swap totals.
   - Target: `MMU_STATS` handles counter lookup, threshold configuration, manual increment, and reset via daemon API bridge. Bare `MMU_STATS` or `SHOWCOUNTS=1` lists maintenance counter states.
   - Acceptance: Running `MMU_STATS COUNTER=cutter_cuts RESET=1` clears `cutter_cuts` count to 0 and records `last_reset`.

8. **`REQ-maintenance-webui-dashboard`**: WebUI must display a Maintenance card/section showing all tracked counters, current count vs limit bar, warning indicator when over limit, and a Reset button per counter.
   - Current: WebUI displays status, lanes, and logs, but no maintenance or consumable lifecycle indicators.
   - Target: WebUI renders maintenance section populated from `/status` or `/maintenance` endpoint and sends reset/edit commands.
   - Acceptance: User viewing WebUI sees cutter blade cuts count, remaining life percentage, and can click Reset.

9. **`REQ-daemon-maintenance-unit-tests`**: Automated unit tests must cover the complete lifecycle:
   - Moonraker DB `lane_data` payload formation and HTTP request dispatch.
   - Cleanup of orphan `lane_data` keys.
   - SQLite CRUD operations for maintenance counters across restarts.
   - Event increments from `EV:CUT:DONE`, `TC:DONE`, and RELOAD events.
   - Limit warning emission and `PAUSE` dispatch.
   - Current: No unit tests cover Moonraker DB `lane_data` or maintenance limits.
   - Target: Comprehensive test suite in `scripts/test_flare_daemon_maintenance.py` passing in CI/regression gate.
   - Acceptance: `python3 -m unittest scripts.test_flare_daemon_maintenance` passes 100% cleanly.

## Boundaries

**In scope:**
- Moonraker DB namespace `lane_data` publishing and orphan key cleanup in `scripts/flare_daemon.py`.
- Metadata extraction from Spoolman (with gate-map fallback) for temperatures, vendor, color, material.
- SQLite schema addition (`maintenance_counters` table) in `scripts/flare_daemon.py`.
- Event increment listeners for `cutter_cuts`, `swaps`, and `reload_failovers`.
- Threshold checking, warning formatting, and optional Klipper PAUSE dispatch.
- `MMU_STATS COUNTER=...` parameter parsing and command handling in `klipper/mmu.py` and `scripts/flare_daemon.py`.
- Maintenance UI cards and reset actions in `scripts/webui/`.
- Unit tests for all added functionality.

**Out of scope:**
- RP2040 firmware C modifications — firmware already provides `EV:CUT:DONE`, `TC:DONE`, `EV:RELOAD:*`.
- Slicer G-code preprocessor plugin — handled separately.
- Modifying Spoolman database directly — Spoolman remains external service queried via Moonraker proxy.

## Constraints

- **Python standard library only**: No third-party PyPI dependencies added to `flare_daemon.py` or scripts (`urllib.request`, `sqlite3`, `json`, `threading`).
- **Non-blocking networking**: Moonraker DB calls must execute in worker thread / non-blocking requests with timeouts (1.0-2.0s) to never stall serial processing.
- **Backward compatibility**: Existing `/status`, `/config`, and `/gatemap` response formats remain backwards-compatible.
- **WebUI vanilla architecture**: No Node.js / Webpack / framework build steps for WebUI; plain vanilla JS, HTML, CSS.

## Acceptance Criteria

- [ ] `lane_data` namespace in Moonraker DB contains valid JSON for all gates matching OrcaSlicer schema (`vendor_name`, `name`, `color`, `material`, `bed_temp`, `nozzle_temp`, `lane`, `spool_id`, `filament_id`).
- [ ] Orphan `lane_data` keys are removed from Moonraker DB when gate count decreases.
- [ ] Database stores `maintenance_counters` across daemon restarts.
- [ ] `EV:CUT:DONE` increments `cutter_cuts` counter.
- [ ] `TC:DONE` increments `swaps` counter.
- [ ] Exceeding `limit_val` produces warning in `/status` and console message.
- [ ] Setting `pause=1` on a counter issues Klipper `PAUSE` when limit is exceeded.
- [ ] `MMU_STATS COUNTER=<name> RESET=1` resets counter to 0.
- [ ] WebUI renders maintenance counter cards with progress and reset button.
- [ ] `scripts/test_flare_daemon_maintenance.py` passes all unit tests cleanly.

## Edge Coverage

**Coverage:** 5/5 applicable edges resolved · 0 unresolved

| Category | Requirement | Status | Resolution / Reason |
|----------|-------------|--------|---------------------|
| Network | REQ-moonraker-lane-data-sync-lifecycle | ✅ covered | Moonraker connection refused / timeout handled gracefully in background worker thread with exponential backoff |
| Data | REQ-moonraker-lane-data-push | ✅ covered | Spoolman offline or gate unassigned falls back to gate map defaults with 0 temps |
| Concurrency | REQ-maintenance-counters-persistence | ✅ covered | SQLite access protected by `_db_lock` and thread locks |
| Lifecycle | REQ-maintenance-counters-persistence | ✅ covered | Default counters (`cutter_cuts`, `swaps`, `reload_failovers`) auto-seeded on first run if table empty |
| Command | REQ-maintenance-klipper-command-parity | ✅ covered | `MMU_STATS COUNTER=unknown` gracefully reports counter not found or auto-creates if `LIMIT=` provided |

## Prohibitions (must-NOT)

**Coverage:** 4/4 applicable prohibitions resolved · 0 unresolved

| Prohibition (must-NOT statement) | Requirement | Status | Verification / Reason |
|----------------------------------|-------------|--------|------------------------|
| MUST NOT block serial reader loop on Moonraker DB HTTP calls | REQ-moonraker-lane-data-push | resolved | verification: test — background queue / thread dispatches HTTP requests |
| MUST NOT crash or corrupt SQLite if database file is locked or daemon abruptly killed | REQ-maintenance-counters-persistence | resolved | verification: test — transactions with `con.commit()` inside try/finally blocks |
| MUST NOT trigger repetitive Klipper `PAUSE` on every loop once a counter is over limit | REQ-maintenance-counter-threshold-actions | resolved | verification: test — pause triggered once on boundary transition until reset |
| MUST NOT drop existing keys or break `/status` schema for current dashboards | REQ-maintenance-counters-persistence | resolved | verification: test — new keys added to `/status` without modifying existing keys |

## Ambiguity Report

| Dimension          | Score | Min  | Status | Notes                              |
|--------------------|-------|------|--------|------------------------------------|
| Goal Clarity       | 0.90  | 0.75 | PASS   | Outcome measurable and grounded in Happy-Hare borrow scan |
| Boundary Clarity   | 0.90  | 0.70 | PASS   | Explicit in-scope and out-of-scope delineation |
| Constraint Clarity | 0.85  | 0.65 | PASS   | Concurrency, stdlib-only, Moonraker DB contracts defined |
| Acceptance Criteria| 0.90  | 0.70 | PASS   | 10 verifiable binary checkboxes |

**Composite Ambiguity:** 0.11 (Gate: ≤ 0.20) — **READY FOR PLANNING**
