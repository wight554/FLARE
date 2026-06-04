## Context

Type-P feed apply (`sync.c`, after the control law) smooths the PD/wall target
with a distance-keyed EMA + slew (`g_psf_target_filt`, `SYNC_PSF_FILTER_MM`,
`SYNC_PSF_SLEW_PER_MM`) plus a wall-clock decay on the down side. This tames
compression-side bang-bang and the frozen-clock overfeed, but it equally throttles
the urgent refill (tension) direction.

`psf_control_law()` already sets `target = max_sps` once `|pos_norm| >
PSF_SOFT_WALL_START` on the tension side (Layer-2 soft wall). The problem is purely
that the feed-apply EMA filters that max target out over ~25 mm of travel.

## Goals / Non-Goals

**Goals:**
- Fast refill: feed reaches the soft-wall target within a tick when the buffer is
  starved into the tension wall.
- No compression overshoot after recovery: feed settles to the extruder rate.
- Keep compression/neutral smoothing untouched.

**Non-Goals:**
- Re-tuning `SYNC_PSF_FILTER_MM` / the estimator `alpha` (would also affect the
  smooth side). The snap is surgical to the tension wall.
- Type-D changes.

## Decisions

### D1 — Snap only in the tension wall, only when feeding up

Gate: `BUF_SENSOR_TYPE == 1 && buf_pos_norm() < -CONF_PSF_SOFT_WALL_START &&
target_sps > sync_current_sps`. Outside the wall, or when feed should drop, the
normal smoothing/decay path runs. The wall is exactly where refill is urgent and a
brief overfeed is safe.

### D2 — Snap to the wall target, seed the filter at demand

`sync_current_sps = target_sps` (= `max_sps` in the wall) gives immediate refill.
But `g_psf_target_filt` must be seeded at `extruder_est_sps` (demand), not
`target_sps`: when the buffer climbs out of the wall the smoothing resumes from
`g_psf_target_filt`, so seeding it at max kept feed pinned high and overshot into
compression (observed `BS:COMPRESSION 0.71`). Seeding at demand eases feed to the
extruder rate on exit (observed: peak drops to `+0.47`, in the goal band).

*Alternative rejected*: leave `g_psf_target_filt` frozen at its pre-snap value —
risks a down-collapse (re-starve) on exit if it was low. Demand is the correct
settling point.

*Alternative rejected*: shorten `SYNC_PSF_FILTER_MM` globally — speeds refill but
reintroduces compression bang-bang the EMA was added to kill.

## Risks / Trade-offs

- **Under-shoot if `extruder_est_sps` lags low** → feed eases too far on exit and
  the buffer drifts back toward tension. Rig showed it settling in the goal band,
  not under-shooting; if a faster move exposes it, seed slightly above demand
  (`extruder_est × k`, k ≈ 1.2).
- **Snap engages only at `-PSF_SOFT_WALL_START` (−0.8)** → a startup transient can
  still dip to ~−0.68 before the snap arms (benign, no `cannot_refill`). Lowering
  `PSF_SOFT_WALL_START` would arm it earlier if needed.

## Open Questions

- Should the snap arm earlier (lower `PSF_SOFT_WALL_START`, or a separate refill
  threshold) to catch the startup tension dip? Rig: the dip is benign so far.
- Does the demand seed need a `×1.2` margin under even faster moves? Re-check at
  higher F.
