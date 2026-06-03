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

## Piece 2 — velocity snap + consecutive-tension escalation (fast-step burst)

HW capture A confirmed the mechanism and refined the gate. On every tension
touch `MM=3000` (catchup magnitude is fine); the burst is `EST` **creeping**
~+200 sps/touch (`667→892→1074→1194→1382`, TPX climbing to 12), taking 4-5
touches per infill entry to converge to the real demand (~1300-1400), then
decaying back to ~670 during the next slow wall so the following infill entry
re-bursts. Two distinct crossing types drive it:

- **First drain crossing** of a cluster: `AV ≈ −2.7 .. −4.6 mm/s` (drain velocity
  present) — the demand is *measurable*.
- **Re-touches**: `AV = 0.00` (buffer pinned at the rail) — no drain velocity, so
  a pure velocity gate misses them. The signal that they are still under-fed is
  simply **another tension touch right after the last one**.

So the fix needs two triggers:

```
# Trigger 1 — first crossing with drain velocity: snap to measured demand
v = |arm_vel_mm_s|;  v_norm = clamp(v / SYNC_TENSION_FAST_MM_S, 0, 1)   # AV ~2.7-4.6 → thr ~2
demand_fast = mmu_feed_sps + drain_sps          # measured demand
demand_slow = feed_avg_sps * 1.15               # existing gentle path (7178c34)
est_sample  = lerp(demand_slow, demand_fast, v_norm)
alpha       = lerp(EST_ALPHA_MAX, 1.0, v_norm)  # fast → full attack (bypasses ALPHA_MAX clamp)
EST = max(EST, blend(est_sample, alpha))

# Trigger 2 — consecutive tension within T_burst: GEOMETRIC escalation
if (now - last_tension_ms) < SYNC_TENSION_BURST_MS:
    burst_n += 1
    EST += SYNC_TENSION_ESC_STEP_SPS * pow(SYNC_TENSION_ESC_RATIO, burst_n)   # clamp to demand cap
else:
    burst_n = 0                                  # reset after a held NEUTRAL dwell
last_tension_ms = now
```

- **Trigger 1** catches the first crossing (velocity present) → EST snaps to the
  measured `mmu_feed + drain_rate` → recovered NEUTRAL feed correct → ideally one
  touch. Generalizes the existing with-travel `feed+drain` (sync.c:1170) to the
  no-travel case that currently falls to the gentle `1.15×`.
- **Trigger 2** handles the `AV=0` re-touches the velocity gate can't see: each
  consecutive tension touch within `T_burst` proves EST is still low, so escalate
  the EST jump **geometrically** until tension clears. Collapses the 4-5 touch
  creep into ~1-2. **Aggressive/geometric is correct here** — this is the
  *tension-recovery* direction (overshoot → COMPRESSION = safe), the exact
  opposite of the NEUTRAL lean (Piece 1) where geometric overshoots into clicks.
  Same instinct, correct location.
- Slow single drift (one touch, no recent prior) → `v_norm≈0`, `burst_n=0` → the
  gentle `1.15×` path, no escalation → preserves the `7178c34` slow-drift fix.
  The softening is now *conditional on not being in a burst*.
- Knobs: `SYNC_TENSION_FAST_MM_S` (≈2 mm/s), `SYNC_TENSION_BURST_MS` (≈300-500),
  `SYNC_TENSION_ESC_STEP_SPS` / `SYNC_TENSION_ESC_RATIO` (≈1.5-2, capped at a
  demand ceiling). `burst_n` resets when a NEUTRAL dwell holds past `T_burst`.

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
