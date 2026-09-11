## 1. Firmware

- [x] 1.1 In `firmware/src/sync.c` type-D relay block, set the `BUF_COMPRESSION`
  target to `0` (was `SYNC_MIN_SPS`).
- [x] 1.2 Bypass the `clamp_i(target, SYNC_MIN, max)` for type-D COMPRESSION so
  feed reaches 0 (`else if (BUF_SENSOR_TYPE == 0 && s == BUF_COMPRESSION)
  target_sps = 0;`).
- [x] 1.3 Confirm type-P (`BUF_SENSOR_TYPE != 0`) path is untouched; the legacy
  compression floor stays type-P-only.

## 2. Cleanup of reverted approach

- [x] 2.1 Remove the unused `relay_compression_relief_mm` tunable
  (`config.ini.example`, `scripts/gen_config.py`, generated `tune.h`).
- [x] 2.2 Update `BEHAVIOR.md` / `TUNING.md` to the minimal true-stop (drop the
  overfill-budget / relief-lifecycle text).

## 3. Validation

- [x] 3.1 Build clean (`ninja -C build_local`).
- [x] 3.2 Hardware: constant 1500 mm/min feed — no TENSION-jump limit cycle.
- [x] 3.3 Hardware: fast multi-blob 200 mm purge (with the `M83` macro fix) —
  no grind/jam, brief COMPRESSION, no overfill.
- [x] 3.4 Hardware: end-of-feed stop — feed goes to 0, buffer does not deepen
  past the switch.
- [ ] 3.5 Real multicolor print sign-off (deferred; low risk for a 1-line
  COMPRESSION 100→0 change).

## 4. Notes

- The purge grind/jam root cause was a klipper macro bug (`G90` → absolute
  extruder → purge `G1 E` retracts into the buffer); fixed in the macro with
  `M83`, not firmware. Defaults used: `SYNC_TENSION_RAMP_MS=0`,
  `SYNC_UP_RATE=40`, `POST_PRINT_STAB_MS=0`.
- `scripts/flare_purge_check.py` (+ test) kept as a regression harness.
