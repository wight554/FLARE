---
phase: 15-daemon-moonraker-lane-data-maintenance-counters
plan: 01
subsystem: host-daemon
tags: [moonraker, orcaslicer, spoolman, lane_data, database, daemon-sync]

# Dependency graph
requires: []
provides:
  - "Moonraker DB lane_data namespace synchronization for OrcaSlicer compatibility"
  - "Spoolman metadata extraction (vendor, temps, filament_id, transmission_distance)"
  - "Automatic fallback to gate map defaults when Spoolman is offline or unassigned"
  - "Orphan lane_data key pruning in Moonraker DB"
  - "Unit tests in scripts/test_moonraker_lane_data.py"
  - "Three Phase 15 requirements registered in REQUIREMENTS.md"
affects: [15-02-PLAN.md, flare_daemon.py]

# Actuals
actuals:
  tasks: 3
  files: 4

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Non-blocking Moonraker DB sync via dedicated worker thread and bounded queue"
    - "Spoolman metadata extraction enriched with temperatures and vendor details"
    - "Directory-safe and file-safe SQLite database opening with os.path.exists checks"

key-files:
  created:
    - scripts/test_moonraker_lane_data.py
  modified:
    - scripts/flare_daemon.py
    - .planning/REQUIREMENTS.md
    - .planning/ROADMAP.md

key-decisions:
  - "Spoolman metadata extraction expanded to include vendor_name, bed_temp, nozzle_temp, filament_id, and transmission_distance without breaking legacy consumers"
  - "Moonraker DB database item POST and DELETE calls run with 2.0s timeouts in a background worker thread to ensure USB serial responsiveness is never impacted"
  - "Orphan keys beyond NUM_GATES are actively pruned during lane_data synchronization"

requirements-completed:
  - REQ-moonraker-lane-data-push
  - REQ-moonraker-lane-data-sync-lifecycle
  - REQ-moonraker-lane-data-cleanup

coverage:
  - id: D1
    description: "Moonraker lane_data payload generated with OrcaSlicer schema and Spoolman enrichment"
    requirement: "REQ-moonraker-lane-data-push"
    verification:
      - kind: unit
        ref: "scripts/test_moonraker_lane_data.py#test_build_moonraker_lane_payload_fallback / test_build_moonraker_lane_payload_with_spoolman"
        status: pass
    human_judgment: false
  - id: D2
    description: "Lifecycle synchronization triggered on startup, gate edits, and Spoolman updates"
    requirement: "REQ-moonraker-lane-data-sync-lifecycle"
    verification:
      - kind: unit
        ref: "scripts/test_moonraker_lane_data.py#test_trigger_moonraker_lane_data_sync_queues / test_sync_moonraker_network_error_resilience"
        status: pass
    human_judgment: false
  - id: D3
    description: "Orphan lane_data keys pruned from Moonraker DB"
    requirement: "REQ-moonraker-lane-data-cleanup"
    verification:
      - kind: unit
        ref: "scripts/test_moonraker_lane_data.py#test_sync_moonraker_lane_data_pushes_and_cleans_orphans"
        status: pass
    human_judgment: false
---

# Phase 15 Plan 01: Moonraker Lane Data Sync Summary

Implemented Moonraker DB `lane_data` namespace synchronization matching Happy-Hare v4 (`components/mmu_server.py:1746-1846`) for OrcaSlicer AMS/MMU filament discovery.

## Key Changes
1. **Metadata Enrichment**:
   - `_spoolman_fetch_spool` extracts `vendor_name`, `settings_bed_temp`, `settings_extruder_temp`, `filament.id`, and `extra.transmission_distance`.
   - `build_moonraker_lane_payload` formats full OrcaSlicer JSON schema (`vendor_name`, `name`, `color`, `material`, `bed_temp`, `nozzle_temp`, `scan_time`, `td`, `lane`, `spool_id`, `filament_id`).
   - Falls back to `gate_map` configuration with 0 temperatures if Spoolman is unassigned or offline.
2. **Moonraker DB Client & Worker**:
   - Implemented `_moonraker_db_post_item`, `_moonraker_db_delete_item`, and `_moonraker_db_get_namespace`.
   - Implemented non-blocking `_lane_sync_worker` thread and `trigger_moonraker_lane_data_sync()`.
   - Pruning removes stale keys `lane<N>` where `N >= NUM_GATES`.
3. **Database Safety**:
   - Ensured `db_get` checks `os.path.exists(_DB_PATH)` and `db_set` ensures directory existence before connecting.
4. **Verification**:
   - Unit tests in `scripts/test_moonraker_lane_data.py` verify payload formatting, Spoolman enrichment, orphan cleanup, retry resilience, and queueing.
   - All regression gates and lints passed.
