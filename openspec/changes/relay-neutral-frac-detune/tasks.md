# Tasks — relay-neutral-frac-detune

## 1. Lower the default

- [x] 1.1 `scripts/gen_config.py` DEFAULTS: `relay_neutral_frac` `1.25 → 1.10`
  with a comment (stale gate-harden palliative; 25 % overfeed caused
  compression bang-bang; 1.10 = gentle lean).
- [x] 1.2 `config.ini.example`: update the commented `relay_neutral_frac` hint
  `1.25 → 1.10`.
- [x] 1.3 `TUNING.md`: update the `relay_neutral_frac` default in the type-D
  block `1.25 → 1.10` and note why (overfeed → quiet limit cycle).

## 2. Un-mask + correct the detector (`scripts/flare_sync_check.py`)

- [x] 2.1 `analyze_stability`: default `cycle_threshold_hz` `2.0 → 1.0`; rewrite
  docstring — type-D ringing is the relay limit cycle; reduce it by lowering
  `relay_neutral_frac` (less overfeed), not `sync_kp_rate` (inert for type-D);
  kp tuning applies to analog type-P only.
- [x] 2.2 `--stability-cycle-hz` CLI default `2.0 → 1.0` + corrected help.
- [x] 2.3 `--tune-drift-pct` CLI default `38.0 → 30.0` + corrected help.
- [x] 2.4 `analyze_drift`: rewrite docstring + FAIL message — for type-D,
  COMPRESSION-heavy drift = `relay_neutral_frac` too high, TENSION-heavy =
  catch-up too weak / `neutral_frac` too low; "Raise SYNC_KP_RATE" applies to
  analog type-P only.

## 3. Build + test

- [x] 3.1 `gen_config.py` regenerates `tune.h` with `CONF_RELAY_NEUTRAL_FRAC
  1.10f` and no errors.
- [x] 3.2 Firmware builds clean.
- [x] 3.3 `scripts/test_flare_sync_check.py` + analyzer/gen_config suites green;
  `py_compile` clean.

## 4. On-hardware validation (operator) — OPEN, blocked on rig

- [ ] 4.1 `GET:RELAY_NEUTRAL_FRAC`; `SET:RELAY_NEUTRAL_FRAC:1.10`; infill soak.
- [ ] 4.2 `flare_sync_check.py --mode stability`: peak `< 1.0` cycles/s,
  endstop `< 30 %`, TENSION ≈ 0 (no starvation). A/B 1.05–1.15 if needed.
- [ ] 4.3 If the on-hw optimum differs from 1.10, update the default to match
  and re-confirm.

## 5. Hardware-discovered relay floor fix

- [x] 5.1 `firmware/src/sync.c`: preserve the raw type-D `BUF_NEUTRAL` relay
  target as a floor after shared reserve scaling and compression-recovery
  shaping, so `RELAY_NEUTRAL_FRAC` cannot be defeated by downstream trims.
- [x] 5.2 `BEHAVIOR.md` and `TUNING.md`: document that type-D neutral relay
  output remains the minimum applied neutral feed after shared shapers.
- [x] 5.3 Validate build and OpenSpec strict validation.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `ninja -C build_local` passed.
- [ ] 5.4 HW: retest the reproduced 1.30 case; expected result is no post-tension
  `MM` collapse below `EST * RELAY_NEUTRAL_FRAC` while in `BUF_NEUTRAL`.
- [x] 5.5 `firmware/src/sync.c`: narrow the relay neutral floor to the
  tension-side half of `BUF_NEUTRAL` so compression-side trim can brake instead
  of repeatedly bang-banging `BUF_COMPRESSION`.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `ninja -C build_local` passed.
