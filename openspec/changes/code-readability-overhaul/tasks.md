## 1. Tooling + standard (no source edits)

- [x] 1.1 Add `.clang-format` (`BasedOnStyle: LLVM`, `IndentWidth: 4`, `ColumnLimit: 100`, `PointerAlignment: Right`, `AllowShortFunctionsOnASingleLine: Empty`)
- [x] 1.2 Add `.editorconfig` (UTF-8, LF, 4-space, trim trailing ws, final newline)
- [x] 1.3 Add `.clang-tidy` enabling `readability-identifier-naming`, `readability-magic-numbers`, `readability-function-size`, `bugprone-*`, `clang-analyzer-*`; naming scheme: `lower_case` fn/var, `_t` typedefs, `UPPER_CASE` macros/enum-const, `g_` global prefix
- [x] 1.4 Write `STYLE.md`: naming conventions, domain-vocabulary whitelist (`sps`,`mm`,`tmc`,`buf`,`psf`,`adc`,`pio`), file/function size norms, magic-number policy, doc-comment format, header/include order, pinned LLVM 22 (`brew install llvm@22`, macOS canonical lint host); note lint is decoupled from the ARM cross-compiler
- [x] 1.5 Document local lint invocation in `STYLE.md` (`clang-format --dry-run -Werror` + `clang-tidy` over `firmware/src` + `firmware/include`); no CI
- [x] 1.6 Cross-link `STYLE.md` from `AGENTS.md` and `CONTEXT.md`
- [x] 1.7 Commit: tooling + standard, build unaffected

## 2. Mechanical format baseline

- [x] 2.1 Run `clang-format -i` over all `firmware/src/*.c` + `firmware/include/*.h`
- [x] 2.2 Verify `ninja -C build_local` passes; confirm diff is whitespace/layout only
- [x] 2.3 Commit as isolated format-only change (single known SHA)

## 3. Behavior-preserving renames (per file, no logic edits)

- [x] 3.1 Rename cryptic types/identifiers in `controller_shared.h` (`din_t`→`debounced_input_t`, `motor_t` fields `en/dir/step`→clear names, etc.); build-verify
- [x] 3.2 Rename locals in `sync.c` (`A`/`L`→`lane`, `m`→`motor`, opaque temporaries); keep domain abbreviations; build-verify
- [x] 3.3 Rename locals in `motion.c`, `toolchange.c`, `cutter.c`; build-verify each
- [x] 3.4 Rename locals in `protocol.c`, `settings_store.c`, `tmc2209.c`, `main.c`, `neopixel.c`; build-verify each
- [x] 3.5 Sync renamed identifiers across docs (`MANUAL.md`, `BEHAVIOR.md`, `CONTEXT.md`, `AGENTS.md`) per AGENTS.md rule 6
- [x] 3.6 Confirm protocol/`SET:`/`GET:`/`config.ini`/`tune.h` surfaces unchanged (rules 7,8)

## 4. Structural splits + extraction + magic numbers

- [x] 4.1 Split `sync.c` along seams: buffer sensing/position model, type-D relay control, type-P analog control, sync orchestration; shared decls in headers; build-verify
- [x] 4.2 Split `protocol.c`: command parse / status dump / TMC-advanced; build-verify
- [x] 4.3 Extract over-long functions flagged by `readability-function-size`; build-verify
  - 2026-06-05 validation: extracted helpers through `protocol.c`, `cutter.c`, `main.c`, `sync.c`, and `sync_buf.c`; `ninja -C build_local` passed after each committed unit. Focused `clang-tidy --checks=-*,readability-function-size firmware/src/*.c` reports no function-size warnings; current clang-tidy invocation still exits on host-header lookup errors (`assert.h`, `sys/cdefs.h`, standard C headers), to be handled in 5.2.
  - Commits: `73fc340`, `0fc36d6`, `a83d61e`, `3dd6635`, `c4e91fd`, `7317137`, `cb196ea`.
- [x] 4.4 Replace residual magic numbers with named constants or config-backed tunables (config→`tune.h`→`CONF_*` path for runtime-tunable); build-verify
  - 2026-06-05 validation: replaced opaque control/protocol/persistence/estimator literals with named constants across `toolchange.c`, shared headers, `cutter.c`, `main.c`, `motion.c`, `neopixel.c`, `protocol.c`, `settings_store.c`, split protocol/sync units, `tmc2209.c`, `sync.c`, and `sync_buf.c`; no new runtime tunables or `settings_t` layout changes were introduced. `ninja -C build_local` passed before each code commit. Focused `clang-tidy --checks=-*,readability-magic-numbers -p build_local firmware/src/*.c` shows no project-file magic-number warnings; current invocation still reports host-header lookup errors, tracked for 5.2.
  - Commits: `348c014`, `9201ed7`, `2f380cf`, `b54cd91`, `52114f4`, `ef47a7c`, `62705e3`, `43ee850`, `357cab2`, `1eb518b`, `da83a4a`, `b3b8e86`, `c83642e`, `ae9ce98`, `01333c7`.
