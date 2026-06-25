## Context

Type-P `SYNC_RELIEF_PAUSE` recovery is fragile. `sync_enabled` ≡ `SYNC_ACTIVE`
(`sync.h:17`), so RELIEF_PAUSE reads as sync-off. Main loop runs
`buffer_stabilize_tick` (`main.c:596`) before `sync_tick` (`main.c:605`); the
former starts negative-sync stabilize when the buffer is in compression and the
controller is "idle" (`buffer_stabilize_controller_idle()` needs only
`!sync_enabled` + TC idle + lanes idle). Once `g_boot_stabilizing` is set,
`sync_tick` early-returns whole (`sync.c:1899`), so the type-P relief re-arm in
`sync_tick_gated_checks` (`sync.c:1300-1310`) is blacked out. On stabilize DONE,
`boot_stabilize_tick_type_p` calls `buf_force_stable_state(BUF_NEUTRAL)`
(`sync.c:311`) which zeroes `g_buf_pos`, bypasses `sync_on_transition` (the only
setter of `g_sync_tension_transitioned`), and — under `!sync_enabled` — wipes
`g_extruder_est_sps`. The polled re-arm predicate (`g_buf_pos < -0.6` AND
(`tension_transitioned` OR `vel < -0.1`)) is then unsatisfiable at a pinned rail.
Type-D escapes via a transition-driven re-arm at `sync_buf.c:838`, gated
`BUF_SENSOR_TYPE_D` only.

## Goals / Non-Goals

**Goals:**
- Type-P re-arms from RELIEF_PAUSE on a debounced tension transition (fix #1), not
  only on the polled velocity/flag predicate.
- Never blackout `sync_tick` re-arm with a stabilize started during RELIEF_PAUSE
  (fix #3).
- Type-D behavior byte-for-byte unchanged. No new tunables.

**Non-Goals:**
- Touching the polled predicate thresholds or D18 auto-start logic.
- Reworking `buf_force_stable_state` / stabilize internals beyond gating when it
  may start.
- Type-P stabilize rail-breakaway (separate requirement, untouched).

## Decisions

### D1 — Type-P rearm-on-transition (fix #1)
At `sync_buf.c:838` the rearm-on-transition is `g_buf_sensor_type ==
BUF_SENSOR_TYPE_D`-gated. Split by sensor type:
- type-D: keep existing predicate (`new_state == BUF_NEUTRAL || BUF_TENSION`) and
  the `buf_target_reserve_mm()` reseed inside `sync_rearm_active`.
- type-P: re-arm when the debounced transition is into the tension zone
  (`new_state == BUF_TENSION`). Reuse `sync_rearm_active(lane, now_ms)`; for
  type-P it must NOT reseed `g_buf_pos` (it already guards the reseed behind
  `BUF_SENSOR_TYPE_D`, confirm at `sync.c:1071`). Keep `!g_boot_stabilizing` in
  the guard so this never fights an in-flight stabilize — D2 ensures stabilize
  cannot be in flight during RELIEF_PAUSE anyway.

This adds the velocity-independent path the polled predicate lacks: a transition
fires even when the arm is pinned and `g_vel_norm≈0`.

### D2 — No stabilize while RELIEF_PAUSE (fix #3)
Gate the stabilize start against RELIEF_PAUSE. Cleanest single chokepoint:
`buffer_stabilize_controller_idle()` (`sync.c:177`) already returns false when
`sync_enabled`; add `g_sync_state != SYNC_RELIEF_PAUSE` (and, defensively,
`SYNC_FAULT_HOLD` is already excluded via `sync_guard_active`? no — guard covers
RETRACT_ASSIST/FAULT_HOLD; RELIEF_PAUSE is the gap). Gating here blocks the
negative-sync start in `buffer_stabilize_tick` and any other caller uniformly.
Does not affect already-running stabilize (none can be running in RELIEF_PAUSE
once this lands) nor boot/idle stabilize when sync is fully OFF.

Result: in RELIEF_PAUSE `g_boot_stabilizing` stays false → `sync_tick` runs every
pass → both the polled (D1 path 1) and transition (D1 path 2) re-arms are live.

### D3 — Estimator wipe
With D2, `buf_force_stable_state(BUF_NEUTRAL)` no longer fires during
RELIEF_PAUSE, so the `!sync_enabled` estimator-zero branch (`sync_buf.c`) no
longer wipes `g_extruder_est_sps` mid-pause. No extra code; `sync_bootstrap_sps()`
on re-arm already tolerates a stale/zero estimate (falls to baseline floor, F2b).

## Risks / Trade-offs

- **Type-P loses idle compression park during RELIEF_PAUSE.** Acceptable: the
  buffer in compression-at-relief is exactly the state we want to re-arm out of,
  not park. Once sync re-arms (or fully stops to `SYNC_OFF`), normal idle
  stabilize resumes.
- **Transition re-arm could fire on a spurious tension blip.** Mitigated: it uses
  the debounced `g_buf.state`, same debounce that gates type-D today; and re-arm
  to `SYNC_ACTIVE` bootstrapped at the baseline floor is self-correcting (a
  non-demand blip settles back without slam).
- **Timing-touchy path** (per memories/repo/typep-stale-fault-timers,
  psf-relief-pause-rearm) — requires rig re-validation of the slow-after-fast
  edge case, not just host build/lint.
