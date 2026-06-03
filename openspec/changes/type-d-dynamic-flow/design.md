# Design — type-d-dynamic-flow

## Scope: fast-step tension burst only (slow drift already solved)

HW captures settled the regimes:

```
                     status        handled by
slow NEUTRAL drift   SOLVED        SYNC_RESERVE_PCT 65 compression bias — slow
toward TENSION                     benchy rides COMPRESSION (REGRESSION PASS),
                                   does NOT drift to TENSION. No new work.
fast 5× step into    BROKEN        this change — Piece 2 (velocity snap + burst
TENSION                            escalation). Burst of re-drain touches today.
```

A proactive NEUTRAL soft-wall lean was considered and **dropped** (see "Dropped:
Piece 1"). Control principle for what remains (event-triggered / intermittent-
observation control; robust-MPC constraint back-off): the tension-recovery
direction is the *safe* overshoot direction (toward COMPRESSION = benign), so
respond **aggressively**, scaled by how the buffer reached the rail. Recovering
from TENSION is aggressive; there is no NEUTRAL lean to keep gentle.

## Piece 2 — velocity snap + consecutive-tension escalation (the fix)

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

## Feed floor (already demoted; no new work)

`SYNC_MIN_RATE` default is already `100` (shipped in `relay-neutral-frac-detune`):
quiet, with `SYNC_RESERVE_PCT 65` compression-biasing the slow case. A high floor
remains the operator opt-in for the loud zero-fast-step-skip mode. Recorded here
so this change's spec carries the type-D lever map; no code change.

## Dropped: Piece 1 (proactive NEUTRAL soft-wall lean)

Originally planned a P-term lean on the dead-reckoned tension-side excursion to
correct slow drift. **Dropped after capture B:**
- `SYNC_RESERVE_PCT 65` already compression-biases slow prints — the slow benchy
  rode COMPRESSION (REGRESSION PASS), no TENSION drift. The drift the soft-wall
  targeted does not occur at the shipped default.
