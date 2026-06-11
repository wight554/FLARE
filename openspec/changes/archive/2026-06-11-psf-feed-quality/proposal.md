## Why

Type-P sync is now fault-free on normal extrudes (archived `psf-stale-fault-timers`,
`psf-tension-refill-snap`), but two quality issues remain open and are not yet
tracked anywhere active. This change is the tracker for them — to be validated /
resolved against a real print rather than 150 mm bench bursts.

1. **Feed-tracking hunting + end-burst overshoot.** A `G1 E150 F1200` burst swings
   the buffer `−0.84 ↔ +0.79` and overfeeds to `+1.0` COMPRESSION → `RELIEF_PAUSE`
   at burst end. The tension-refill snap drives `max_sps` for ~20 mm/s demand
   (~1.8× overfeed) → overshoot → smoothing drops feed → back to tension → snap
   again = limit cycle. The big swings are **transient** (burst start = snap
   overshoot, burst stop = overfeed); mid-extrude steady-state is tighter
   (`−0.14 … +0.02`). Real prints feed continuously, so this may be a bench
   artifact — confirm on a real print before tuning.

2. **`BS` no-op from mid-tension (intermittent).** From `BP −0.43` (control TENSION
   zone, `CF 1.0`, not saturated) a manual `BS` returned `OK` but the buffer did
   not move; a second `BS` drove it to goal `+0.41`. Suspected cause: the
   predict-early-stop (`predicted = g_buf_pos + SYNC_STAB_PREDICT_LEAD_S *
   g_vel_norm_f`) firing `DONE` instantly when residual `g_vel_norm_f` makes
   `predicted >= goal`, or a one-shot `STAGNANT`. Unconfirmed — no event captured,
   no repro yet.

## What Changes

- No firmware change committed yet — this is a tracking + investigation change.
- Candidate fixes (gated on real-print evidence):
  - Hunting: moderate the snap target to `extruder_est × ~1.3` instead of
    `max_sps`; and/or enable a small `KD_PSF` (needs jitter check, currently P-only).
  - `BS` no-op: gate the predict-reached so it cannot fire on the first tick / when
    `g_vel_norm_f` is stale; or require a minimum drive before `DONE`.

## Capabilities

### Modified Capabilities
- `psf-type-p-sensor`: adds a feed-quality acceptance target (steady tracking near
  goal, reliable single-shot `BS`) to be met on a real print.

## Impact

- Investigation only until evidence; likely `firmware/src/sync.c` (snap target,
  predict-reached guard) if pursued.
- Needs a real-print telemetry capture to decide.
