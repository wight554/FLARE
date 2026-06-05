## Why

Firmware grew AI-first: dense, terse, machine-legible but human-hostile. Cryptic
identifiers (`A`, `L`, `m`, `din_t`, `sps`, `tc_ctx_t`), monster translation units
(`sync.c` 2837 lines, `protocol.c` 1412), macro-heavy logic, no enforced format,
no lint config (`.clang-format`/`.clang-tidy`/`.editorconfig` all absent). A human
maintainer is taking over; onboarding cost and edit risk are high. Codify a style
standard, enforce it with tooling, then apply it behavior-preserving across the tree.

## What Changes

- Add tooling: `.clang-format` (LLVM-derived, project-tuned), `.clang-tidy`
  (incl. `readability-identifier-naming`, magic-number, function-size, bugprone),
  `.editorconfig`. Lint is local-only (no CI for now); `STYLE.md` documents the
  `clang-format`/`clang-tidy` invocation.
- Add `STYLE.md`: naming conventions, file/function size norms, doc-comment format,
  header/include order, magic-number policy. Single source of truth for the standard.
- **Mechanical format pass**: `clang-format` whole firmware tree in one commit, zero
  logic change. Establishes the formatted baseline.
- **Behavior-preserving renames**: expand cryptic identifiers to intention-revealing
  names per `STYLE.md` (`A`→`lane`, `L`→`lane`, `m`→`motor`, `din_t`→`debounced_input_t`,
  abbreviations spelled where non-domain). Domain terms (`sps`, `mm`, `tmc`, `buf`,
  `psf`) kept — they are the project vocabulary, documented in `STYLE.md`.
- **Structural cleanup**: split monster TUs into cohesive modules; extract over-long
  functions; replace residual magic numbers with named constants. Each split is its
  own commit, build-verified, no behavior change.
- **Doc-comment normalization**: consistent function/struct/macro comment style;
  preserve all existing rationale comments (they carry hard-won tuning history).

No firmware behavior changes. No serial protocol, config key, or tunable changes.
Every step is build-verified; HW-validated logic stays byte-equivalent in semantics.

## Capabilities

### New Capabilities
- `code-style-standard`: enforced C style + lint contract — naming conventions,
  format config, file/function size limits, magic-number policy, doc-comment format,
  CI lint gate, and the behavior-preserving constraint for all refactors.

### Modified Capabilities
- `project-architecture`: file/module map updated for split translation units;
  no behavioral requirement changes (organization + readability norms only).

## Impact

- New files: `.clang-format`, `.clang-tidy`, `.editorconfig`, `STYLE.md`.
- Touched: all of `firmware/src/*.c`, `firmware/include/*.h` (format + renames +
  splits). New module files from split TUs.
- Docs: `AGENTS.md`/`CONTEXT.md` cross-link `STYLE.md`; rename-impact sync per
  AGENTS.md rule 6 (parameter renames update all docs). `project-architecture` spec
  file map.
- Risk: renames/splits in HW-validated `sync.c`/`toolchange.c` — mitigate with
  per-step build verify, mechanical-only edits, and diff review; no logic edits in
  the same commit as a rename or move.
- Not touched: serial protocol, `config.ini` keys, `tune.h` generation, settings
  layout, `SETTINGS_VERSION` (no `settings_t` change).
