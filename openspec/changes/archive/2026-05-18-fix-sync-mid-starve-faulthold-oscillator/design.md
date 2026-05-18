## Context

`stabilize-sync-mid-reserve-control` shipped B+C reserve floors, safe
defaults, and `sync_mid_anti_advance_floor_sps()`. Hardware logs (its task
3.4) show the loop still oscillates. Telemetry (sign: `BP>0`=ADVANCE,
`BP<0`=TRAILING, walls ±7.80, `RT≈-6.24`):

- MID samples: `BP≈-7.7`, `RE≈-1.5`, `EST` collapsed `1217→483`, buffer
  pinned at the trailing wall while state is still MID, then TRAILING,
  relief, repeat → FAULT_HOLD.
- After 5s: `FAULT_HOLD_RECOVERY` → state OFF → `AUTO_START` on a
  dead-reckoned fake ADVANCE → `RE:14.04 AV:11.10` → ADVANCE pin → 6s
  `SYNC_ADVANCE_DWELL_STOP_MS` → FAULT_HOLD. ~14 cycles, `ADV_RISK_HIGH`.

## Goals / Non-Goals

**Goals:**
- MID holds feed near the learned baseline and refills toward `RT` instead
  of starving at the trailing wall when the estimator is fresh-but-low.
- FAULT_HOLD recovery cannot re-arm into an ADVANCE overshoot.

**Non-Goals:**
- No change to reserve target depth (deep `RT` is the intentional
  never-ADVANCE bias; the user rejected softening it).
- No change to B+C floors, `BUF_TRAILING` braking/collapse/fault-hold,
  schedule format, 5-state model.
- No new config tunables.

## Decisions

### F1a — MID anti-advance floor is unconditional below target

`sync_mid_anti_advance_floor_sps()` SHALL return the baseline-derived
assist floor whenever `g_sync_state==SYNC_ACTIVE`, `s==BUF_MID`, the active
lane is `TASK_FEED`/`FAULT_NONE`, and `reserve_error_mm ≤
reserve_deadband_mm` (buffer at/below target). The
`!est_stale && !low_confidence && !est_below_floor` early-return is
removed: a fresh-but-collapsed estimator was the exact failure, so estimator
freshness/confidence must not gate the refill floor. The floor remains
derived from `baseline_control_floor_sps() * SYNC_MID_ANTI_ADVANCE_FLOOR_FRAC`
(not the estimator), so it cannot itself drive ADVANCE.

### F1b — trailing-recovery collapse cap floored in MID

In the `sync_trailing_recovery_active` cap block, after `recovery_cap` is
computed, `if (s == BUF_MID && recovery_cap < baseline_control_floor_sps())
recovery_cap = baseline_control_floor_sps();`. The collapse cap exists to
brake an *over-advanced* buffer draining through TRAILING; while still in
MID it must not pull feed below the learned baseline. Full collapse
behavior is unchanged once `s == BUF_TRAILING`.

### F2a — FAULT_HOLD_RECOVERY reseeds the virtual buffer

When `sync_tick()` transitions out of `SYNC_FAULT_HOLD` on
`CONF_SYNC_FAULT_HOLD_RECOVERY_MS`, set `g_buf_pos =
buf_target_reserve_mm()` before emitting `FAULT_HOLD_RECOVERY` (virtual
endstop mode only, `BUF_SENSOR_TYPE==0`). This discards the fictional
ADVANCE the dead-reckoned model accumulates with feed=0 during hold, so
`AUTO_START` re-arms only on a genuine ADVANCE.

### F2b — bootstrap capped at baseline floor

`sync_bootstrap_sps()` SHALL clamp its result to at most
`baseline_control_floor_sps()` (still respecting the existing lower
`startup_floor_sps`). A post-recovery start ramps from the learned
baseline; the normal control law then raises feed only as the real buffer
demands, eliminating the bootstrap ADVANCE slam.

### F-degenerate — parity

With a fresh strong estimator and buffer above target, F1a adds no floor
(gated by `reserve_error ≤ deadband`); F1b only raises a cap that was
already below baseline; F2a/F2b act solely on the recovery/bootstrap path.
Single-point schedule + healthy estimator behavior is unchanged.

### G1 — recovery resumes ACTIVE directly (post-retest)

Hardware retest of F1–F2 showed F2a reseeds `g_buf_pos` but not
`g_buf.state`; `AUTO_START` (gated `s==BUF_ADVANCE`) still fired on the
stale latched state. Reseeding state to MID and *waiting* for a real
ADVANCE would deadlock mid-print (sync OFF + extruder pulling drains to
TRAILING, never ADVANCE). Decision (user: "direct resume"): on
`FAULT_HOLD_RECOVERY` reseed pos+state to MID at RT and re-enter
`SYNC_ACTIVE` directly at the F2b-capped bootstrap. No ADVANCE-event
dependence — eliminates both the fake-advance slam and the starve
deadlock.

### G2 — stalled-trailing bleed targets the baseline floor (post-retest)

`sync.c` model-stalled-trailing recovery bled the estimator only toward
`lane_motion + 6 sps`. While pinned, `lane_motion ≈` the collapsed rate,
so the feedforward self-cancelled (~150 mm/min net) and the buffer never
climbed the reserve deficit before the next disturbance — the dominant
"MID never refills" cause behind F1a being insufficient. Decision: bleed
toward at least `baseline_control_floor_sps()` (the historically healthy
feed). The branch is gated to the pinned condition, so it self-terminates
the instant the buffer leaves the wall — no ADVANCE overshoot.

## Risks / Trade-offs

- [F1a/F1b hold more feed in MID near the trailing wall] → Intended; the
  floor is baseline-derived and only active below target, and full braking
  resumes the instant the buffer reaches `BUF_TRAILING`. It cannot push
  ADVANCE because it never exceeds the learned baseline.
- [F2a discards model state on recovery] → The model was provably wrong
  (fake ADVANCE with feed=0); reseeding to `RT` is strictly more correct
  than carrying drift. A real advance still re-arms normally.
- [Hardware-only validation] → No MMU on the dev host. Add a `TEST_CASES.md`
  regression: long same-flow standalone print must not enter the
  `MID→TRAILING→FAULT_HOLD→ADVANCE` loop; MID feed ≥ baseline floor below
  target; recovery does not slam ADVANCE.

## Migration Plan

1. F1a: remove the freshness/confidence early-return in
   `sync_mid_anti_advance_floor_sps()`.
2. F1b: floor `recovery_cap` at `baseline_control_floor_sps()` when
   `s == BUF_MID`.
3. F2a: reseed `g_buf_pos` to `buf_target_reserve_mm()` on
   FAULT_HOLD_RECOVERY (virtual mode).
4. F2b: clamp `sync_bootstrap_sps()` ≤ `baseline_control_floor_sps()`.
5. Local `cmake --build build_local`; `py_compile`; `openspec validate`.
6. `TEST_CASES.md` hardware regression entry; on-Pi A/B retest.

Rollback: revert the four edits; the gated floor + collapse cap return.

## Open Questions

- Whether `AUTO_START` should additionally require N consecutive real
  ADVANCE ticks (debounce) — deferred; F2a removes the fake-advance source,
  so a single-tick debounce is likely unnecessary. Revisit if on-Pi logs
  still show spurious AUTO_START.
