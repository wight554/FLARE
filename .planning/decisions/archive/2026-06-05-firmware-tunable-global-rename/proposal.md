## Why

132 runtime-mutable, config-backed tunables are declared `UPPER_CASE` (`extern int
FEED_SPS;`). UPPER_CASE universally reads as a compile-time constant/macro, but these
are mutable globals (settable via `SET:`, persisted in flash). A newbie assumes they
are `#define`s and is surprised when they change — the single most confusing naming
trap in the firmware (flagged in the readability audit). `.clang-tidy` already targets
`GlobalVariableCase: lower_case` + prefix `g_`, but exempts these via
`GlobalVariableIgnoredRegexp: '.*'`, so the convention is declared yet unenforced.

## What Changes

- Rename all 132 `UPPER_CASE` mutable global tunables to `g_lower_case`
  (`FEED_SPS` -> `g_feed_sps`, `BUF_GOAL` -> `g_buf_goal`, …), ~1383 use sites.
- Done with `clang-tidy --fix` (`readability-identifier-naming`), which is AST-aware:
  it renames identifiers only and never touches string literals — critical because 47
  of these names also appear as literal `SET:`/`GET:` protocol param strings that MUST
  stay UPPER_CASE.
- Drop `GlobalVariableIgnoredRegexp: '.*'` so the `g_` rule is enforced going forward.
- Update `STYLE.md`: all firmware globals use `g_`; the "looks like a constant" rule is
  gone. Tunable-vs-state is distinguished by the controller_shared.h tunables section +
  `settings_t` mirror + `SET:`/`GET:` surface, not by casing.

**Not changed (wire/persistence surface):** serial protocol param strings, `config.ini`
keys, `tune.h` `CONF_*` macros, `settings_t` field names (already lower_case). No
behavior change. `SETTINGS_VERSION` unchanged.

## Capabilities

### Modified Capabilities
- `code-style-standard`: the global-naming requirement becomes enforced (`g_` for all
  globals incl. tunables); supersedes the prior `UPPER_CASE`=tunable exemption.

## Impact

- Touched: `firmware/include/controller_shared.h` (132 decls), `firmware/src/main.c` +
  `firmware/src/settings_store.c` (definitions), all `firmware/src/*.c` use sites,
  `.clang-tidy`, `STYLE.md`. One macro body (`SYNC_COMPRESSION_COLLAPSE_RAMP_MULT`)
  needs a manual edit (clang-tidy does not rewrite inside macro bodies).
- Docs: protocol/config docs reference the UPPER_CASE param/key strings (unchanged);
  only doc snippets quoting C identifiers need sync (CONTEXT.md/BEHAVIOR.md spot-check).
- Risk: large mechanical diff; mitigated by AST-accurate tooling + build verify + the
  string-literal safety of clang-tidy.
