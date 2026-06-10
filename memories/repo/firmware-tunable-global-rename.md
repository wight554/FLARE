# firmware-tunable-global-rename (archived 2026-06-05)

- Globals now enforced `g_lower_case`; bulk rename done via `clang-tidy --fix` (readability-identifier-naming), not sed.
- Gotchas: protect SCREAMING constants from the rule; clang-tidy is AST-safe vs protocol strings (won't touch `"SET:..."` literals) — sed is not; clangd staleness after mass rename — restart language server before trusting diagnostics.
- Must run rename over dev superset too (`-DFLARE_DEV_TUNING=1`) or guarded globals escape the rule (see `regression-gate-dev-tuning-build`).
