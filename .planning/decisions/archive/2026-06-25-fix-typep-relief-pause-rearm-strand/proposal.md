## Why

Type-P sync can strand in `SYNC_RELIEF_PAUSE` after slow-print-following-fast:
compression dwell triggers `sync_relief_pause()` (feed off), then — because
`sync_enabled` is only `SYNC_ACTIVE` — an idle/negative-sync stabilize starts
while paused. A returning fast feature pulls the buffer to the tension rail, but
`sync_tick` is fully blocked by `g_boot_stabilizing`, so the type-P relief re-arm
never runs; when stabilize finishes `buf_force_stable_state(BUF_NEUTRAL)` zeroes
`g_buf_pos` and bypasses `sync_on_transition`, leaving `g_vel_norm≈0` and
`g_sync_tension_transitioned=false` — the only type-P re-arm predicate is now
unsatisfiable. Sync sits paused (often visibly pinned at tension) until an
unrelated state re-cross perturbs it. Type-D avoids this via a transition-driven
re-arm (`sync_buf.c:838`) gated `BUF_SENSOR_TYPE_D` only.

## What Changes

- Add a type-P transition-driven re-arm from `SYNC_RELIEF_PAUSE` (fix #1):
  extend the `sync_buf.c` rearm-on-transition (currently type-D only) so a
  debounced cross into the type-P tension zone re-arms sync immediately, instead
  of relying solely on the polled velocity/flag predicate in
  `sync_tick_gated_checks`.
- Do not start idle/negative-sync stabilize while `g_sync_state ==
  SYNC_RELIEF_PAUSE` (fix #3): treat relief-pause as not-idle so `sync_tick`'s
  re-arm path is never blacked out by `g_boot_stabilizing`, and a real demand
  always wins over a cosmetic park.
- No new tunables. Type-D behavior unchanged.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `psf-type-p-sensor`: strengthen "Type-P Relief-Pause Auto-Recovery" — add a
  transition-driven re-arm path (not only the polled predicate) and forbid
  starting buffer-stabilize while in `SYNC_RELIEF_PAUSE`.

## Impact

- `firmware/src/sync_buf.c`: extend rearm-on-transition (`~:838`) to type-P.
- `firmware/src/sync.c`: gate `buffer_stabilize_tick` negative-sync/idle start
  against `SYNC_RELIEF_PAUSE` (via `buffer_stabilize_controller_idle` or the
  start guard).
- Spec `psf-type-p-sensor`. No config/protocol/tune.h change.
- Rig re-validation: type-P relief paths are timing-touchy
  (see memories/repo/psf-relief-pause-rearm.md, typep-stale-fault-timers).
