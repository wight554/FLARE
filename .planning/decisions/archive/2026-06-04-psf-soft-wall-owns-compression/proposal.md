## Why

Type-P's compression-overfeed backoff is fought over by two independent brakes:
the analog-native **soft wall** (`psf_control_law`, gated correctly to type-P)
and the **`sync_compression_recovery` cap** (`sync_tick` L2470), which is shared
and **ungated** — it arms for type-P too.

The recovery cap is a type-D estimator-era device. Type-D is blind between
switch crossings, so a COMPRESSION click is its only "stop overfeed" signal and
it must brake hard and latch. Type-P measures position continuously, so the soft
wall already backs feed off gradually and proportionally. Running both on type-P
means:

- **The recovery cap pre-empts the soft wall.** It arms on COMPRESSION *zone*
  entry (~0.5 norm from goal, where `g_buf_pos` passes `goal ± PSF_ZONE_DEADBAND`)
  — far earlier than the soft wall's `PSF_SOFT_WALL_START = 0.8`. Layer-2's
  gradual compression-side blend is therefore mostly dead.
- **It slams, not recovers.** `recovery_cap = extruder_est_sps - kp_window`, and
  `kp_window = SYNC_KP_SPS = 13811` dwarfs any real demand, so the cap is
  essentially always negative → clamped to `compression_floor_sps` (`max(SYNC_MIN_SPS,
  COMPRESSION_SPS)` = 682 sps ≈ 1.67 mm/s), then time-collapsed further.
- **It latches** until the buffer leaves to NEUTRAL/TENSION, so a brief overfeed
  dip starves feed to 1.67 mm/s through the entire drain back — an oscillation
  source exactly where type-P PD smoothness was the goal.

This is the psf-analog-rig open question "compression_recovery / soft-wall
overlap" (design D7), deferred to rig. Resolution: the soft wall owns the type-P
compression side; the recovery cap stays type-D only.

## What Changes

- Gate the `sync_compression_recovery_active` feed cap (`sync.c` ~L2470) to
  `BUF_SENSOR_TYPE == 0`. Type-P compression-overfeed backoff is owned solely by
  the `psf_control_law` soft wall (Layer 2) + hard catch (Layer 3).
- Gate the continuous-compression collapse block (`sync.c` ~L2582) the same way —
  same mechanism, same ungated leak.
- No change to the latch *state* (`sync_compression_recovery_active` may still be
  set; it also feeds `baseline_update_on_settle` suppression at L1934 and the
  post-compression boost — those stay type-agnostic). Only the **feed cap** and
  **collapse trim** consumers are gated.
- No change to type-D behavior (the cap and collapse are unchanged for
  `BUF_SENSOR_TYPE == 0`).

## Capabilities

### Modified Capabilities
- `sync-feedback`: the shared `compression_recovery` feed cap and collapse trim
  are gated strictly to type-D, consistent with the existing
  estimator-compensation gating (D9). Type-P compression backoff is owned by the
  soft wall / hard catch.

## Impact

- `firmware/src/sync.c`: `sync_tick()` — wrap the L2470 recovery-cap block and
  the L2582 collapse block in `BUF_SENSOR_TYPE == 0`.
- Rig: re-run the compression-overfeed sweep (slow/print/fast feed into the
  compression zone) and confirm gradual soft-wall blend instead of a slam to
  682 sps; confirm no feed-starve hunting on transient dips.
- No NVM, protocol, or host changes.
