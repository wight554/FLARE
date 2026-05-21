## Context

Type-D standalone (`BUF_SENSOR_TYPE == 0`) is a 2-switch buffer (TENSION /
COMPRESSION microswitches, NEUTRAL between). `extruder_est_sps` is the only
demand proxy and is derived from buffer-arm motion at switch crossings;
`g_buf_pos` between crossings is pure dead reckoning from `(est − mmu)`.

The relay law (`firmware/src/sync.c`, around the `BUF_SENSOR_TYPE == 0` block):

```
TENSION     -> baseline_floor * RELAY_CATCHUP_FRAC
COMPRESSION -> SYNC_MIN_SPS            (forward feed, ~100 sps)
NEUTRAL     -> extruder_est_sps * RELAY_NEUTRAL_FRAC, clamped
```

The only thing that stops feed while pinned in COMPRESSION is the
continuous-compression block (`sync.c`, `sync_continuous_compression_since_ms`):
after `SYNC_AUTO_STOP_MS` (5000 ms) of continuous COMPRESSION with
`sync_current_sps <= floor`, it fires `RELIEF_PAUSE`.

Captured fast-purge telemetry (the damage case):

| metric | value |
|---|---|
| time pinned in COMPRESSION before RELIEF_PAUSE | ~4.9 s (`CT` 57→4962 ms) |
| forward feed into the full buffer | ~8 mm (`SYNC_RELIEVE_MM` 2→10) |
| `BP` during pin | -5.0 → -5.60 (deepening past the -5 switch) |
| `cannot_relieve` warn (`CONF_SYNC_CANNOT_RELIEVE_MM = 50 mm`) | never fired |

So the bowden break is not estimator freeze — it is ~5 s of `SYNC_MIN` forward
feed into a full buffer with no extruder draw.

## Goals / Non-Goals

**Goals:**

- Stop forward feed into a full, non-relieving buffer fast enough to keep the
  overfill within a small budget (~1-2 mm), down from ~8 mm / ~5 s.
- Keep normal type-D operation intact: the relay limit cycle legitimately
  touches COMPRESSION every cycle, and the extruder draws the buffer back. The
  fix must not starve or stall that cycle.
- Type-P analog behavior byte-identical.

**Non-Goals:**

- Mid-band estimator correction of any kind (see Alternatives — rejected).
- Klipper→firmware extruder-velocity feedforward.
- Changing TENSION or NEUTRAL relay behavior.

## Decisions

### D1: COMPRESSION commands true stop (0), not `SYNC_MIN_SPS` forward

When the buffer is pinned in COMPRESSION, feeding any forward rate adds filament
to a full buffer. A true 0 is strictly safer than `SYNC_MIN`: if the extruder is
drawing (normal cycle) the buffer still leaves COMPRESSION via the draw — the
MMU does not need to feed; if the extruder is idle (purge pause) 0 means no
overfill. So COMPRESSION → 0.

Open sub-question: keep a brief `SYNC_MIN` only during the very first moments of
COMPRESSION entry (to avoid chattering the motor enable) then drop to 0, vs 0
immediately. Lean: 0 immediately, paired with the fast-brake already on
`TENSION→COMPRESSION`.

Alternative — keep `SYNC_MIN` but shorten the auto-stop timer: rejected, still
overfills (SYNC_MIN × shortened_time) and leaves a forward bias into the wall.

### D2: Demand-aware relief — stop on overfill budget, not a blind 5 s timer

Replace / supplement the `SYNC_AUTO_STOP_MS` blind timer with an overfill-budget
trip: while pinned in COMPRESSION, accumulate `g_sync_relieve_effort_mm` (already
tracked); if it exceeds a small budget (~1-2 mm) while `BP` is not recovering
(staying at/deepening past `-threshold`), enter `RELIEF_PAUSE` immediately. This
caps damage to the budget regardless of timer. Lower / repurpose the 50 mm
`CONF_SYNC_CANNOT_RELIEVE_MM` accordingly.

