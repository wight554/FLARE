## Context

`reload_trigger()` in `firmware/src/toolchange.c` handles lane runout. When the other lane has no filament (`lane_in_present` returns false), it emits `RELOAD:FAULT NO_FILAMENT` and returns early. The sync motor (tracked via `g_sync_state`) is left running in `SYNC_ACTIVE` state.

Three runout paths in `motion.c` (lines ~404, ~436, ~462) all call `reload_trigger`. None of them stop sync themselves — they rely on `reload_trigger` to handle the TC-level response.

## Goals / Non-Goals

**Goals:**
- Sync motor stops automatically on NO_FILAMENT fault.
- Status reflects `SM:0` after fault without manual `ST`.

**Non-Goals:**
- Auto-recovery / auto-resume when filament is loaded later (user initiates that).
- Changes to runout detection, lane task management, or event protocol.

## Decisions

**`sync_disable(true)` over `sync_fault_hold()`**

`sync_fault_hold` auto-recovers after `CONF_SYNC_FAULT_HOLD_RECOVERY_MS` (5 s) and attempts `AUTO_START` if buffer reaches `BUF_TENSION`. With no filament in either lane, buffer won't reach TENSION, so recovery would hang — but that's fragile reasoning. NO_FILAMENT is a hard stop; the system cannot self-recover. `sync_disable(true)` is the correct semantic: full stop, estimator reset, no auto-retry.

`true` (reset estimator): when filament is eventually loaded, sync must bootstrap fresh. Stale estimator state from a previous empty-print run would produce incorrect initial SPS.

**Call site: `reload_trigger()`, not motion.c callers**

All three motion.c call sites hit this same guard. One fix in `reload_trigger` covers all paths and any future callers.

## Risks / Trade-offs

No recovery loop risk → `sync_disable` sets `SYNC_OFF`, and `AUTO_MODE` auto-start requires `BUF_TENSION` which won't occur with no filament. No unintended restart.

Estimator reset on `true` → minor cost: sync needs a fresh warm-up after filament is manually loaded. Acceptable — alternative (stale estimator) causes worse initial overshoot.

## Open Questions

None.
