# Tasks — fix-settings-persistence-leaks

## 1. Fix the write-only / unseeded fields (HIGH)

- [x] 1.1 `settings_load()`: read `BUF_VARIANCE_BLEND_FRAC = clamp_f(
  s->buf_variance_blend_frac, 0.0f, 0.9f)` and `BUF_VARIANCE_BLEND_REF_MM =
  clamp_f(s->buf_variance_blend_ref_mm, 0.5f, 5.0f)` (same bounds as the `SET:`
  handlers at `protocol.c:966-967`).
- [x] 1.2 `settings_defaults()`: seed `BUF_VARIANCE_BLEND_FRAC =
  CONF_BUF_VARIANCE_BLEND_FRAC` and `BUF_VARIANCE_BLEND_REF_MM =
  CONF_BUF_VARIANCE_BLEND_REF_MM`.
- [x] 1.3 Confirm `SETTINGS_VERSION` is **unchanged** (layout untouched).
- [x] 1.4 Seed `RELOAD_MODE = CONF_RELOAD_MODE` in `settings_defaults()` — the
  parity test (task 3) flagged it as loaded-from-flash but never defaulted, so a
  runtime factory-reset would not restore the compiled default. Same bug class.

## 2. Remove dead duplication

- [x] 2.1 `settings_load()`: delete the second TMC/`FOLLOW_TIMEOUT_MS` copy loop
  (`604-618`); keep the first (`526-541`).
- [x] 2.2 `settings_defaults()`: delete the duplicate `MM_PER_STEP[0/1] =
  CONF_L*_MM_PER_STEP` assignment (keep one).
- [x] 2.3 `gen_config.py`: delete one of the two identical `buf_psf_max_comp/
  max_tens/neutral/goal` blocks in `DEFAULTS`.

## 3. Parity guard

- [x] 3.1 Add `scripts/test_settings_parity.py`: parse `settings_t` fields and
  the `save()`/`load()`/`defaults()` bodies; assert every saved field is loaded
  and every loaded global is defaulted. (Compares load-LHS vs defaults-LHS
  globals — no brittle field→global map.)
- [x] 3.2 Verify it FAILS on the pre-fix tree (red: `buf_variance_blend_frac`,
  `_ref_mm` write-only), PASSES after task 1. The post-fix run additionally
  surfaced the `RELOAD_MODE` leak (task 1.4); now `108 saved / 108 loaded` clean.

## 4. Build + regression

- [x] 4.1 `python3 scripts/gen_config.py` → `tune.h` regenerates (DEFAULTS de-dup
  is value-preserving).
- [x] 4.2 Firmware builds + links clean (`ninja -C build_local`).
- [x] 4.3 `test_gen_config.py` PASS + new parity test PASS.

## 5. On-hardware confirmation (operator) — OPEN, blocked on rig

- [x] 5.1 SUPERSEDED by `tier-config-surface`: `buf_variance_blend_*` was demoted
  out of `settings_t` (now a `tune_internal.h` constant), so the VAR_BLEND
  persistence path no longer exists to test. The durable value of this change —
  the `RELOAD_MODE` default fix, the dead-code removals, and the
  `test_settings_parity.py` guard on the remaining fields — is build+parity
  verified, and the v58 firmware was flashed with no regression (see
  tier-config-surface 6.4).
