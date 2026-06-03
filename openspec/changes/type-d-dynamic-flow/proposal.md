# type-d-dynamic-flow

## Why

`relay-neutral-frac-detune` shipped `SYNC_MIN_RATE 100` (quiet) with the
compression-side reserve bias (`SYNC_RESERVE_PCT 65`) handling slow-drift
anti-tension. HW captures confirm that holds: a slow benchy at min100/reserve65
rides the **compression** side (REGRESSION PASS, benign clicks), it does **not**
slowly drift to TENSION. (The earlier "slow lean to TENSION" was at the old
`reserve35` default; `reserve65` already corrected it.) So slow drift is solved.

The remaining defect is the **fast 5× step** (e.g. 300→1500 wall→infill). EST is
frozen between switch crossings, so the buffer drains mid-band→TENSION crossing no
switch — unobservable, the tension touch is the first signal and must be accepted
as a positioning/recalibration touch. But today it is not a *single* touch — it
is a **burst**.

Burst root cause (confirmed by HW capture A + the `7178c34` change): on a tension
touch, catchup `MM` slams to ~3000 (catchup magnitude is fine), but the
**post-recovery NEUTRAL feed is too low** because the tension-crossing EST update
is the *softened* no-travel fallback (`feed_avg × 1.15`, lowered from `1.5` in
`7178c34` to stop slow-drift overshoot). EST **creeps** up ~+200 sps/touch, taking
4-5 touches per infill entry to converge (`667→892→1074→1194→1382`, TPX→12), then
decays during the next slow wall so the following entry re-bursts. The same
softening that protects slow drift starves the fast-step recovery.

Fix the recovery, not the (already-solved) drift: **the tension-recovery direction
should respond aggressively, scaled by how the buffer reached the rail.** A fast
crossing has drain velocity → snap EST to the measured demand. Pinned re-touches
have zero velocity → the only signal is "another touch right away" → escalate
geometrically. Aggressive/geometric is correct *here* because overshoot is toward
COMPRESSION (benign) — the opposite of a NEUTRAL lean. (A proactive NEUTRAL
soft-wall was considered and **dropped**: reserve65 already compression-biases the
slow case, and capture B showed the dead-reckon `BP` stays frozen then jumps to
the rail with no gradual warning, so a position-based lean has no signal to act
on.)

## What Changes

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
- **Feed floor stays demoted (already shipped).** `SYNC_MIN_RATE 100` default; the
  compression reserve bias handles slow drift. No further change here — recorded
  so this change's spec covers the lever map.

## Success criteria

- **Fast step:** at most **one** TENSION touch per step (positioning/recalibration),
  no burst — EST is correct on the first touch so the recovery feed does not
  re-drain.
- No regression to the slow-drift overshoot that `7178c34` fixed (the gentle path
  still applies to genuinely slow / single / no-travel crossings).
- Slow prints continue to ride the compression reserve (no new TENSION drift).

## Non-Goals

- **Eliminating the fast-step TENSION touch.** A 2-switch buffer cannot foresee an
  unobservable mid-NEUTRAL demand step; one clean positioning touch is the target,
  not zero. True zero on fast steps still requires a high floor (loud) or type-P
  hardware.
- **No proactive NEUTRAL soft-wall lean.** Considered and dropped: reserve65
  already compression-biases the slow case, and the dead-reckon does not track slow
  drift (BP frozen then jumps), so a position-based lean has no usable signal.
- No type-P control-law change. All work scoped `BUF_SENSOR_TYPE == 0`; the type-P
  estimator / `psf_control_law` path stays byte-identical.
- No `SETTINGS_VERSION` bump (new knobs follow the non-persisted runtime pattern of
  `SYNC_COMPRESSION_DRAIN_FRAC` / `SYNC_EST_ATTACK_ALPHA`).
