# type-d-dynamic-flow

## Why

`relay-neutral-frac-detune` ended with a working but blunt type-D config: a high
feed floor (`SYNC_MIN_RATE 1000`) prevents the slow→fast step skip by holding
feed up through slow walls. HW follow-up shows the floor is **too loud for real
(non-test) prints** — it overfeeds every slow section into constant COMPRESSION
clicks. Dropping the floor (`SYNC_MIN_RATE 100`) is quiet but the buffer then
**slowly leans toward TENSION** on slow-ish prints.

These are two different failure modes that the single floor knob cannot serve
together, because they live in different observability regimes:

- **Slow drift (real slow print):** long NEUTRAL dwell, EST tracks demand, the
  dead-reckoned `g_buf_pos` genuinely follows the drift → **observable**. A
  gentle, proportional lean can correct it without constant clicks.
- **Fast 5× step (e.g. 300→1500 wall→infill):** EST is frozen between crossings,
  the buffer drains mid-band→TENSION crossing no switch → **unobservable**. The
  tension touch is the first signal; it must be accepted as a positioning/
  recalibration touch. But today it is not a *single* touch — it is a **burst**.

Burst root cause (confirmed from session logs + the `7178c34` change): on a
tension touch, catchup `MM` slams to ~3000 (catchup magnitude is fine), but the
**post-recovery NEUTRAL feed is too low** because the tension-crossing EST update
is the *softened* no-travel fallback (`feed_avg × 1.15`, lowered from `1.5` in
`7178c34` to stop slow-drift overshoot). EST creeps up only ~15% per touch, so it
takes two-plus touches to converge to the real demand → re-drain → another touch.
The same softening that helps slow drift starves the fast-step recovery.

The unifying insight: **ramp aggressiveness should be asymmetric by direction and
scaled by crossing speed.** Leaning toward TENSION in NEUTRAL must be gentle
(overshoot → COMPRESSION → clicks). Recovering *from* TENSION must be aggressive
(overshoot → COMPRESSION is the safe direction, and it kills the re-drain). The
crossing velocity (`arm_vel_mm_s` / dwell) measures the demand deficit and tells
the controller which regime it is in and how hard to respond.

## What Changes

- **Proactive NEUTRAL soft-wall lean (slow-drift fix).** When the dead-reckoned
  `g_buf_pos` drifts toward the TENSION side in NEUTRAL, add a *proportional*
  feed lean (P-term toward a virtual tension wall), fed through the existing
  one-sided trim (`g_relay_neutral_trim_sps`, §15). It decays toward zero as the
  buffer recovers, so it leans only while actually drifting → quiet. This is the
  type-P `psf_control_law` soft-wall ported to type-D's dead-reckon, gated to the
  slow/observable regime.
- **Tension-crossing EST: velocity snap + consecutive-tension escalation
  (fast-step burst fix).** HW capture A confirmed catchup is strong (`MM=3000`)
  and the burst is EST *creeping* ~+200 sps/touch over 4-5 touches per infill
  entry. Two triggers: (1) a crossing with drain velocity snaps EST to the
  measured demand `mmu_feed + drain_rate` at full attack; (2) consecutive tension
  touches within a burst window — the only signal for pinned `AV=0` re-touches —
  escalate the EST jump **geometrically** until tension clears, then reset after a
  held NEUTRAL dwell. A single slow crossing keeps the gentle `× 1.15` path, so the
  `7178c34` softening becomes *conditional on not being in a burst*. (Geometric
  escalation is correct in this tension-recovery direction — overshoot toward
  COMPRESSION is the safe side — the exact opposite of the NEUTRAL lean.)
- **Optional: velocity-scaled catchup ramp.** Collapse `SYNC_TENSION_RAMP_DELAY`
  toward 0 for fast crossings so the first touch clears a tick sooner. Secondary
  to the EST snap (catchup magnitude is already adequate).
- **Demote the feed floor.** With the soft-wall handling slow drift quietly,
  `SYNC_MIN_RATE` is no longer the primary anti-tension lever. Lower its default
  to a quiet value; it remains available for operators who prefer the loud
  zero-fast-step-skip behavior. Final default HW-tuned.

## Success criteria

- **Slow-ish real print:** no slow TENSION drift at a *low* `SYNC_MIN_RATE`, and
  COMPRESSION clicks markedly rarer than the `SYNC_MIN_RATE 1000` config (quiet).
- **Fast step:** at most **one** TENSION touch per step (positioning/recalibration),
  no burst — EST is correct on the first touch so the recovery feed does not
  re-drain.
- No regression to the slow-drift overshoot that `7178c34` fixed (the gentle path
  still applies to genuinely slow/no-travel crossings).

## Non-Goals

- **Eliminating the fast-step TENSION touch.** A 2-switch buffer cannot foresee an
  unobservable mid-NEUTRAL demand step; one clean positioning touch is the target,
  not zero. True zero on fast steps still requires a high floor (loud) or type-P
  hardware.
- No type-P control-law change. All work scoped `BUF_SENSOR_TYPE == 0`; the type-P
  estimator / `psf_control_law` / soft-wall path stays byte-identical.
- No `SETTINGS_VERSION` bump (new knobs follow the non-persisted runtime pattern of
  `SYNC_COMPRESSION_DRAIN_FRAC` / `SYNC_EST_ATTACK_ALPHA`).
