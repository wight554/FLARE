## Why

After the sync rework, `flow-keyed-param-schedule` re-sourced **two**
control terms from the flow schedule and dropped their pre-existing
`max(.., config)` floors:

- Reserve bias: `buf_target_reserve_mm()` now uses
  `flow_param(extruder_est_sps).bias_milli/1000` instead of the constant
  `SYNC_TRAILING_BIAS_FRAC` (0.4). Below the first schedule breakpoint
  (print start, low-flow walls, post-travel) `flow_param` clamps to the
  lowest breakpoint, whose bias is typically ~0.16–0.39 for a
  high-flow-derived schedule.
- Baseline control floor: `baseline_control_floor_sps()` now returns raw
  `flow_param(...).baseline_sps` instead of
  `max(g_baseline_sps, g_baseline_target_sps)`.

With `threshold = BUF_HALF_TRAVEL_MM = 7.8 mm`, the reserve cushion from
the bias term drops from `0.4·7.8 = 3.12 mm` to `~0.16·7.8 = 1.25 mm` —
**~1.87 mm (~24% of total half-travel) of reserve lost** exactly at
startup/low flow. The buffer parks too shallow, a normal extruder surge
drains it to ADVANCE, and the simultaneously-weakened ADVANCE recovery
gain (`baseline_ref·2`, also schedule-clamped low) makes it sit there for
seconds → underextrusion. This is the exact failure the full-bias design
existed to prevent, reintroduced.

The intended invariant was always: feed rate (baseline) may learn / vary
with flow; the **reserve safety cushion must never shrink below config**.
The implementation lost that distinction.

## What Changes

- **B — bias floor:** the reserve bias used by `buf_target_reserve_mm()`
  SHALL be `max(SYNC_TRAILING_BIAS_FRAC, schedule_bias)`. The schedule may
  only **deepen** reserve, never make it shallower than the configured
  scalar.
- **C — baseline floor restored:** `baseline_control_floor_sps()` SHALL be
  `max(flow_param(...).baseline_sps, g_baseline_target_sps)` (the
  pre-rework `max(.., config)` guarantee), so ADVANCE recovery gain never
  collapses below the configured baseline.
- Net: schedule/learning still flow-keyed and additive-up; both
  schedule-sourced control terms become "may strengthen, never weaken
  below config". Smoothing/learning retained; the safety floor restored.
- No change to: schedule format, analyzer emission, the 5-state model,
  RELIEF_PAUSE/FAULT_HOLD, live-learner ratchet direction.

## Capabilities

### New Capabilities
- `reserve-safety-floor`: the corrected full-bias invariant — schedule
  and live learning may only strengthen reserve depth and baseline floor,
  never reduce either below the configured scalar; the explicit floor
  formulas for the bias and baseline control terms.

### Modified Capabilities
<!-- behavior tightening; expressed as the new reserve-safety-floor
     capability rather than rewriting flow-keyed-schedule deltas -->

## Impact

- Firmware: `firmware/src/sync.c` — `buf_target_reserve_mm()` bias floor;
  `baseline_control_floor_sps()` restore `max(.., target)`. ~2 small
  edits, no new tunables (reuses `SYNC_TRAILING_BIAS_FRAC`,
  `g_baseline_target_sps`).
- No host/script/firmware-state-format change. Behavior parity for a
  1-point degenerate schedule is unchanged; multi-point schedules can
  only now hold equal-or-deeper reserve than the scalar.
- Hardware-validated regression (buffer must not pin ADVANCE at
  startup/low flow) added to `TEST_CASES.md`.