- The dead-reckon gives no usable signal anyway: capture B showed `BP` frozen at
  `+3.25` (feed == EST → net 0 → integrator stalls) right up to a **sudden** jump
  to `−5.00`. A position-proportional lean sees `+3.25` (compression side) →
  computes zero tension excursion → does nothing. The rare residual TENSION
  touches are sudden/step-driven (Piece 2's domain), not gradual.

So a position soft-wall is both unnecessary (reserve handles it) and inert (no
gradual signal). The `SYNC_NEUTRAL_SOFTWALL_*` knobs are not added.

## Risks

- **Velocity threshold tuning:** too low → every crossing treated as fast → slow
  drift re-overshoots (the `7178c34` regression returns); too high → bursts
  persist. `SYNC_TENSION_FAST_MM_S` must be HW-swept.
- **Escalation windup:** the burst escalator + §15 TENSION-touch step both push
  the trim/EST up; bound by the trim clamp + demand ceiling + the burst reset on a
  held NEUTRAL dwell. Verify it converges and resets, not winds up.
- **Measurement noise:** live-print captures are noise-dominated — use a fixed
  stimulus (square-wave macro or identical sliced print) for A/B.

## HW validation gate

Capture A (fast-step burst) already confirmed the diagnosis: `MM` ~3000 (catchup
fine), `EST` creeping ~+200 sps/touch over 4-5 touches/entry, first crossing
`AV −2.7..−4.6 mm/s`, re-touches `AV 0`. → Piece 2 (velocity snap + burst
escalation) is the fix. Post-implementation, re-run capture A and confirm the
burst collapses to ~1 touch; sweep `SYNC_TENSION_FAST_MM_S` / `_BURST_MS` /
escalation ratio.

## Scope / settings

All `BUF_SENSOR_TYPE == 0`. Type-P estimator / `psf_control_law` byte-identical.
New knobs (`SYNC_TENSION_FAST_MM_S`, `SYNC_TENSION_BURST_MS`,
`SYNC_TENSION_ESC_STEP_SPS`, `SYNC_TENSION_ESC_RATIO`) follow the non-persisted
runtime pattern of `SYNC_COMPRESSION_DRAIN_FRAC` — no `SETTINGS_VERSION` bump. No
shared-knob or feed-floor change in this change (the floor was already demoted).

## Implementation plan — 2026-06-03

### firmware/src/sync.c
- Add type-D-only burst state (`last_tension_ms`, `burst_n`) beside existing
  tension-risk state and reset it in `sync_disable`.
- In `buf_update`, replace the `BUF_NEUTRAL -> BUF_TENSION` split between
  with-travel `feed_avg + drain_sps` and no-travel `feed_avg * 1.15` with a
  velocity lerp: `feed_avg * 1.15` at `v_norm=0`, measured demand at
  `v_norm=1`, and an explicit raise-only blend alpha that can reach `1.0` on
  TENSION crossings.
- Add geometric burst escalation on the unconditional
  `BUF_NEUTRAL -> BUF_TENSION` crossing hook, after any estimator blend, because
  fast pinned re-touches are too short to pass the demand-sample dwell gates.
  Clamp to `GLOBAL_MAX_SPS`.
- Reset the burst count once a `BUF_NEUTRAL` dwell has held beyond the burst
  window. Risk: slow single crossings must remain exactly the gentle
  `feed_avg * 1.15` endpoint.

### firmware/include/controller_shared.h + firmware/src/settings_store.c
- Declare and load-reset four non-persisted runtime knobs, following
  `SYNC_COMPRESSION_DRAIN_FRAC`; do not alter `settings_t` or
  `SETTINGS_VERSION`.
- Risk: defaults must come from generated `CONF_*` macros, not header-only C.

### firmware/src/protocol.c + scripts/flare_cmd.py
- Add `SET:`/`GET:` handlers and live-tune-lock coverage for the four knobs.
- Add dump entries so host live dumps preserve protocol parity.
- Risk: parameter names must fit the fixed parser width and return stable
  decimal/integer formats.

### scripts/gen_config.py + config.ini + config.ini.example
- Add defaults and generated macros for all four knobs.
- Risk: config defaults and generated `tune.h` must remain synchronized.

### MANUAL.md + BEHAVIOR.md + TUNING.md
- Document tension-recovery velocity snap and burst escalation, including that a
  fast-step touch is reduced to a positioning touch, not eliminated.
- Note `SYNC_MIN_RATE` remains quiet and reserve bias handles slow drift.

## Pivot (2026-06-03): decaying recovery floor replaces burst escalation

HW fix3→fix7 proved the Piece 2 burst escalation cannot collapse the burst:
even ungated from `sample_valid`, with `BURST_MS 3000` and raise-only blend, EST
oscillates `~780↔~1080`, TPX→12-13. The escalation pushes `extruder_est_sps`, but
the recovery-cycle NEUTRAL-fill / COMPRESSION-drain crossings re-estimate the
genuinely-low wall demand (~766) and pull EST back down between touches → the
re-drain persists. No EST-side mechanism wins, because between steps the demand
really is low.

Replacement: a **feed-side decaying floor**, independent of EST.
- On `NEUTRAL→TENSION`: `g_tension_floor_sps = SYNC_TENSION_RECOVERY_FLOOR`,
  `g_tension_floor_set_ms = now`.
- NEUTRAL relay feed: `target = max(target, floor·(1 − age/RECOVERY_MS))` while
  `age < SYNC_TENSION_RECOVERY_MS`.
Because it is not an EST term, the fill/drain samples can't lower it → NEUTRAL
feed stays high through recovery → buffer can't re-drain → burst collapses to one
touch. It decays (hands off to EST as the estimator converges), so it is loud
only ~1.5 s after each touch — quiet between steps, unlike a static high
`SYNC_MIN_RATE`.

Accepted scope (operator): touch-1 per step is structural (no anticipation); a
brief COMPRESSION pulse during the recovery window is acceptable for fast
recovery. Knobs: `SYNC_TENSION_RECOVERY_FLOOR` (≈2400 mm/min), `RECOVERY_MS`
(≈1500). The §1.2 escalation + its knobs are removed; velocity-snap raise-only
(§1.1) may stay as a benign nudge. See tasks §5.