- [x] 4.5 Update file map in `AGENTS.md` Key Files + `project-architecture` spec for new units
  - 2026-06-05 validation: added split sync/protocol units and internal headers to `AGENTS.md`; updated active and durable `project-architecture` specs with split-unit ownership scenarios. Docs/spec-only; no build required. `openspec validate code-readability-overhaul --strict` passed.

## 5. Doc-comments + gate

- [x] 5.1 Normalize function/struct/macro doc-comments to `STYLE.md` format; preserve all rationale/tuning-history comments verbatim in meaning
  - 2026-06-05 validation: normalized API-facing struct/macro/shared-interface comments in public/internal headers to `/// @brief` / `///<` form; retained `.c` algorithm/hardware/tuning rationale block comments in meaning. `ninja -C build_local` passed. Commit: `07bca8b`.
- [x] 5.2 Re-run `clang-tidy`; resolve or justified-suppress remaining findings
  - 2026-06-05 validation: documented ARM/newlib `clang-tidy` invocation in `STYLE.md` and tuned `.clang-tidy` so the local firmware gate parses `firmware/src/*.c` through `build_local` without host-header errors. Resolved actionable findings (macro parentheses, reserved local name, suspicious `strcmp`, small loop counters, explicit float math, sync function-size helper, reload approach null guard). Justified suppressions cover Pico fixed-address register access, bounded `snprintf` on newlib, legacy serial parser conversion semantics, similar typed firmware APIs, generated headers, and legacy runtime globals. `ninja -C build_local` passed before code commits. Full documented `clang-tidy` command exited 0 and emitted no project warning lines; only aggregate suppressed/header counters remained.
  - Commits: `c7f0cfa`, `def0627`.
- [x] 5.3 Final `ninja -C build_local` pass; confirm zero behavior/protocol/config/tunable change across the change
  - 2026-06-05 validation: final `ninja -C build_local` passed (`ninja: no work to do` after prior successful build). Working tree clean before ledger update. Change-span review from the tooling baseline showed no edits to `config.ini`, `config.ini.example`, `scripts/gen_config.py`, `scripts/flare_cmd.py`, `MANUAL.md`, `KLIPPER.md`, `BEHAVIOR.md`, or gitignored `tune.h`; touched protocol/settings files were split/extracted/named/formatted only. `settings_t` field layout stayed unchanged and `SETTINGS_VERSION` remained `59u`; no runtime tunable, protocol parameter, persisted field, or config key was added/removed/renamed.

## 6. Comprehension layer (why-not-what comments + self-documenting structure)

- [x] 6.1 Add a file-header doc-block to each `firmware/src/*.c` (`/// @file`: what the unit owns, the core algorithm in 1-3 lines, pointer to the `BEHAVIOR.md`/spec section); build-unaffected (comments only)
  - 2026-06-05: added `/// @file` blocks to all 14 `.c` units; `ninja -C build_local` passed. Commit `1735824`.
- [x] 6.2 Add a state-transition map comment atop the `tc_state_t` FSM in `toolchange.c` (states, legal transitions, trigger per edge); comment non-obvious phase logic why-not-what
  - 2026-06-05: transition map atop `tc_tick` + why-comments (start-state, ready-to-join debounce, follow success); removed duplicate `cutter.h` include. Build passed. Commit `4dfe024`.
- [x] 6.3 Comment under-commented modules why-not-what (intent, invariants, hardware quirks, edge cases): `motion.c`, `cutter.c`, `settings_store.c`, `protocol_tmc.c`, `protocol_status.c`, `neopixel.c`; no narration of obvious lines
  - 2026-06-05: commented `motion.c` (tail-in-transit, PWM rate, lane_tick pipeline), `cutter.c` (servo PWM, cutter_state_t map), `settings_store.c` (flash/XIP, load guards), `neopixel.c` (GRB, FIFO). `protocol_tmc.c`/`protocol_status.c` covered by `@file` headers (thin dispatch/format). Build passed. Commit `ba038e2`.
- [x] 6.4 Add section-divider banners in the large units (`sync.c`, `sync_buf.c`, `protocol.c`) for navigation
  - 2026-06-05: banners in `sync.c` (8), `sync_buf.c` (7), `protocol.c` (5). Build passed. Commit `2853901`.
- [x] 6.5 Minimal enum cleanup: convert remaining bool-as-int returns / magic int params to `bool` or named enum where it clarifies intent; build-verify (states already enumerated — small targeted pass only)
  - 2026-06-05: scan found no bool-as-int returns or bool int-params. Added anonymous enum `BUF_SENSOR_TYPE_D/_P` (same int 0/1) and replaced 61 bare `BUF_SENSOR_TYPE == 0/1` comparisons; assignment/persistence/IO sites unchanged, no `settings_t`/`SETTINGS_VERSION` change. Build passed. Commit `34975e9`.
- [x] 6.6 Final `ninja -C build_local` pass; confirm comments/enum-cleanup only, zero behavior/protocol/config/tunable change
  - 2026-06-05: final `ninja -C build_local` = `no work to do` (clean). Phase 6 diff is comments, section banners, one duplicate-include removal, and the sensor-type enum naming (identical int values) only. No serial protocol, `config.ini` key, `tune.h`, runtime tunable, or `settings_t`/`SETTINGS_VERSION` change.
