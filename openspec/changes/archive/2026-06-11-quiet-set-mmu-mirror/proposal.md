## Why

Idle Klipper console floods with the full `SET_MMU` blob (~34 fields) every 10 s even
when MMU state is frozen. Cause: the `flare_daemon.py` push loop force-full resync timer
(`scripts/flare_daemon.py:1008`) resends every field unconditionally each 10 s for restart
recovery, bypassing the delta logic that already exists below it.

The buffer is NOT the source. In an idle trace the Hall read wobbles `SYNC_FEEDBACK
0.400 ↔ 0.390` (±0.01), below the `0.05` push threshold (`:1002`) → no trigger. `BUF`
bucket, lane, sensors all steady. Only the 10 s timer fires; it merely snapshots the
current noisy value as it blasts the full set. So the changing number is cosmetic noise
riding the timer, not a state change.

Restart recovery is the timer's only justification, and a restart is detectable. A blind
periodic full-push is a poll standing in for an event.

## What Changes

- Replace blind periodic full-push with a **silent reconcile**: keep the periodic tick,
  but read the Klipper `mmu` object and emit a full `SET_MMU` only when it diverges from
  the desired field set (real restart / drift). No divergence → no emit. Idle goes quiet.
- Delta-on-change path (already present, `:1166`) governs steady state unchanged — real
  bucket / lane / sensor changes still push immediately.
- Secondary (optional, gated on motion-time evidence): debounce the published type-P
  `BUF` label (`buf_status_label`, `firmware/src/protocol_status.c:23`). It uses a fixed
  ±0.1 neutral deadzone about mechanical centre with NO time hysteresis, so a signal
  hovering at the ±0.1 edge flaps the bucket and emits real deltas.
- Operator stopgap (no code): Mainsail/Fluidd console hidden-command filter `^SET_MMU`.

## Capabilities

### Modified Capabilities

- `daemon-klipper-mirror`: full-resync trigger redefined. The periodic tick becomes a
  silent state-reconcile read; a full `SET_MMU` is emitted only on first push, on a
  board-online transition, or when the read shows the Klipper mock diverged.

## Impact

- `scripts/flare_daemon.py` push loop: `force_full` timer → reconcile read of
  `/printer/objects/query?mmu`, full push gated on mismatch.
- Adds one cheap Moonraker GET per tick (read — no console echo).
- Secondary: `firmware/src/protocol_status.c` `buf_status_label` if layer-B pursued
  (own firmware-spec delta then; not in this change's spec delta).
- Risk: the reconcile compare must use the same formatted values `cmd_SET_MMU` stores —
  false-positive re-introduces spam, false-negative leaves stale UI after restart.
  Validate across Klipper restart AND Moonraker restart, idle and mid-print.
