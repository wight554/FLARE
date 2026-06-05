## 1. Enable the naming check

- [ ] 1.1 In `.clang-tidy`, remove `GlobalVariableIgnoredRegexp: '.*'` so the 132 tunables become fixable findings
- [ ] 1.2 Confirm `run-clang-tidy`/`clang-tidy` (LLVM 22) resolves against `build_local` compile DB

## 2. Apply the rename

- [ ] 2.1 Run `run-clang-tidy -fix -checks='-*,readability-identifier-naming' -p build_local firmware/src` to rename all globals to `g_lower_case` (AST-aware; skips string literals)
- [ ] 2.2 Manually fix the one macro-body reference clang-tidy cannot rewrite: `#define SYNC_COMPRESSION_COLLAPSE_RAMP_MULT RELAY_COLLAPSE_RAMP_MULT` in `sync.c`
- [ ] 2.3 `ninja -C build_local` passes (fix any missed reference the build flags)

## 3. Verify wire/persistence surface intact

- [ ] 3.1 Grep the 47 protocol-param string literals (`"BUF_GOAL"`, …) still UPPER_CASE
- [ ] 3.2 Confirm `CONF_*` macros, `config.ini` keys, and `settings_t` field names unchanged; `SETTINGS_VERSION` unchanged

## 4. Docs + style

- [ ] 4.1 Update `STYLE.md`: all globals `g_lower_case`; replace the prior "Global Naming Categories" (UPPER_CASE=tunable) with the enforced-`g_` rule + tunable/state distinction by location/mirror
- [ ] 4.2 Spot-check + sync C-identifier references in `CONTEXT.md` / `BEHAVIOR.md` (protocol/config names stay)

## 5. Final verify

- [ ] 5.1 Final `ninja -C build_local` pass; identifier-rename + config/docs only, zero behavior/wire/persistence change
