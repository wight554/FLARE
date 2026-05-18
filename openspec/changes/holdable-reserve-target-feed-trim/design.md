## Context

Real-print logs (user pauses between captures; rows are during active
print, `AV:0.00` is normal for virtual-endstop mode — arm velocity sampled
only at switch crossings). Pattern: `RT=-6.24`, buffer parks `BP:-7.6…-7.8`,
`EST` frozen/hallucinated for seconds, FAULT_HOLD cycle, underextrusion.
Estimator only updates at switch crossings (`sync.c:722`); no external
extruder signal in standalone mode.

`buf_target_reserve_mm()`: `-(thr*pct) - thr*GUARD - bias*thr`
= `-2.73 - 0.39 - 3.12 = -6.24` (bias floored 0.4 by the B fix). The
`bias*thr` term (up to 5.46mm) is the depth that rides the fault wall and
starves crossings.

## Goals / Non-Goals

**Goals:** holdable target off the fault wall; estimator stays fresh via
frequent crossings; preserve never-ADVANCE trailing lean.

**Non-Goals:** no observer rework (user chose the holdable-target path);
no schedule/state-model change; no removal of B+C/F/G fixes.

## Decisions

### H1 — cap the bias position contribution

`bias_pos = min(bias, SYNC_RESERVE_BIAS_POS_FRAC_CAP=0.10)`;
`target -= bias_pos*threshold`. New `RT ≈ -2.73 - 0.39 - 0.78 = -3.90mm`
(~50% to wall, 3.9mm margin vs 1.56mm). Rationale: the parked depth, not
the pct/guard terms, caused both failures. The cap is the explicit reserve
depth knob; lower it on-hardware if still wall-riding. Degenerate parity:
when `bias ≤ cap` the target is unchanged.

### H2 — trailing lean as a bounded feed trim

After feed-target assembly, if `s==BUF_MID && reserve_error_mm > 0`
(buffer on the advance side of target), subtract
`(bias/0.7)*SYNC_TRAILING_FEED_TRIM_MAX_SPS` (≤120 sps ≈150mm/min).
Rationale: the never-ADVANCE intent becomes a gentle directional lean, not
parked depth. The advance-side + MID gate makes it strictly safe: it can
only pull toward the holdable target, self-disables at/below target, and
the tight cap is negligible vs the reserve P-authority, so it cannot
re-create the starvation seen with deep parking.

### H-degenerate — parity

1-point schedule with `bias ≤ 0.10`: H1 no-op (target identical to
pre-change). H2 only ever subtracts a small bounded value on the advance
side in MID; on the trailing side / non-MID it is inert.

## Risks / Trade-offs

- [Shallower RT → more advance-side excursions] → Intended: a holdable
  target with real control authority beats an unholdable deep one that
  fault-cycles. H2 + reserve P-term keep the equilibrium trailing-biased.
- [H2 always subtracts on advance side → slight steady under-feed] →
  Bounded ≤150mm/min and self-disabling at target; shifts equilibrium
  marginally trailing of `RT`, does not drive to the wall (P-authority and
  3.9mm margin dominate).
- [Cap value may be wrong on hardware] → It is a single `#define`; the
  proposal names it the primary tuning knob. Lower → shallower/safer.
- [Hardware-only validation] → No MMU on dev host. `TEST_CASES.md`
  regression: in-print buffer must hold near `RT` with frequent crossings,
  `EST` must not freeze for seconds, no FAULT_HOLD cycle.

## Migration Plan

1. Add `SYNC_RESERVE_BIAS_POS_FRAC_CAP`, `SYNC_TRAILING_FEED_TRIM_MAX_SPS`.
2. H1 cap in `buf_target_reserve_mm()`.
3. H2 gated trim after feed-target assembly.
4. Local build; `py_compile`; `openspec validate`.
5. `TEST_CASES.md` entry; on-Pi A/B; tune the cap if needed.

Rollback: revert the three edits; deep parked target returns.

## Open Questions

- Final `SYNC_RESERVE_BIAS_POS_FRAC_CAP` value — `0.10` is a starting
  point; on-hardware crossing frequency and underextrusion decide. Lower
  if the buffer still dwells near the wall.