With D1 (COMPRESSION = 0) the relieve effort barely accumulates, so D2 is mostly
a backstop, but it also covers any residual ramp-down feed.

### D3: `relay_min_flip_mm` interaction (deadlock guard)

Prior history: a non-zero `relay_min_flip_mm` with `COMPRESSION = SYNC_MIN` once
froze the relay flips. The supported value is `relay_min_flip_mm: 0.0`
(time-only), per `TUNING.md` — the earlier `0.5` was a stale value in a local
(gitignored, since-removed) `config.ini`; tuning is done live on the device. At
`0.0` the flip is time-based and does not depend on MMU travel, so a zero
COMPRESSION feed cannot deadlock the flip-out. Constraint for this change:
COMPRESSION = 0 is only safe while `relay_min_flip_mm` stays `0.0`; if a non-zero
min-flip is ever re-enabled, the flip-out-of-COMPRESSION must be made exempt from
the travel guard (key on the physical NEUTRAL crossing instead).

## Risks / Trade-offs

- [COMPRESSION = 0 freezes the flip out of COMPRESSION under `relay_min_flip_mm
  = 0.5`] → Confirm flip-out keys on the physical NEUTRAL crossing / exempt it
  from the travel guard; test the normal relay cycle still flips. (D3)
- [Relief trips too eagerly during the normal compression touch of the relay
  cycle → premature RELIEF_PAUSE / underfeed] → Budget on relieve effort + BP
  not recovering, not on mere COMPRESSION contact; the normal cycle leaves
  COMPRESSION quickly via extruder draw before the budget accrues.
- [Motor disable/enable chatter from frequent 0 feed in the normal cycle] →
  Hold motor enabled at 0 sps rather than toggling; reuse existing fast-brake
  zero-feed handling.
- [Type-P divergence] → All changes gated to `BUF_SENSOR_TYPE == 0`.

## Migration Plan

Firmware behavior change; a relief-budget tunable may be added (no `settings_t`
struct change unless a new persisted key is required — if so, bump
`SETTINGS_VERSION` per repo rule). One milestone commit. Rollback = revert.
Validate with `scripts/flare_purge_check.py`: purge A/B must PASS (overfill
within budget, no slam grind), regression C on a normal print must show
unchanged TENSION/NEUTRAL cycling and no premature RELIEF_PAUSE.

## Alternatives considered

### Rejected: NEUTRAL-band demand-collapse estimator corrector (the prior change)

The `neutral-demand-collapse-brake` change tried to decay `extruder_est_sps`
mid-NEUTRAL when `g_buf_pos` slid toward COMPRESSION, so the relay NEUTRAL target
would back off before the wall. Implemented (commits `3614851`, `a333b6b`) and
reverted (`5969c74`) after hardware showed EST frozen and the buffer still
slamming. Why it cannot work in type-D:

- `g_buf_pos` between switches is dead-reckoned from `(est − mmu)`. In NEUTRAL
  `mmu = est × RELAY_NEUTRAL_FRAC`, so `BP` slides at exactly `−0.25 × est` — the
  slide is the relay's built-in compression lean and carries **no real-demand
  information**.
- Normal type-D operation is a limit cycle that hits COMPRESSION every cycle, so
  a purge collapse and a normal glide are **indistinguishable mid-band**. Any
  corrector strong enough to help the purge also fires during normal printing.
- The arrest-the-drift target self-cancels: `target = mmu / relay_frac − margin
  ≈ est − margin`, decaying est ~0.2 sps/tick — far too slow for the ~1.4 s
  glide.

Conclusion: a 2-switch type-D buffer has no mid-band ground truth; correction
must happen at the switch crossings, which is what this change does.

## Open Questions

Resolved in implementation:

- D1: COMPRESSION = 0 immediately (motor held enabled at 0).
- D2: dedicated `relay_compression_relief_mm` tunable added (default 1.5 mm).
- D3: relies on `relay_min_flip_mm: 0.0` (time-only, the supported value); a
  non-zero min-flip would need the flip-out travel-guard exemption.

Remaining: hardware validation (purge A/B + normal-print regression C).
