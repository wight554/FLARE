## Why

Hardware test of `stabilize-sync-mid-reserve-control` (task 3.4) fails. With
the B+C reserve floors, safe defaults, and the gated MID anti-advance floor
all shipped, the standalone sync loop still collapses into a self-sustaining
`MID(deep-trailing) → TRAILING → FAULT_HOLD → recovery → ADVANCE-pin(6s) →
FAULT_HOLD` oscillator (~14 cycles, `ADV_RISK_HIGH`). The "mid-trailing
biasing" the design intends is gone: the buffer parks at the trailing wall
in MID and never refills.

Two coupled root causes, both downstream of the (correct) deep reserve
target `RT≈-6.24mm` (≈80% toward the `-7.80` trailing wall):

1. **MID cannot climb out of deep reserve.** When the estimator collapses
   (`EST 1217→483`) the trailing-recovery collapse cap
   (`sync.c:1480-1493`, `target ≤ est − kp_window − collapse_trim`) starves
   feed below the rate needed to refill the buffer back to `RT`. The shipped
   `sync_mid_anti_advance_floor_sps()` does not bite — its gate requires a
   stale or low-confidence estimator, but here `CF≈1.0` and the estimator is
   fresh, merely low-valued. So MID quietly drains to the trailing wall and
   re-faults.

2. **FAULT_HOLD recovery is an ADVANCE oscillator.** During hold, feed=0 but
   the dead-reckoned virtual buffer keeps integrating the extruder estimate
   and drifts to a fictional ADVANCE. `AUTO_START` (gated `s==BUF_ADVANCE`,
   `sync.c:1171`) then fires on that fake advance and `sync_bootstrap_sps()`
   slams high feed → real ADVANCE pin → 6s dwell → FAULT_HOLD again.

## What Changes

- **F1a — MID refill floor unconditional.** `sync_mid_anti_advance_floor_sps()`
  applies the baseline-derived feed floor whenever the buffer is in `BUF_MID`,
  the active lane is feeding without fault, and the buffer is at/below the
  reserve target (`reserve_error ≤ deadband`). Remove the
  stale/low-confidence/est-below-floor precondition that currently disables it
  when the estimator is fresh but collapsed.
- **F1b — collapse cap cannot starve MID.** In the trailing-recovery cap
  block, floor `recovery_cap` at `baseline_control_floor_sps()` while
  `s == BUF_MID`. Full collapse braking still applies once actually in
  `BUF_TRAILING`.
- **F2a — FAULT_HOLD recovery reseeds the virtual buffer.** On
  `FAULT_HOLD_RECOVERY`, reset `g_buf_pos` to the reserve target so the
  dead-reckoned model does not carry a fictional ADVANCE; `AUTO_START` then
  only fires on a *real* advance.
- **F2b — bootstrap cannot slam ADVANCE.** Cap `sync_bootstrap_sps()` at
  `baseline_control_floor_sps()` so a post-recovery start ramps from the
  learned baseline instead of overshooting into ADVANCE.
- No change to: reserve target depth (intentional, never-ADVANCE intent),
  the B+C floors, full `BUF_TRAILING` braking/fault-hold, schedule format,
  the 5-state model.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sync-refactor`: standalone MID reserve control must refill the buffer
  back toward the reserve target instead of starving at the trailing wall,
  and FAULT_HOLD recovery must not re-arm into an ADVANCE overshoot.

## Impact

- Firmware: `firmware/src/sync.c` — `sync_mid_anti_advance_floor_sps()`
  (F1a), trailing-recovery cap block (F1b), FAULT_HOLD_RECOVERY branch
  (F2a), `sync_bootstrap_sps()` (F2b). No new tunables.
- No host/script/state-format change. Degenerate (single-point schedule,
  fresh strong estimator) behavior unchanged: F1a only adds a floor when
  buffer ≤ target; F1b only raises a cap that was below baseline; F2a/F2b
  only act on the recovery path.
- OpenSpec: `openspec/specs/sync-refactor/spec.md` on archive.
- Hardware retest (`flare_cmd.py "?:" --poll 500`) pending on the Pi —
  no MMU on the dev host.
