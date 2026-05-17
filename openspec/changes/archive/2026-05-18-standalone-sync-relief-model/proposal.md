## Why

Today the sync controller uses destructive `sync_disable(true)` as a relief
mechanism: when the buffer stays pinned at the trailing/full wall or hits the
hard wall, it wipes the extruder estimator, drift observer, sigma/confidence
state, and integrators, then must cold-bootstrap on the next ADVANCE. This
throws away hard-won local state on routine overfull events, and the live
baseline learner updates eagerly (one EWMA per settle, no gating) even right
after these abnormal events — so it learns from distorted data. NOSF is
standalone (no Klipper/host speed, no extrusion deltas, no encoder); local
state is expensive to rebuild and must not be discarded casually.

## What Changes

- Introduce an explicit sync state model:
  `SYNC_OFF / SYNC_ACTIVE / SYNC_HOLD / SYNC_RELIEF_PAUSE / SYNC_FAULT_HOLD`,
  replacing the current ad-hoc booleans and early-returns.
- Keep the existing graded trailing escalation (soft-wall trim, collapse
  ramp/cap) **unchanged**. Replace **only** the two terminal destructive
  `sync_disable(true)` calls:
  - continuous-trailing auto-stop (`sync.c:1295`) → `SYNC_RELIEF_PAUSE`
    (non-destructive: estimator/drift/sigma/integrators preserved; resume on
    ADVANCE re-arm, exactly the current auto-stop→auto-start cycle).
  - hard-wall critical (`sync.c:1245`) → `SYNC_FAULT_HOLD` (motor stopped,
    state preserved, auto conservative recovery, no host needed).
- Formalize the host `HD` HOLD command as `SYNC_HOLD`: closed loop off,
  buffer-stabilize-to-MID allowed (HD only sent when paused/idle, extruder not
  pulling), estimator/state preserved, learning paused. Replaces the current
  near-destructive `sync_disable(false)` on HD.
- Discipline the live baseline learner (`baseline_update_on_settle`): gate to
  `SYNC_ACTIVE` only, require multi-cycle agreement, variance/outlier reject,
  cooldown. Keep the existing `max()` (up-only) + non-persistent properties.
  Offline analyzer remains the sole **persistent** baseline/bias authority.
- Add warn-only FlowGuard-style relief-effort counters in commanded-MMU mm
  (cannot-refill on sustained ADVANCE, cannot-relieve on sustained TRAILING)
  as diagnostics; no behavior change from the counters themselves.
- Suppress `mid_creep` in `SYNC_HOLD / SYNC_RELIEF_PAUSE / SYNC_FAULT_HOLD`;
  keep it active in `SYNC_ACTIVE`.
- **No change** to the full-bias buffer invariant: the reserve target between
  MID and TRAILING (`buf_target_reserve_mm`, `reserve_correction`,
  `zone_bias`) is owned entirely by `SYNC_ACTIVE` control and is untouched.
- Out of scope (explicitly deferred to follow-up change
  `flow-keyed-param-schedule`, which depends on this one): replacing the
  scalar baseline/trailing-bias with a flow-keyed schedule. The scalar path is
  preserved here and becomes that change's degenerate single-point fallback.

## Capabilities

### New Capabilities
- `sync-state-model`: The explicit sync lifecycle states, their transitions,
  entry/exit conditions, what state is preserved vs reset per state, and the
  non-destructive relief/fault contract.

### Modified Capabilities
- `motion-safety`: Trailing/jam handling no longer destroys estimator state;
  relief vs fault distinction; ADVANCE-never-paused invariant; FAULT_HOLD
  auto-recovery without host.
- `sync-refactor`: Sync controller lifecycle and estimator-preservation
  contract replace ad-hoc disable/bootstrap behavior.
- `calibration-workflow`: Live baseline learning gated to `SYNC_ACTIVE`,
  multi-cycle/variance/cooldown disciplined; offline analyzer is the only
  persistent authority.
- `live-tuner`: New sync states and relief-effort counters exposed in status
  and diagnostics output.

## Impact

- Firmware: `firmware/src/sync.c` (state model, transitions, relief/fault
  paths, disciplined learner, effort counters), `firmware/src/protocol.c`
  (HD → SYNC_HOLD, status/diag fields), minor `firmware/src/motion.c` and
  `firmware/src/main.c` (state global, tail-finish disable path).
- No host/Klipper coupling added; no encoder; pure local data only.
- No persistence-format change (live baseline stays non-persistent;
  `g_baseline_target_sps` offline authority unchanged).
- Behavior parity target: SYNC_ACTIVE control output and full-bias operation
  byte-for-byte equivalent to current; only terminal destructive disable on
  jam paths becomes non-destructive state retention.
