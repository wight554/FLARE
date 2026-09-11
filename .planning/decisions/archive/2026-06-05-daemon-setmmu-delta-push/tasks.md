## 1. Delta push

- [x] 1.1 In `klipper_syncer`, build the `SET_MMU` fields as an ordered `{KEY: formatted_str}` dict (same fields/formatting as today)
- [x] 1.2 Track `last_pushed_fields`; compute `full = first push | force-resync | board-online transition`
- [x] 1.3 Emit `SET_MMU` with all fields when `full`, else only changed fields; skip the push if the delta is empty (still emit `_FLARE_SYNC_BOARD` on board sync)
- [x] 1.4 On successful POST, store the full current field snapshot as `last_pushed_fields`; on failure clear it to force a full push on recovery
  - 2026-06-05: implemented in `flare_daemon.py` `klipper_syncer`; `force_full` set on 10s resync, board-online, and first push.

## 2. Gate-state instrumentation

- [x] 2.1 Add `FLARE_GATE_DEBUG` env-gated logging of `(ts, active_gate, in1,out1,in2,out2, gate_status_1,gate_status_2, tc_state)` when those inputs change; no-op when unset
  - 2026-06-05: logs to stderr only when the gate tuple changes and the flag is set.

## 3. Verify

- [x] 3.1 `python3 -m py_compile scripts/flare_daemon.py`; `ruff check scripts/` clean
  - 2026-06-05: compile OK; `ruff check` All checks passed. A pure push-builder unit test was not added — the field build is embedded in the network loop; extracting it is a separate refactor. The unittest suite + reasoning check below cover no-regression.
- [x] 3.2 `python3 -m unittest discover -s scripts -p "test_*.py"` stays green
  - 2026-06-05: 46/46 green.
- [x] 3.3 Manual reasoning check: a full push and the equivalent delta sequence leave the mock in the same state
  - 2026-06-05: each success stores the full current snapshot in `last_pushed_fields`; a delta omits only fields whose formatted value equals what Klipper already holds, and `cmd_SET_MMU` keeps absent params -> the mock converges to the same full state as a full push. Force-resync (10s) / board-online / first-push send the full set for restart recovery.
