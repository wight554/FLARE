## 1. F1a — MID refill floor unconditional

- [x] 1.1 `sync_mid_anti_advance_floor_sps()`: remove the
  `!est_stale && !low_confidence && !est_below_floor` early-return; keep the
  `SYNC_ACTIVE`/`BUF_MID`/feed/no-fault/`reserve_error ≤ deadband` gate and
  the baseline-derived `assist_floor_sps`
- [x] 1.2 Confirm `est_stale`/`low_confidence`/`est_below_floor` locals are
  removed or unused without warnings

## 2. F1b — collapse cap floored in MID

- [x] 2.1 In the `sync_trailing_recovery_active` cap block, after
  `recovery_cap` is computed: `if (s == BUF_MID && recovery_cap <
  baseline_control_floor_sps()) recovery_cap = baseline_control_floor_sps();`
- [x] 2.2 Confirm `BUF_TRAILING` collapse/ramp/fault-hold branches unchanged

## 3. F2a — FAULT_HOLD recovery reseed

- [x] 3.1 In `sync_tick()` FAULT_HOLD recovery branch, before
  `cmd_event("SYNC","FAULT_HOLD_RECOVERY")`: if `BUF_SENSOR_TYPE==0` set
  `g_buf_pos = buf_target_reserve_mm()`
- [x] 3.2 Confirm no double-reset / ordering issue vs `sync_set_state(SYNC_OFF)`

## 4. F2b — bootstrap capped at baseline floor

- [x] 4.1 `sync_bootstrap_sps()`: clamp `res` to at most
  `baseline_control_floor_sps()` while still honoring `startup_floor_sps`
  lower bound
- [x] 4.2 Confirm `est_fresh` and fallback branches both respect the cap

## 5. Validation

- [x] 5.1 `cmake --build build_local`
- [x] 5.2 `python3 -m py_compile scripts/*.py`
- [x] 5.3 `openspec validate fix-sync-mid-starve-faulthold-oscillator --strict`
- [x] 5.4 Degenerate parity reasoning recorded: fresh-strong-est + buffer
  above target → no behavior change
- [x] 5.5 `TEST_CASES.md`: add hardware regression — long same-flow
  standalone print must not enter `MID→TRAILING→FAULT_HOLD→ADVANCE` loop;
  MID feed ≥ baseline floor below target; recovery does not slam ADVANCE

## 7. G1 — recovery resumes ACTIVE directly (post-retest)

- [x] 7.1 FAULT_HOLD recovery branch: reseed `g_buf_pos`=RT,
  `g_buf.state=BUF_MID`, `g_buf.entered_ms=now`, `sync_current_sps =
  sync_bootstrap_sps()`, `sync_set_state(SYNC_ACTIVE)`,
  `sync_auto_started=true`, tail-assist + `sync_idle_since_ms=0`; emit
  `FAULT_HOLD_RECOVERY` + `AUTO_START`
- [x] 7.2 No dependence on `s==BUF_ADVANCE` to re-arm

## 8. G2 — stalled-trailing bleed targets baseline floor (post-retest)

- [x] 8.1 `model_stalled_trailing` branch: `target_rate = max(lane_motion +
  margin, baseline_control_floor_sps())`
- [x] 8.2 Branch still gated to the pinned condition (self-terminating)

## 9. Re-validation

- [x] 9.1 `cmake --build build_local`
- [x] 9.2 `python3 -m py_compile scripts/*.py`
- [x] 9.3 `openspec validate fix-sync-mid-starve-faulthold-oscillator --strict`

## 6. Closeout

- [x] 6.1 Commit to main (firmware + OpenSpec + TEST_CASES.md)
- [x] 6.2 SUPERSEDED by `relay-buffer-control-2switch` (F1/F2 overridden by
  the relay override; shared infra G1/F2b/`sync_bootstrap_sps` still live).
  Analog re-verification reconciled by `audit-sync-polarity` (now archived).
