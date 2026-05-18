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

## 5. Polarity fix (post-retest)

- [x] 5.1 Hardware showed inverted polarity (ADVANCE=empty,
  TRAILING=full per FLARE convention). Swap: ADVANCE→catch-up,
  TRAILING→back-off
- [x] 5.2 MID lean flipped to overfeed (`MID_FRAC` 0.97 → 1.05) toward
  the full/TRAILING reserve side (never starve)
- [x] 5.3 Skip legacy `trailing_floor` in relay mode (it force-raised
  feed in TRAILING, defeating back-off)
- [x] 5.4 `cmake --build build_local`; `py_compile`; `openspec validate`

## 6. Demand-tracked MID (post-retest 2)

- [x] 6.1 Hardware showed MID↔TRAILING bangbang + full-wall (-11) slam:
  MID anchored to fixed baseline (~5× real demand). Re-anchor MID to
  `extruder_est_sps * MID_FRAC` clamped `[SYNC_MIN, baseline]`
- [x] 6.2 TRAILING (full) → `SYNC_MIN` (stop, drain off wall); remove
  `SYNC_RELAY_BACKOFF_FRAC`
- [x] 6.3 ADVANCE keeps strong fixed baseline-anchored catch-up
- [x] 6.4 `MID_FRAC` 1.05 → 1.10; `cmake`/`py_compile`/`openspec validate`
