## 1. Relay control core

- [x] 1.1 Add `SYNC_RELAY_CATCHUP_FRAC` / `BACKOFF_FRAC` / `MID_FRAC`
- [x] 1.2 Override `target_sps` before the ramp/clamp, gated
  `BUF_SENSOR_TYPE == 0`: TRAILING→catch-up, ADVANCE→back-off, MID→hold
- [x] 1.3 Confirm ramp, `[SYNC_MIN,max]` clamp, `trailing_floor`,
  fast-brake, relief logic still run after the override

## 2. Disarm FAULT_HOLD on normal switch contact

- [x] 2.1 Gate advance-dwell FAULT_HOLD to `BUF_SENSOR_TYPE != 0`
- [x] 2.2 Gate `trailing_wall_critical` FAULT_HOLD to `BUF_SENSOR_TYPE != 0`
- [x] 2.3 Confirm RELIEF_PAUSE / continuous-trailing auto-stop unchanged

## 3. Validation

- [x] 3.1 `cmake --build build_local`
- [x] 3.2 `python3 -m py_compile scripts/*.py`
- [x] 3.3 `openspec validate relay-buffer-control-2switch --strict`
- [x] 3.4 Analog parity reasoning: `BUF_SENSOR_TYPE != 0` path unchanged
- [x] 3.5 `TEST_CASES.md`: 2-switch relay regression entry

## 4. Closeout

- [x] 4.1 Commit + push to main
- [ ] 4.2 On-Pi A/B: tune `SYNC_RELAY_*_FRAC` for a slow, shallow,
  never-ADVANCE, never-faulting cycle (pending hardware)
