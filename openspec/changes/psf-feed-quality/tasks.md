## 1. Real-print evidence (gates everything)

- [ ] 1.1 **Rig**: run a real print (or a continuous-feed sequence, not 150 mm
  burst-then-stop). Stream `g_buf_pos` + events. Confirm: no `FAULT_HOLD` /
  `cannot_refill` mid-print; record steady-state buffer band and any
  surface artifacts that correlate with `BS:TENSION`/`COMPRESSION` swings.

## 2. Feed hunting / end-burst overshoot

- [ ] 2.1 If 1.1 shows buffer-driven artifacts: moderate the tension-refill snap —
  target `extruder_est × ~1.3` instead of `max_sps` (`sync.c` snap branch). Re-test;
  watch for re-starvation if the demand estimate lags.
- [ ] 2.2 Alternatively / additionally: enable a small `KD_PSF` to damp swing
  velocity. Measure ADC jitter first (was deferred P-only); size Kd against it.
- [ ] 2.3 End-burst overshoot to `+1.0` → `RELIEF_PAUSE`: confirm it only occurs at
  genuine full stops (not mid-print). If it bites at pauses, check the wall-clock
  decay (`SYNC_PSF_DECAY_SPS_PER_S`) drops feed fast enough on demand-stop.

## 3. BS no-op from mid-tension

- [ ] 3.1 **Capture**: reproduce `BS` from mid-tension (`BP ~ −0.4`, not saturated)
  while streaming events — determine whether the no-op `BS` emits `BUF_STAB:DONE`
  (predict-early-stop) or `STAGNANT_TIMEOUT`.
- [ ] 3.2 If `DONE` instantly: guard the predict-reached so it cannot fire on the
  first tick after `START`, or require a minimum off-start displacement before
  honoring `reached` (residual `g_vel_norm_f` makes `predicted >= goal` spuriously).
- [ ] 3.3 If `STAGNANT`: the desaturated stagnant window is firing before the drive
  registers — re-check `g_stab_stagnant_since_ms` init / the 0.03 threshold at this
  position.

## 4. Closeout

- [ ] 4.1 Resolve or consciously accept each item; archive.
