## 1. Delta push

- [ ] 1.1 In `klipper_syncer`, build the `SET_MMU` fields as an ordered `{KEY: formatted_str}` dict (same fields/formatting as today)
- [ ] 1.2 Track `last_pushed_fields`; compute `full = first push | force-resync | board-online transition`
- [ ] 1.3 Emit `SET_MMU` with all fields when `full`, else only changed fields; skip the push if the delta is empty (still emit `_FLARE_SYNC_BOARD` on board sync)
- [ ] 1.4 On successful POST, store the full current field snapshot as `last_pushed_fields`; on failure clear it to force a full push on recovery

## 2. Gate-state instrumentation

- [ ] 2.1 Add `FLARE_GATE_DEBUG` env-gated logging of `(ts, active_gate, in1,out1,in2,out2, gate_status_1,gate_status_2, tc_state)` when those inputs change; no-op when unset

## 3. Verify

- [ ] 3.1 `python3 -m py_compile scripts/flare_daemon.py`; `ruff check scripts/` clean
- [ ] 3.2 `python3 -m unittest discover -s scripts -p "test_*.py"` stays green (add/adjust a push-builder test if feasible)
- [ ] 3.3 Manual reasoning check: a full push and the equivalent delta sequence leave the mock in the same state
