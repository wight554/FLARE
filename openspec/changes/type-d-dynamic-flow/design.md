# Design — type-d-dynamic-flow

## The two regimes, and why one knob can't serve both

```
                     observable?   right response        wrong response
slow NEUTRAL drift   YES (g_buf_pos EST tracks, so the    GENTLE proportional   high floor / geometric
toward TENSION        dead-reckon follows the drift)       lean; decay on        ramp → constant
                                                           recovery → quiet      COMPRESSION clicks
fast 5× step into    NO (EST frozen, drains mid-band→     accept 1 touch;       gentle 1.15× EST
TENSION               tension crossing no switch)          AGGRESSIVE recovery   recovery → burst of
                                                           (EST snap + catchup)  re-drain touches
```

The control principle (event-triggered / intermittent-observation control;
robust-MPC constraint back-off): **back off from the dangerous rail by a margin
sized to uncertainty, but close the loop so you stop when the threat clears — and
make the response aggressiveness asymmetric, because the cost is asymmetric**
(TENSION = print defect, COMPRESSION = benign noise). Leaning toward TENSION is
gentle; recovering from it is aggressive.

## Piece 1 — proactive NEUTRAL soft-wall lean (slow drift)

In the slow regime `g_buf_pos` is valid (steady demand → EST good → the
integrated position genuinely tracks). Lean proportional to the tension-side
excursion, toward a virtual wall inside the physical TENSION switch:

```
e = (-g_buf_pos) - SOFT_WALL_DEADBAND_MM          # tension-side excursion past deadband
if e > 0:  lean_target = K_SOFTWALL * e            # sps, P-term
else:      lean_target = 0
# feed the existing one-sided trim (clamped, leaked):
g_relay_neutral_trim_sps += slew(lean_target - g_relay_neutral_trim_sps)
```
- Reuses `g_relay_neutral_trim_sps` (§15): clamp `+SYNC_RELAY_TRIM_CLAMP_SPS`,
  in-NEUTRAL leak toward 0, apply path in `relay_control_law`. The §15
  TENSION-touch step stays as the hard backstop.
- Self-correcting: leans only while drifting; recovers → leak pulls it back →
  quiet. This is `psf_control_law`'s soft-wall (sync.c:2061) on the dead-reckon.
- Knob: `SYNC_NEUTRAL_SOFTWALL_GAIN` (K, sps per mm; 0 = disabled),
  `SYNC_NEUTRAL_SOFTWALL_DEADBAND_MM` (or reuse the reserve deadband).

Validity gate: only meaningful when the dead-reckon tracks (slow/steady). Fast
steps make `g_buf_pos` stale, but there the lean simply hasn't ramped yet (short
dwell) and the tension touch + Piece 2 take over — so no special gating needed
beyond the proportional form (small e → small lean).

## Piece 2 — velocity-scaled tension-crossing EST (fast-step burst)

The burst is post-recovery NEUTRAL feed too low because the tension-crossing EST
update is the softened `feed_avg × 1.15` (no-travel fallback, `7178c34`). Make the
aggressiveness scale with crossing velocity:

```
v = |arm_vel_mm_s| at the crossing            # = drain rate = deficit magnitude
v_norm = clamp(v / SYNC_TENSION_FAST_MM_S, 0, 1)

# demand revealed by the drain: extruder pulled = mmu_feed + drain_rate
demand_fast = mmu_feed_sps + drain_sps         # aggressive target (one-touch)
demand_slow = feed_avg_sps * 1.15              # existing gentle path (7178c34)

est_sample = lerp(demand_slow, demand_fast, v_norm)
alpha      = lerp(EST_ALPHA_MAX, 1.0, v_norm)  # fast crossing → full attack
blend_extruder_est_sps(est_sample, alpha)
```
- Fast crossing (`v` high) → EST snaps to the measured demand at full attack →
  the recovered NEUTRAL feed is correct → no re-drain → single touch.
- Slow crossing (`v` low) → existing gentle `1.15×` / clamped alpha → no overshoot
  (preserves the `7178c34` slow-drift fix). The softening is now *conditional*.
- Knob: `SYNC_TENSION_FAST_MM_S` (velocity at which the crossing counts as a hard
  step → full aggressiveness).
- When there is known switch-to-switch travel the existing `feed + drain` path
  (sync.c:1170) already computes `demand_fast`; this generalizes it to the
  no-travel / re-drain crossings that currently fall to the gentle `1.15×`.

## Piece 3 (optional) — velocity-scaled catchup ramp

Catchup magnitude is adequate (HW: `MM` hits ~3000). Marginal win: scale the
`SYNC_TENSION_RAMP_DELAY` collapse by `v_norm` so a fast crossing reaches max
feed a tick sooner. Aggressive/overshoot here is safe (toward COMPRESSION). Lower
priority than Piece 2 — ship only if the first-touch clearance is still slow.

## Piece 4 — demote the feed floor

`SYNC_MIN_RATE 1000` was the prior skip fix but is too loud for real prints. With
Piece 1 handling slow drift quietly, lower the default `SYNC_MIN_RATE` to a quiet
value (HW-tuned; candidate ~100-300). It stays a knob: operators wanting the
loud zero-fast-step-skip behavior can raise it. Document the trade in TUNING.md.

## Risks

- **Soft-wall on a drifting dead-reckon:** over a very long dwell `g_buf_pos`
  itself accumulates error. Mitigation: the lean is bounded (trim clamp) and
  biases toward the *safe* (COMPRESSION) side; worst case = a mild compression
  touch (benign), which also re-baselines the dead-reckon.
- **Velocity threshold tuning:** too low → every crossing treated as fast → slow
  drift re-overshoots (the `7178c34` regression returns); too high → bursts
  persist. `SYNC_TENSION_FAST_MM_S` must be HW-swept.
- **Two leans interacting** (soft-wall lean + §15 trim step): both push the same
  trim; the clamp + leak bound them. Verify they don't wind up.
- **Measurement noise:** live-print captures are noise-dominated — use a fixed
  stimulus (square-wave macro or identical sliced print) for A/B.

## HW validation gate (before/while implementing)

Capture a fast-step tension burst, confirm the diagnosis: between touch 1 and
touch 2, `MM` is HIGH (~3000, catchup fine) and `EST` is LOW/creeping (the `1.15×`
under-correction). If confirmed → Piece 2 is the fix. Also capture a slow-ish
print at low `SYNC_MIN_RATE`: does `BP` trend negative *gradually* before TENSION
(→ Piece 1 soft-wall works) or jump (→ dead-reckon not tracking even slow → fall
back to a time-since-crossing ramp).

## Scope / settings

All `BUF_SENSOR_TYPE == 0`. Type-P estimator / `psf_control_law` byte-identical.
New knobs (`SYNC_NEUTRAL_SOFTWALL_GAIN`, `SYNC_NEUTRAL_SOFTWALL_DEADBAND_MM`,
`SYNC_TENSION_FAST_MM_S`) follow the non-persisted runtime pattern of
`SYNC_COMPRESSION_DRAIN_FRAC` — no `SETTINGS_VERSION` bump. `SYNC_MIN_RATE`
default change is the only edit to a shared knob (lowering it is safe for type-P;
it was the type-D-specific raise that needed review).
