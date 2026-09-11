## Why

When `reload_trigger` fires a `RELOAD:FAULT NO_FILAMENT` event (other lane not loaded at runout), it returns immediately without stopping the sync motor. The sync motor continues running at full speed until the user manually issues `ST`, wasting energy and causing unnecessary mechanical wear.

## What Changes

- `reload_trigger()` calls `sync_disable(true)` before returning on the NO_FILAMENT path, stopping the sync motor and resetting the estimator.

## Capabilities

### New Capabilities

- `no-filament-fault-sync-stop`: Sync motor stops automatically when a NO_FILAMENT fault is raised during reload, reflected as `SM:0` in `?:` status.

### Modified Capabilities

<!-- None — no existing spec-level requirements change. -->

## Impact

- `firmware/src/toolchange.c`: single-line fix in `reload_trigger()`.
- All three runout paths in `motion.c` that call `reload_trigger` benefit automatically.
- No protocol changes, no config/settings changes, no host-tool changes.
