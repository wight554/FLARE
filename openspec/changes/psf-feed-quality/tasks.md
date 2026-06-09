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

- [ ] 3.1 **Root cause narrowed by audit-hardening-fixes code audit (2026-06-10)** —
  live-recurrence wait no longer needed; bench repro below. Decision tree closed by
  code reading:
  - Every *visible* BS failure replies `ER` (`ER:BUSY` protocol.c:1636/1659,
    `ER:BUF_STAB_UNAVAILABLE` on `false` return protocol.c:1671-1673). The
    "check `L1T/L2T/TC` active task" hypothesis is **ruled out** — task-active
    paths are never silent.
  - No-op with `OK` = `buffer_stabilize_start_internal` returned **true without
    arming**. Exactly two reachable silent-success paths, both BEFORE the
    `BUF_STAB:START` emit (sync.c:245-246, unconditional once armed):
    1. **Type-P presence gate (sync.c:197-207) — prime suspect.**
       `pick_boot_stabilize_lane()` (sync.c:514-516) returns the ACTIVE lane
       unconditionally when set — never falls through to OUT-sensor checks. Active
       lane with empty IN+OUT → `return true` → `OK`, no motion, no START. Matches
       hand-loading incident: filament in flux on the *other* lane, buffer at −0.4,
       gate inspects wrong lane's sensors.
    2. Raw-NEUTRAL skip (sync.c:221-222) — secondary; −0.4 vs compression-side goal
       should classify TENSION, unlikely here.
  - Caveat: `BUF_STAB:START` is budget-limited best-effort (8/100 ms shared window);
    during hand-loading event chatter a real START can drop on the wire — absence of
    START alone is weak evidence, require "no motion" too. (audit F6 fixes fault-class
    events only; START stays droppable.)
- [ ] 3.2 **Bench repro** (replaces live-recurrence wait): set active lane = empty
  lane (`T:n`), hand-feed other lane until `BP ≈ −0.4`, send `BS` while streaming
  events. Expect `OK` + no `BUF_STAB:START` + no motion → confirms presence-gate
  path.
- [ ] 3.3 **Fix** (on confirmed repro): make presence gate / `pick_boot_stabilize_lane`
  pick the lane that actually has filament (IN or OUT) instead of blind active-lane;
  or return `false` (→ `ER:BUF_STAB_UNAVAILABLE`) instead of silent `true` so the
  operator sees the refusal. Predict/stagnant guards (old 3.2/3.3 hypotheses) only
  if bench repro instead shows START-but-no-move: predict-reached first-tick guard
  or `g_stab_stagnant_since_ms` init — currently NOT implicated.

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

- [ ] 5.1 Only the **BS no-op (section 3)** remains open — root cause narrowed to
  silent pre-arm early-return (presence gate, prime suspect) by the
  audit-hardening-fixes code audit; bench repro 3.2 replaces the live-recurrence
  wait. Hunting / auto-start items accepted per the real print. Archive once 3.x
  is resolved.
