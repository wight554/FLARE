## 1. Real-print evidence (gates everything)

- [x] 1.1 **Rig**: real print (few minutes) + end pause. PASS — no `FAULT_HOLD` /
  `cannot_refill` the entire print; `SM:1 ST:1` throughout. Buffer hunts
  `≈ −0.5 … +0.6` (compression-biased ~+0.2), feed surges `MM 100↔2000`, but
  **never saturates mid-print** → extruder always fed → flow unaffected (hunting is
  mechanical surge/noise, not a flow defect in this trace). End-of-print: extruder
  stops → buffer rides to `+1.0` as feed decays to floor → `RELIEF_PAUSE` → settles
  to goal `+0.40` (correct Layer-3 behavior; brief, feed already at floor). The
  fault-timer fixes hold under real printing.

  Open: the hunting amplitude is benign for flow here but mechanically active
  (motor surge). Pursue 2.x only if a print *surface* shows buffer-correlated
  artifacts; the cleanest damp is a small `KD_PSF` (2.2).

## 2. Feed hunting / end-burst overshoot

- [x] 2.1 **ACCEPTED — no action.** Multi-pause real print came out clean (operator
  confirmed surface); the compression-biased hunting never saturates mid-print and
  does not affect flow. Snap-moderation not needed.
- [x] 2.2 **ACCEPTED — `KD_PSF` stays 0.** No buffer-driven artifacts on the printed
  surface, so no derivative damping required. Revisit only if a future print shows
  banding tracking the buffer swings.
- [x] 2.3 **CONFIRMED.** End-burst `+1.0` → `RELIEF_PAUSE` occurs only at genuine
  full stops (pauses / end of print), recovers to goal, and re-arms correctly on
  resume across repeated pauses. Not a mid-print issue.

## 3. BS no-op from mid-tension

- [ ] 3.1 **Capture**: reproduce `BS` no-op while streaming events. Could NOT repro
  via `MV → BS` from mid-tension (BS drove cleanly to goal even from `BP −0.45`/`−0.03`
  with residual velocity) — so predict-on-residual-velocity is **ruled out**. The
  original no-op happened **during hand-loading, settled at `−0.4`** (lane/presence
  in flux). New lead: the cause is likely an **early-return before arming**
  (controller-not-idle / presence / lane), not predict/stagnant. KEY QUESTION to
  capture next time: **does the no-op `BS` emit `BUF_STAB:START`?** No START →
  early-return (check `L1T/L2T/TC` for an active load/preload task at that moment);
  START-but-no-move → predict/stagnant.
- [ ] 3.2 If `DONE` instantly: guard the predict-reached so it cannot fire on the
  first tick after `START`, or require a minimum off-start displacement before
  honoring `reached` (residual `g_vel_norm_f` makes `predicted >= goal` spuriously).
- [ ] 3.3 If `STAGNANT`: the desaturated stagnant window is firing before the drive
  registers — re-check `g_stab_stagnant_since_ms` init / the 0.03 threshold at this
  position.

## 4. Auto-start trigger sensitivity

- [x] 4.1 **ACCEPTED — `-0.6` gate kept.** Real print: auto-start re-armed correctly
  on every pause-resume (buffer dives past `-0.6` under demand → `SM:1 ST:1`). The
  threshold is not too conservative in practice; no late-engage starvation observed.
- [x] 4.2 **No action.** No easier trigger needed (4.1). The `-0.6` + velocity gate
  is doing its job without the boot/idle spurious auto-sync the D18 + stale-timer
  fixes exist to prevent. Revisit only if a real workflow shows it engaging late.
- [x] 4.3 **CONFIRMED.** Stabilize/`BS` parks at goal `+0.40` (not `-0.4`) in normal
  operation, so the shallow-tension band is not the resting point. (The `-0.4`
  resting case ties to the BS no-op below, not the auto-start gate.)

## 5. Closeout

- [ ] 5.1 Only the **BS no-op (section 3)** remains open — needs a captured repro
  (DONE vs STAGNANT) before a fix. Hunting / auto-start items accepted per the real
  print. Archive once 3.x is resolved.
