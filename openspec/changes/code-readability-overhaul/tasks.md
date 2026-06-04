## 1. Tooling + standard (no source edits)

- [x] 1.1 Add `.clang-format` (`BasedOnStyle: LLVM`, `IndentWidth: 4`, `ColumnLimit: 100`, `PointerAlignment: Right`, `AllowShortFunctionsOnASingleLine: Empty`)
- [x] 1.2 Add `.editorconfig` (UTF-8, LF, 4-space, trim trailing ws, final newline)
- [x] 1.3 Add `.clang-tidy` enabling `readability-identifier-naming`, `readability-magic-numbers`, `readability-function-size`, `bugprone-*`, `clang-analyzer-*`; naming scheme: `lower_case` fn/var, `_t` typedefs, `UPPER_CASE` macros/enum-const, `g_` global prefix
- [x] 1.4 Write `STYLE.md`: naming conventions, domain-vocabulary whitelist (`sps`,`mm`,`tmc`,`buf`,`psf`,`adc`,`pio`), file/function size norms, magic-number policy, doc-comment format, header/include order, pinned LLVM 22 (`brew install llvm@22`, macOS canonical lint host); note lint is decoupled from the ARM cross-compiler
- [x] 1.5 Document local lint invocation in `STYLE.md` (`clang-format --dry-run -Werror` + `clang-tidy` over `firmware/src` + `firmware/include`); no CI
- [x] 1.6 Cross-link `STYLE.md` from `AGENTS.md` and `CONTEXT.md`
- [x] 1.7 Commit: tooling + standard, build unaffected

## 2. Mechanical format baseline

- [ ] 2.1 Run `clang-format -i` over all `firmware/src/*.c` + `firmware/include/*.h`
- [ ] 2.2 Verify `ninja -C build_local` passes; confirm diff is whitespace/layout only
- [ ] 2.3 Commit as isolated format-only change (single known SHA)

## 3. Behavior-preserving renames (per file, no logic edits)

- [ ] 3.1 Rename cryptic types/identifiers in `controller_shared.h` (`din_t`→`debounced_input_t`, `motor_t` fields `en/dir/step`→clear names, etc.); build-verify
- [ ] 3.2 Rename locals in `sync.c` (`A`/`L`→`lane`, `m`→`motor`, opaque temporaries); keep domain abbreviations; build-verify
- [ ] 3.3 Rename locals in `motion.c`, `toolchange.c`, `cutter.c`; build-verify each
- [ ] 3.4 Rename locals in `protocol.c`, `settings_store.c`, `tmc2209.c`, `main.c`, `neopixel.c`; build-verify each
- [ ] 3.5 Sync renamed identifiers across docs (`MANUAL.md`, `BEHAVIOR.md`, `CONTEXT.md`, `AGENTS.md`) per AGENTS.md rule 6
- [ ] 3.6 Confirm protocol/`SET:`/`GET:`/`config.ini`/`tune.h` surfaces unchanged (rules 7,8)

## 4. Structural splits + extraction + magic numbers

- [ ] 4.1 Split `sync.c` along seams: buffer sensing/position model, type-D relay control, type-P analog control, sync orchestration; shared decls in headers; build-verify
- [ ] 4.2 Split `protocol.c`: command parse / status dump / TMC-advanced; build-verify
- [ ] 4.3 Extract over-long functions flagged by `readability-function-size`; build-verify
- [ ] 4.4 Replace residual magic numbers with named constants or config-backed tunables (config→`tune.h`→`CONF_*` path for runtime-tunable); build-verify
- [ ] 4.5 Update file map in `AGENTS.md` Key Files + `project-architecture` spec for new units

## 5. Doc-comments + gate

- [ ] 5.1 Normalize function/struct/macro doc-comments to `STYLE.md` format; preserve all rationale/tuning-history comments verbatim in meaning
- [ ] 5.2 Re-run `clang-tidy`; resolve or justified-suppress remaining findings
- [ ] 5.3 Final `ninja -C build_local` pass; confirm zero behavior/protocol/config/tunable change across the change
