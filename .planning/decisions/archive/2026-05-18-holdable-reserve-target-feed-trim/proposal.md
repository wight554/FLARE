## Why

Three rounds of fixes (F1a/F1b/F2a/F2b/G1/G2) each corrected a real
downstream bug but the in-print limit cycle persisted. Real-print hardware
logs isolate the structural root:

In standalone virtual-endstop mode the **only** ground-truth observation of
extruder consumption is a buffer switch crossing (`sync.c:722`); there is no
external extruder signal. The reserve target `RT≈-6.24mm` parked the buffer
**1.56mm from the -7.80 TRAILING fault wall**. That deep parked target did
two fatal things simultaneously:

1. **Starved the observer** — the buffer dwelt near the wall for seconds
   with no switch crossings, so `extruder_est_sps` froze/hallucinated
   (`172 → 357 → 1439`), and `feed ≈ EST + reserve_correction` then
   starved or slammed.
2. **Rode the fault edge** — 1.56mm margin + a coarse switch-only observer
   + 40 sps/tick ramp cannot hold that band → relaxation oscillation →
   repeated FAULT_HOLD → underextrusion.

The never-ADVANCE / trailing-bias intent does not require parking *on* the
trailing wall; it can be a modest feed-rate lean around a holdable target.

## What Changes

- **H1 — holdable reserve target.** Cap the bias contribution to the
  reserve *position* target at `SYNC_RESERVE_BIAS_POS_FRAC_CAP` (0.10),
  moving `RT` from `-6.24mm` to `≈-3.90mm` (~50% toward trailing, 3.9mm
  fault margin vs 1.56mm). The buffer now oscillates gently around a
  holdable point with frequent switch crossings that keep the estimator
  fresh.
- **H2 — trailing feed trim.** Carry the never-ADVANCE lean as a small,
  tightly-capped feed reduction (`SYNC_TRAILING_FEED_TRIM_MAX_SPS` 120,
  ≈150mm/min), gated to `BUF_MID` and only on the advance side of the
  reserve target (`reserve_error_mm > 0`). It can only nudge toward the
  holdable target — never deepen past it, never starve.
- No change to: the B+C floors, F1/F2/G1/G2 fixes (still valid), full
  `BUF_TRAILING` braking/fault-hold, schedule format, the 5-state model.
- `SYNC_RESERVE_BIAS_POS_FRAC_CAP` is the new primary on-hardware tuning
  knob for reserve depth.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sync-refactor`: the reserve target must remain holdable off the fault
  wall so the switch-crossing estimator stays fresh; trailing bias is a
  bounded feed-rate lean, not parked position depth.

## Impact

- Firmware: `firmware/src/sync.c` — `buf_target_reserve_mm()` (H1 cap),
  feed-target assembly (H2 gated trim), two new `#define` constants. No
  config schema change.
- No host/script/state-format change. Degenerate (1-point schedule, bias
  ≤ cap) reserve target is unchanged by H1; H2 only subtracts on the
  advance side in MID.
- OpenSpec: `openspec/specs/sync-refactor/spec.md` on archive.
- Hardware A/B retest pending on the Pi; `SYNC_RESERVE_BIAS_POS_FRAC_CAP`
  may need on-device tuning (shallower if still wall-riding).
