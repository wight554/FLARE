## Why

Type-P sync starved the buffer to the tension rail on fast extruder moves. On
rig (`G1 E40 F1500` ≈ 25 mm/s, with `SYNC_MAX_SPS` ≈ 36.7 mm/s so the MMU *can*
out-feed), the buffer repeatedly slammed `BS:TENSION −1.0` and raised
`SYNC:cannot_refill` / `SYNC:TENSION_RISK_HIGH`.

Root cause is stacked feed lag, not the soft wall or the estimator sign (the PD
soft wall correctly sets `target = max_sps` at the tension rail, and the estimator
sign is correct at `sync.c:2055`):

1. The type-P feed apply uses a **distance-based EMA** keyed to filament moved
   (`L = SYNC_PSF_FILTER_MM = 25 mm`). It closes ~2% of the target gap per tick,
   so feed needs ~25 mm of travel (~1 s at 25 mm/s) to ramp to max — the buffer
   starves to the rail first.
2. The estimator itself is EMA'd (`alpha = 0.1`), adding ~400 ms of lag to the
   demand signal that drives the distance clock.

The smoothing exists to kill compression-side bang-bang/overfeed; it should not
gate the **tension/refill** direction, which is the urgent + safe direction (a
brief overfeed once recovered is absorbed by the existing compression smoothing).

## What Changes

- In the type-P feed apply, when the buffer is in the **tension soft-wall zone**
  (`buf_pos_norm() < -PSF_SOFT_WALL_START`) and feed needs to rise
  (`target_sps > sync_current_sps`), **bypass the distance-EMA/slew and snap
  `sync_current_sps` to the soft-wall target** (`max_sps`) for fast refill.
  (`sync.c`, commit `fb490ad`)
- Seed the smoothing filter at **demand** (`g_psf_target_filt = extruder_est_sps`),
  not the wall's `max_sps`, so when the buffer climbs out of the wall the feed
  eases to the extruder rate instead of staying pinned at max and overshooting
  into compression. (`sync.c`, commit `9a17441`)
- Compression/neutral feed behaviour unchanged (smoothing + wall-clock decay
  still own the overfeed side). Type-D unchanged.

## Capabilities

### Modified Capabilities
- `psf-type-p-sensor`: the tension/refill side of the feed apply snaps to the
  soft-wall target (no distance-EMA throttle) and settles toward demand on exit,
  so fast extruder moves no longer starve the buffer to the tension rail.

## Impact

- `firmware/src/sync.c`: type-P branch of the feed-apply block in `sync_tick()`.
- Rig: `G1 E40 F1500` — `cannot_refill` eliminated; mid-burst buffer rides the
  goal band (`0.0 … +0.47`) instead of slamming `−1.0` or overshooting to `+0.71`.
- No NVM/protocol/host changes. The threshold reuses `CONF_PSF_SOFT_WALL_START`.
