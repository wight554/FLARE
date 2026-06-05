## Context

FLARE firmware is C on RP2040 (Pico SDK/CMake/Ninja). Code is dense and AI-authored:
strong rationale comments but cryptic identifiers, monster TUs (`sync.c` 2837,
`protocol.c` 1412, `main.c`/`motion.c` 537 each), heavy macro use, no format/lint
config. A human maintainer takes over. Much of `sync.c`/`toolchange.c` is HW-validated
tuning logic (see memory: AIMD probe latch, NEUTRAL creep, predict-lead stop) — high
regression cost if logic shifts during cleanup.

Constraints: cross-compiler toolchain, build via `ninja -C build_local`. AGENTS.md
mandates build-pass before every commit, doc sync on renames, one milestone per commit.
No local AI config committed.

## Goals / Non-Goals

**Goals:**
- Enforceable style standard (`STYLE.md`) + tooling (`.clang-format`, `.clang-tidy`,
  `.editorconfig`) + CI gate.
- Readable identifiers, cohesive file sizes, named constants, consistent doc comments.
- Zero behavior change; every step build-verified and diff-reviewable.
- Lower onboarding cost for a human maintainer.

**Non-Goals:**
- No behavior, protocol, config-key, tunable, or `settings_t` changes.
- No algorithm redesign or "while we're here" logic fixes (separate changes).
- No host-tool (`scripts/*.py`) restyle in this change (firmware-first; Python can
  follow with its own ruff/black pass later).
- No rename of documented domain vocabulary (`sps`, `mm`, `tmc`, `buf`, `psf`).

## Decisions

### D1: LLVM base style, project-tuned, over Google/custom-from-scratch
`.clang-format` `BasedOnStyle: LLVM` with overrides: `IndentWidth: 4`,
`ColumnLimit: 100`, `PointerAlignment: Right` (matches existing `lane_t *L`),
`AllowShortFunctionsOnASingleLine: Empty`. Rationale: existing code already 4-space,
right-pointer; LLVM is the smallest diff to current layout → mechanical pass churns
least. Alternatives: Google (2-space, large diff), bespoke (bikeshedding, no upstream).

### D2: `clang-tidy` naming via `readability-identifier-naming`, local-only
Enable check families: `readability-identifier-naming`, `readability-magic-numbers`,
`readability-function-size`, `bugprone-*`, `clang-analyzer-*`. Scheme: `lower_case`
functions/vars, `lower_case` + `_t` typedefs (matches existing), `UPPER_CASE` macros
/enum constants, `g_` prefix retained for globals (already used). Lint runs locally
only (no CI gate this change); `STYLE.md` documents the invocation. Rationale: user
deferred CI; local lint + diff discipline is enough to drive the cleanup.

### D3: Phased, commit-isolated application
Phases as separate commit groups so each is independently reviewable and revertible:
1. Tooling + `STYLE.md` (no source edits).
2. Mechanical `clang-format` pass (whole tree, one commit, no manual edits).
3. Renames (per-file, behavior-preserving; no logic edit in a rename commit).
4. Structural splits + function extraction + magic-number naming (per-unit commits).
5. Doc-comment normalization + onboarding cross-links + final lint sweep.
Rationale: AGENTS.md one-milestone-per-commit + bisectability. A bug introduced in a
pure-format or pure-rename commit is trivially located.

### D4: Split targets driven by cohesion, not just line count
`sync.c` (2837) splits along existing internal seams: buffer sensing/position model,
type-D relay control, type-P analog control, sync orchestration. `protocol.c` (1412)
splits command-parse vs status-dump vs TMC-advanced. Keep one domain owner per unit
(satisfies `project-architecture` MODIFIED). Headers/`controller_shared.h` hold shared
decls. Rationale: splitting on natural seams keeps each TU buildable and reviewable;
arbitrary line-count cuts would scatter related logic.

### D5: Verification = build + diff-semantics, not new tests
Behavior-preservation verified by `ninja -C build_local` pass + reviewer confirming
each commit is format/rename/move only. No HW re-validation required because semantics
are unchanged (HW tasks stay unchecked per AGENTS.md rule 12). Rationale: project has
no firmware unit-test harness; equivalence is structural, established by diff discipline.

## Risks / Trade-offs

- [Rename or split silently changes behavior in HW-validated `sync.c`/`toolchange.c`]
  → No logic edit in a rename/move commit; mechanical edits only; per-commit build +
  diff review; phases isolated so a regression bisects to one mechanical commit.
- [`clang-format` mass reflow buries a real diff / churns blame] → One isolated format
  commit, reviewed as whitespace-only, so blame churn is confined to a single known SHA
  (no blame-ignore file — active dev reflows too often for it to be worth maintaining).
- [`clang-tidy` naming check forces churn on domain abbreviations] → Whitelist domain
  vocabulary in `STYLE.md` and tune the naming regex to accept them; warn-first sizing.
- [Macro-heavy code resists tooling (`SYNC_*` aliases over `RELAY_*` tunables)] →
  Preserve macro indirection as-is; rename only where it does not break the
  config→`tune.h`→`CONF_*`/`SET:`/`GET:` parity (AGENTS.md rules 7,8).
- [Big diff is hard to land at once] → Phasing makes each commit small and
  independently build-verified, so intermediate states stay green.
- [Doc drift on renames] → AGENTS.md rule 6: every rename pass updates `MANUAL.md`,
  `BEHAVIOR.md`, `CONTEXT.md`, `AGENTS.md` references in the same commit.

## Resolved

- No CI this change — lint local-only, documented in `STYLE.md`.
- No `.git-blame-ignore-revs` — active dev reflows too often to maintain it.
- Host-tool (`scripts/*.py`) restyle deferred to a follow-up change.
- Pin LLVM 22 (`clang-format` / `clang-tidy` 22.x). macOS is the canonical (only)
  dev/lint host — `brew install llvm@22`, binaries in `/opt/homebrew/opt/llvm/bin`.
  Pi dev dropped, so no cross-host format skew to manage; pin the latest current
  major for the strongest checks. Lint is decoupled from the ARM cross-compiler:
  clang-format needs no toolchain; clang-tidy parses via its own frontend using the
  `compile_commands.json` target flags (`--extra-arg` if host-header noise appears).

## Open Questions

- None.
