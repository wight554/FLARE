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

## Plan for Task 4.3 (Extract over-long functions)
- `protocol.c`: Extract `cmd_execute` (1254 lines) into:
  - `cmd_handle_motion` (handles `TC`, `T`, `LO`, `UL`, `UM`, `FL`, `RL`, `FD`, `MV` etc.)
  - `cmd_handle_sensor_status` (handles `ST`, `BS`, `TS`, `RA`, `BL`, `SM`, `CU`, `CX`, `CP` etc.)
  - `cmd_handle_system` (handles `CAL`, `SV`, `LD`, `RS`, `VR`, `?`, `MARK`, `BOOT` etc.)
  - `cmd_handle_set` (handles `SET`)
  - `cmd_handle_get` (handles `GET`)
- `settings_store.c`: Extract `settings_defaults` (120 lines) into:
  - `settings_defaults_motion`, `settings_defaults_tmc`, `settings_defaults_servo_cutter`, `settings_defaults_sync_reload`
  - Extract `settings_load` (109 lines) into:
    - `settings_load_motion`, `settings_load_tmc`, `settings_load_servo_cutter`, `settings_load_sync_reload`
- `sync.c`:
  - Extract `buffer_stabilize_tick` (140 lines) helper `boot_stabilize_tick_type_p` for Type-P analog stabilization.
  - Extract `sync_buffer_lock_tick` (166 lines) helper `sync_buffer_lock_follow` for the follow-on movement phase.
  - Extract `sync_tick` (642 lines) into helpers `sync_tick_off`, `sync_tick_retract_assist`, `sync_tick_active`, `sync_tick_auto_start`.

## Plan for Task 4.4 (Replace magic numbers)
- Scan files and replace literals like `10000u`, `500u`, `300000` with named constants or `config.ini` backed variables.

## Task 4.3 Implementation Notes (2026-06-05)

Completed helper extraction for remaining `readability-function-size` warnings:
- `protocol.c`: split `cmd_handle_get` into GET parameter groups; split buffer GET groups; split `cmd_handle_set` into SET parameter group helpers.
- `cutter.c`: extracted cutter feed-wait/ramp handling.
- `main.c`: extracted boot sensor settling.
- `sync.c`: extracted Type-P rail guard, MMU dwell sampling, reserve-integral handling, and Type-D compression drain target.
- `sync_buf.c`: extracted estimator sample application.

Validation:
- `ninja -C build_local` passed after each committed file unit.
- Focused `clang-tidy --checks=-*,readability-function-size firmware/src/*.c` produced no size warnings after final extraction.
- Same tidy command still exits on host-header lookup errors (`assert.h`, `sys/cdefs.h`, standard C headers); keep for task 5.2 lint-host fix/suppression.
- Commits: `73fc340`, `0fc36d6`, `a83d61e`, `3dd6635`, `c4e91fd`, `7317137`, `cb196ea`.

## Task 4.4 Implementation Notes (2026-06-05)

Completed source-local magic-number naming pass:
- `toolchange.c`, `cutter.c`, `main.c`, `motion.c`, `neopixel.c`: named timing, conversion, clamp, PWM, and protocol-buffer constants.
- `controller_shared.h`/shared headers plus `protocol.c`, `protocol_status.c`, `protocol_tmc.c`: named command parser/reply sizes, GET/SET bounds, TMC protocol register/byte limits, and status formatting thresholds.
- `settings_store.c`: named flash-buffer, CRC, clamp, reserve, and TMC vsense thresholds without changing `settings_t` size, layout, or `SETTINGS_VERSION`.
- `tmc2209.c`: named UART frame indexes, masks, bit shifts, current scaling, microstep mapping, and write/read timing constants.
- `sync.c`, `sync_buf.c`, `sync_analog.c`, `sync_relay.c`: named estimator, buffer geometry, Type-P rail/confidence, Type-D dwell/sample, warning-rate, and drift-control thresholds.

Validation:
- `ninja -C build_local` passed before each code commit.
- Focused `clang-tidy --checks=-*,readability-magic-numbers -p build_local firmware/src/*.c` has no project-file magic-number warnings.
- The same tidy invocations still emit host-header lookup errors; task 5.2 owns the lint-host fix or justified suppression.
- No protocol/config/runtime tunable or persisted settings layout change intended; no `SETTINGS_VERSION` bump required.
- Commits: `348c014`, `9201ed7`, `2f380cf`, `b54cd91`, `52114f4`, `ef47a7c`, `62705e3`, `43ee850`, `357cab2`, `1eb518b`, `da83a4a`, `b3b8e86`, `c83642e`, `ae9ce98`, `01333c7`.
