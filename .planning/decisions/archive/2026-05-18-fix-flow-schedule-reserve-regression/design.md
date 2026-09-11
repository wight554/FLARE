## Context

`flow-keyed-param-schedule` (archived `2026-05-18`) replaced two
`max(schedule_or_live, config)` guarantees with raw schedule
interpolation:

- `buf_target_reserve_mm()` line ~253: `bias = flow_param(est_sps)
  .bias_milli/1000` (was constant `SYNC_TRAILING_BIAS_FRAC`).
  `target -= bias·threshold`. Below the first breakpoint `flow_param`
  clamps to the lowest schedule point.
- `baseline_control_floor_sps()` line ~860: returns
  `flow_param(est_sps).baseline_sps` (was
  `max(g_baseline_sps, g_baseline_target_sps)`). Feeds ADVANCE recovery
  gain `baseline_ref·2` (line ~960).

`threshold = BUF_HALF_TRAVEL_MM = 7.8 mm`, `SYNC_RESERVE_PCT = 35`,
`SYNC_TRAILING_BIAS_FRAC = 0.4`. Reserve cushion from the bias term: was
`3.12 mm`, becomes `~1.25 mm` at a low-flow-clamped weak breakpoint —
~1.87 mm shallower, ~24% of total half-travel. Result: startup/low-flow
buffer parks shallow → surge → ADVANCE pin → underextrusion, with
weakened recovery gain prolonging it.

User intent (restated, confirmed): never hit ADVANCE; smoother motion;
less TRAILING↔MID bang-bang. Learning/smoothing the feed is wanted; the
reserve safety cushion must not shrink.

## Goals / Non-Goals

**Goals:**
- Reserve depth never below the configured scalar, regardless of
  schedule/flow/segment.
- ADVANCE recovery gain never below the configured baseline.
- Keep flow-keyed baseline and per-segment live learning (additive-up).

**Non-Goals:**
- No schedule-format / analyzer / state-model / RELIEF_PAUSE /
  FAULT_HOLD change.
- No new config tunables.
- Not removing flow-keyed bias — only flooring it.

## Decisions

### D1 — B: reserve bias is a floored quantity

In `buf_target_reserve_mm()` the effective bias SHALL be
`max(SYNC_TRAILING_BIAS_FRAC, schedule_bias)` before
`target -= bias·threshold`. The schedule may deepen reserve (bias above
the scalar) but never reduce it below the scalar. Rationale: bias encodes
the safety cushion that implements "never ADVANCE"; it is a floor, not a
free variable. Alternative (decouple entirely, schedule = baseline only —
option A) rejected by the user: keep the flow-keyed-bias upside, just make
it strictly safe.

### D2 — C: restore the baseline control floor max()

`baseline_control_floor_sps()` SHALL return
`max(flow_param(est_sps).baseline_sps, g_baseline_target_sps)`,
restoring the pre-rework guarantee that the control floor (and thus the
ADVANCE recovery gain `baseline_ref·2`) never drops below the configured
persistent baseline. Rationale: a deep cushion (D1) still recovers slowly
if the gain term collapsed; both schedule-sourced terms must share the
"may strengthen, never weaken" property or the symptom only half-fixes.

### D3 — Degenerate parity preserved

For a 1-point schedule whose values equal the config scalars, `max()`
returns the scalar → behavior byte-identical to pre-flow-keyed. Multi-
point schedules: bias clamped up to ≥ scalar, baseline floor ≥ config.
No new tunables; reuse `SYNC_TRAILING_BIAS_FRAC` and
`g_baseline_target_sps`.

### D4 — This is the corrected statement of the full-bias invariant

The session repeatedly asserted "full-bias invariant untouched". It was
violated once bias became schedule-sourced. `reserve-safety-floor` is the
honest, testable restatement: schedule and live learning are
**monotone-strengthening** on reserve depth and baseline floor; neither
may reduce them below config.

## Risks / Trade-offs

- [Schedules intending lower bias at high flow can no longer do so] →
  Intended: lowering the safety cushion was never legitimate; high flow
  can still raise baseline. If a genuine need for shallower high-flow
  reserve emerges, it is a separate, deliberate change with its own
  justification — not a silent clamp artifact.
- [Two-line firmware change in hot path] → Two `max()` (one float, one
  int); negligible cost; no per-tick allocation.
- [Hardware-only validation] → No MMU here; add explicit
  `TEST_CASES.md` regression (startup/low-flow must not pin ADVANCE,
  multi-point schedule reserve ≥ scalar) for the next on-device run.

## Migration Plan

1. `buf_target_reserve_mm()`: bias = `max(SYNC_TRAILING_BIAS_FRAC,
   fp.bias_milli/1000)`.
2. `baseline_control_floor_sps()`: `max(fp.baseline_sps,
   g_baseline_target_sps)`.
3. Local build + host regression; degenerate-parity check (1-point ==
   pre-flow-keyed).
4. `TEST_CASES.md` hardware regression entry.

Rollback: revert the two edits; raw schedule interpolation returns.

## Open Questions

- Should the live per-segment ratchet delta also be floored into the
  same `max()` (it already only adds upward, so likely a non-issue) —
  default: leave as-is; revisit if on-device data shows segment-delta
  interacting with the floor.
