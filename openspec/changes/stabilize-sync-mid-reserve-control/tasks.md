## 1. Safe Defaults

- [x] 1.1 Update `config.ini`, `config.ini.example`, and `scripts/gen_config.py` so fresh builds default to the hardware-tested safe sync envelope.
  - 2026-05-18: Updated tracked defaults in `config.ini.example` and `scripts/gen_config.py`; updated ignored local `config.ini` for build/flash sanity.
- [x] 1.2 Regenerate `firmware/include/tune.h` from `config.ini`.
  - 2026-05-18: Ran `python3 scripts/gen_config.py`; generated ignored local `firmware/include/tune.h` with `SYNC_MAX_RATE=2200`, `SYNC_DN_RATE=80`, `SYNC_OVERSHOOT_PCT=150`, `SYNC_ADV_RAMP_MS=0`, `SYNC_OVERSHOOT_MID_EXT=1`.
- [x] 1.3 Update `MANUAL.md` and `BEHAVIOR.md` default descriptions for the changed sync tunables.
  - 2026-05-18: Documented the safe defaults and clarified that the advance estimator-bypass ramp is now disabled by default.

## 2. MID Reserve Control

- [ ] 2.1 Add a MID-only stale-estimator anti-advance floor in `firmware/src/sync.c` after normal reserve target calculation and scaling.
- [ ] 2.2 Gate the assist so it only applies in `SYNC_ACTIVE` / `BUF_MID` while the active lane is feeding without fault.
- [ ] 2.3 Confirm `BUF_TRAILING` keeps existing braking, collapse ramp, fast brake, and fault-hold behavior.

## 3. Validation

- [x] 3.1 Run `python3 -m py_compile scripts/*.py`.
  - 2026-05-18: Passed.
- [x] 3.2 Run `cmake --build build_local`.
  - 2026-05-18: Passed.
- [x] 3.3 Run `openspec validate stabilize-sync-mid-reserve-control --strict`.
  - 2026-05-18: Passed.
- [ ] 3.4 Hardware test with `python3 scripts/flare_cmd.py "?:" --poll 500`; compare `BUF`, `MM`, `BP`, `EST`, `AD`, `TD`, `APX`, `RDC`, and `EV:SYNC:*` against prior logs.
