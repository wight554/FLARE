## 1. H1 — holdable reserve target

- [x] 1.1 Add `#define SYNC_RESERVE_BIAS_POS_FRAC_CAP 0.10f`
- [x] 1.2 `buf_target_reserve_mm()`: `bias_pos = fminf(bias, cap)`;
  `target -= bias_pos * threshold`
- [x] 1.3 Confirm pct/center-guard terms and clamps unchanged

## 2. H2 — bounded trailing feed trim

- [x] 2.1 Add `#define SYNC_TRAILING_FEED_TRIM_MAX_SPS 120`
- [x] 2.2 After feed-target assembly: gate `s==BUF_MID &&
  reserve_error_mm > 0`; subtract `(bias/0.7)*MAX`, clamped `[0,MAX]`
- [x] 2.3 Confirm `reserve_error_mm` in scope and is `bp_eff -
  effective_target`

## 3. Validation

- [x] 3.1 `cmake --build build_local`
- [x] 3.2 `python3 -m py_compile scripts/*.py`
- [x] 3.3 `openspec validate holdable-reserve-target-feed-trim --strict`
- [x] 3.4 `TEST_CASES.md`: in-print buffer holds near RT with frequent
  crossings; `EST` does not freeze for seconds; no FAULT_HOLD cycle

## 4. Closeout

- [x] 4.1 Commit + push to main
- [ ] 4.2 On-Pi A/B retest; tune `SYNC_RESERVE_BIAS_POS_FRAC_CAP` if the
  buffer still dwells near the wall (pending hardware)
