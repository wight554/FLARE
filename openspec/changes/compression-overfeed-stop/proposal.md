## Why

A fast purge (extrude hard, then stop for brush/pause) drives the type-D buffer
into COMPRESSION, and the relay then keeps feeding `SYNC_MIN_SPS` **forward**
into an already-full buffer for up to 5 s — until the blind
`SYNC_AUTO_STOP_MS` timer fires `RELIEF_PAUSE`. Captured purge telemetry shows
~8 mm of filament force-fed into the full buffer over ~4.9 s (`SYNC_RELIEVE_MM`
2→10, `CT` 57→4962 ms), with `BP` deepening -5.0→-5.6. That sustained pressure
is what broke a bowden tube and stressed the filament sensor. The `cannot_relieve`
guard is set at 50 mm — far beyond the damage threshold, so it never fires.

This replaces the abandoned `neutral-demand-collapse-brake` attempt (a mid-band
estimator corrector), which was reverted because a 2-switch type-D buffer has no
ground truth between switch crossings — see design.md *Alternatives considered*.

## What Changes

- Type-D COMPRESSION law: command a **true stop (0 feed)** while the buffer is
  pinned full and cannot relieve, instead of `SYNC_MIN_SPS` forward. Eliminates
  forward overfill into a full buffer; the extruder still draws the buffer off
  the wall.
- Demand-aware fast relief: when pinned in COMPRESSION with relieve effort
  accumulating and `BP` not recovering, stop within a small overfill budget
  (~1-2 mm) instead of waiting out the ~5 s `SYNC_AUTO_STOP_MS` timer.
- Scope is the type-D virtual-endstop path (`BUF_SENSOR_TYPE == 0`) only;
  type-P analog control behavior is unchanged.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `sync-refactor`: the type-D hysteretic relay COMPRESSION state commands a true
  stop when the buffer cannot relieve, and the compression relief becomes
  demand-aware (overfill-budgeted) rather than a blind fixed timer, so a purge
  ending against the wall no longer force-feeds a full buffer.

## Impact

- Firmware: `firmware/src/sync.c` — the type-D relay COMPRESSION branch
  (currently `target_sps = SYNC_MIN_SPS`) and the continuous-compression
  auto-stop block (`sync_continuous_compression_since_ms` / `SYNC_AUTO_STOP_MS`
  / `RELIEF_PAUSE`).
- Possible tunable change: a small compression overfill / relief-stop threshold
  (replacing or supplementing the 50 mm `CONF_SYNC_CANNOT_RELIEVE_MM` and the
  5 s `CONF_SYNC_AUTO_STOP_MS`); flows through `config.ini` → generated `tune.h`.
- Interaction risk: `relay_min_flip_mm` (currently `0.5`) plus a zero
  COMPRESSION feed must not freeze the flip out of COMPRESSION (prior deadlock
  history with non-zero min-flip). Must be verified in design/implementation.
- Behavior gated to `BUF_SENSOR_TYPE == 0`; type-P analog path stays
  byte-identical.
- Validated host-side with `scripts/flare_purge_check.py` (purge A/B + normal
  print regression C).
