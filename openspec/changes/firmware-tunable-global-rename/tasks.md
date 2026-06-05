## 1. Enable the naming check

- [x] 1.1 In `.clang-tidy`, remove `GlobalVariableIgnoredRegexp: '.*'` so the 132 tunables become fixable findings
  - 2026-06-05: removed the ignore regexp; also added `Global/StaticConstantCase: UPPER_CASE` so `static const` constants are not swept into `g_`.
- [x] 1.2 Confirm `run-clang-tidy`/`clang-tidy` (LLVM 22) resolves against `build_local` compile DB
  - 2026-06-05: clang-tidy 22.1.5 via `/opt/homebrew/opt/llvm/bin`; `-p build_local` compile DB present.

## 2. Apply the rename

- [x] 2.1 Run `run-clang-tidy -fix -checks='-*,readability-identifier-naming' -p build_local firmware/src` to rename all globals to `g_lower_case` (AST-aware; skips string literals)
  - 2026-06-05: renamed the 132 tunables + remaining non-`g_` globals (`active_lane`->`g_active_lane`, `extruder_est_sps`->`g_extruder_est_sps`, etc.). First run also swept `static const` constants — reverted, added constant-case protection, re-ran clean.
- [x] 2.2 Manually fix the macro-body references clang-tidy cannot rewrite
  - 2026-06-05: fixed 3 aliases in `sync_internal.h` (`g_relay_collapse_delay_ms/_ramp_mult/_cap_ms`), not just the one initially grepped.
- [x] 2.3 `ninja -C build_local` passes (fix any missed reference the build flags)
  - 2026-06-05: clean `ninja -C build_local -t clean && ninja` = `[106/106] Linking`. clangd showed transient stale undeclared-id errors mid-reindex; clean build is authoritative.

## 3. Verify wire/persistence surface intact

- [x] 3.1 Grep the 47 protocol-param string literals (`"BUF_GOAL"`, …) still UPPER_CASE
  - 2026-06-05: `"BUF_GOAL"`/`"RELOAD_MODE"`/`"SYNC_RESERVE_PCT"`/`"AUTO_MODE"`/`"UNLOAD_CUT"` = 2 each (SET+GET) intact; `grep '"g_[a-z_]*"'` in src = 0 (no literal got a `g_`).
- [x] 3.2 Confirm `CONF_*` macros, `config.ini` keys, and `settings_t` field names unchanged; `SETTINGS_VERSION` unchanged
  - 2026-06-05: no diff to `tune.h`/`config.ini`/`scripts`; `settings_t` fields stay lower_case; `SETTINGS_VERSION` = `59u`. clang-tidy global-naming violations now = 0.

## 4. Docs + style

- [x] 4.1 Update `STYLE.md`: all globals `g_lower_case`; replace the prior "Global Naming Categories" (UPPER_CASE=tunable) with the enforced-`g_` rule + tunable/state distinction by location/mirror
  - 2026-06-05: rewrote the §1 exemption note and §2 "Global Naming Categories"; tunable vs state distinguished by location (controller_shared.h block + settings_t), not casing.
- [x] 4.2 Spot-check + sync C-identifier references in `CONTEXT.md` / `BEHAVIOR.md` (protocol/config names stay)
  - 2026-06-05: CONTEXT.md has no renamed-identifier refs. BEHAVIOR.md refs are either unchanged setting names (`BUF_GOAL`, `SYNC_MIN_SPS`, `RELAY_NEUTRAL_FRAC` — wire/config names, not renamed) or conceptual pseudocode vars (`extruder_est_sps`); the documented surface is unchanged, so no doc edit required.

## 5. Final verify

- [x] 5.1 Final `ninja -C build_local` pass; identifier-rename + config/docs only, zero behavior/wire/persistence change
  - 2026-06-05: final build passes; diff is identifier rename + `.clang-tidy` + `STYLE.md` only. No protocol/config/tunable/persistence/behavior change.
