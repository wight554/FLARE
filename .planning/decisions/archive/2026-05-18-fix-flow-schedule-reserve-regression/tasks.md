## 1. B — reserve bias floor

- [x] 1.1 `firmware/src/sync.c` `buf_target_reserve_mm()`: compute `schedule_bias = (float)fp.bias_milli/1000.0f`, then `bias = clamp_f(fmaxf((float)SYNC_TRAILING_BIAS_FRAC, schedule_bias), 0.0f, 0.7f)` before `target -= bias·threshold`
- [x] 1.2 Confirm no other reserve/zone_bias/soft-wall/collapse code changed

## 2. C — baseline control floor

- [x] 2.1 `baseline_control_floor_sps()`: return `max(flow_param(extruder_est_sps).baseline_sps, g_baseline_target_sps)` (restore the pre-rework `max(.., config)`)
- [x] 2.2 Verify `g_baseline_target_sps` is the correct persistent-config symbol in scope here (matches pre-flow-keyed semantics)

## 3. Validation

- [x] 3.1 Local `cmake --build build_local`; `scripts/validate_regression.sh`
- [x] 3.2 Degenerate parity: 1-point schedule == config scalars → reserve target and control floor byte-identical to pre-flow-keyed (host calc or instrumented check)
- [x] 3.3 Multi-point synthetic: low-flow clamp endpoint with bias < scalar → effective bias == scalar (not the weak value); baseline floor == config when schedule baseline < config
- [x] 3.4 `TEST_CASES.md`: add hardware regression — startup + low-flow features must not pin ADVANCE; multi-point schedule reserve depth ≥ scalar; ADVANCE recovery not sluggish

## 4. Closeout

- [x] 4.1 `openspec validate fix-flow-schedule-reserve-regression --strict`
- [x] 4.2 No host/script/state-format diff; firmware code limited to the two `sync.c` edits; docs/OpenSpec tracking updated; `git status` clean after commit

## Validation Notes

- 2026-05-18: Host C check confirmed degenerate scalar bias remains bit-equal,
  weak schedule endpoints floor to the scalar bias/baseline, and stronger
  schedule values still pass through.
- 2026-05-18: `cmake --build build_local` passed.
- 2026-05-18: `bash scripts/validate_regression.sh` passed.
- 2026-05-18: `openspec validate fix-flow-schedule-reserve-regression --strict`
  passed.
- 2026-05-18: Hardware validation still pending; added
  `pending-manual-hardware: Flow-Schedule Reserve Safety Floor` to
  `TEST_CASES.md`.
