## Context

132 config-backed tunables are `extern UPPER_CASE` globals in `controller_shared.h`,
seeded from `CONF_*` (generated `tune.h`), mutated by `SET:` handlers, persisted in
`settings_t` (whose fields are already lower_case). ~1383 use sites across
`firmware/src`. 47 of the names also appear as literal protocol-param strings
(`"BUF_GOAL"`) that are part of the wire contract.

## Goals / Non-Goals

**Goals:**
- All firmware globals (incl. tunables) named `g_lower_case`; `g_` rule enforced.
- Zero behavior / wire / persistence change; build passes.

**Non-Goals:**
- No change to protocol param strings, `config.ini` keys, `CONF_*` macros, or
  `settings_t` field names.
- No logic edits; pure identifier rename.

## Decisions

### D1: clang-tidy --fix, not sed
`readability-identifier-naming` with `--fix` is AST-aware: it rewrites the declaration
and every reference as identifiers, and never edits string literals or `CONF_*` macros
(different spelling). sed would corrupt the 47 names that double as `"..."` protocol
strings. Run via `run-clang-tidy -fix -p build_local` (dedupes concurrent header edits
across TUs). Tool pinned at LLVM 22 per `STYLE.md`.

### D2: enable by removing the ignore regexp
`.clang-tidy` already sets `GlobalVariableCase: lower_case` + `GlobalVariablePrefix: g_`.
Removing `GlobalVariableIgnoredRegexp: '.*'` turns the 132 into fixable findings. After
the fix the regexp stays removed so the rule is enforced.

### D3: macro bodies fixed by hand
clang-tidy does not rewrite identifiers inside `#define` bodies. Audit found exactly one:
`#define SYNC_COMPRESSION_COLLAPSE_RAMP_MULT RELAY_COLLAPSE_RAMP_MULT`. Edit it manually;
the build (undeclared old name) catches any missed.

### D4: verification = build + protocol-string grep
`ninja -C build_local` must pass. Separately grep that the 47 protocol-param string
literals are still UPPER_CASE (unchanged) and that `CONF_*` / `settings_t` field names
are untouched. No HW re-validation (semantics identical).

## Risks / Trade-offs

- [clang-tidy corrupts a protocol string] → It cannot; it renames AST identifiers only.
  Verified post-run by grepping the 47 `"NAME"` literals remain UPPER_CASE.
- [Partial/concurrent header rewrites leave a half-renamed build] → `run-clang-tidy`
  dedupes; build fails loudly if any reference is missed; fix + rebuild.
- [A tunable used in a macro body is missed] → only one exists (D3); build catches it.
- [Huge diff buries a real change] → isolated rename commit, no logic edits; reviewable
  as identifier-only.
- [Docs drift] → protocol/config names unchanged; only C-identifier snippets in
  CONTEXT.md/BEHAVIOR.md need a spot-check + sync.

## Open Questions

- None.
