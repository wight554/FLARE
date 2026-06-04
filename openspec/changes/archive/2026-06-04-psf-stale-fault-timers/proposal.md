## Why

Type-P sync hit a print-killer: a normal extrude (`G1 E150 F1200` ≈ 20 mm/s, well
under `SYNC_MAX_SPS` ≈ 36.7 mm/s) `FAULT_HOLD`-stalled for 5 s and starved the
extruder, then deadlocked in an infinite `FAULT_HOLD ↔ RECOVERY ↔ AUTO_START`
loop. Two fault timers survived the OFF→ACTIVE boundary as stale values:

1. **Tension-dwell timer** (`sync_tension_pin_since_ms`) — set on the
   NEUTRAL→TENSION control-zone transition *even while sync is OFF*. Because
   `BUF_GOAL` is compression-side (`+0.40`), an idle buffer rests at `~0.0`, which
   is inside the control TENSION zone (`pos < goal − deadband = 0.30`). So the
   timer accumulates a large stale dwell during idle, and on `AUTO_START` the
   tension-dwell fault fired **instantly** (observed at `BP −0.27`, before the
   buffer ever saturated).

2. **Saturation timer** (`g_buf_analog_saturated_since_ms`) — not reset on
   `FAULT_HOLD_RECOVERY`. A buffer still pinned at `−1.0` (extruder pulled through
   the 5 s hold) carried a long-expired timer, so the saturation check re-faulted
   on the next tick after recovery → the infinite loop.

## What Changes

- Restart `sync_tension_pin_since_ms` at every type-P sync activation (normal
  auto-start, relief-pause re-arm, fault-hold recovery): set it to `now_ms` if the
  buffer is currently in `BUF_TENSION`, else `0`. The dwell-to-fault then counts
  only tension time *during* active sync; a genuine sustained starve still faults
  after the window. (`sync.c`, commit `f6af580`)
- Reset `g_buf_analog_saturated_since_ms` on `FAULT_HOLD_RECOVERY` so the recovered
  ACTIVE state gets a fresh `PSF_WALL_SAT_MS` window for the refill snap to relieve
  the rail before any re-fault — breaking the deadlock loop. (`sync.c`, commit
  `17e4152`)
- Type-D unchanged (the recovery reseed and type-D paths are gated/separate).

## Capabilities

### Modified Capabilities
- `psf-type-p-sensor`: tension-dwell and saturation fault timers are tied to the
  active-sync window, so idle-accumulated state can no longer fire a spurious
  `FAULT_HOLD` on engagement or deadlock the recovery.

## Impact

- `firmware/src/sync.c`: `sync_tick()` auto-start / relief-rearm / fault-recovery
  blocks; saturation-fault path.
- Rig: `G1 E150 F1200` no longer faults/starves — sync engages, the buffer dips to
  tension and recovers (refill snap), ends at goal. No `FAULT_HOLD`, no deadlock.
- No NVM/protocol/host changes.
