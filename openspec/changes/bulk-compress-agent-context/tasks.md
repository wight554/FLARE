## 1. Measure and plan

- [x] 1.1 Measure current filler density for active specs and AI-facing docs before compression
  - 2026-06-05 baseline: raw spec max 14.86%; protected-clause-aware max 5.13% after safe prose compression. AI docs already low-density (`AGENTS.md` 2.29%, `AI.md` 2.69%, `CONTEXT.md` 0.62%, `WORKFLOW.md` 6.15% before edits; `CLAUDE.md`/`GEMINI.md` one-line pointers).
- [x] 1.2 List exact files in scope and files explicitly excluded
  - Scope: `openspec/specs/*/spec.md`, `AGENTS.md`, `AI.md`, `CONTEXT.md`, `CLAUDE.md`, `GEMINI.md`, `WORKFLOW.md`, and `scripts/test_spec_compression.py`.
  - Excluded: `README.md`, `BUILD_FLASH.md`, `HARDWARE.md`, `KLIPPER.md`, `TUNING.md`, `MANUAL.md`, `BEHAVIOR.md`, `TEST_CASES.md`, `MOTOR_PARAMS.md`.

## 2. Compress OpenSpec specs

- [x] 2.1 Compress `openspec/specs/*/spec.md` requirement/body prose per `openspec/COMPRESSION.md`
- [x] 2.2 Preserve each `## Purpose`, heading, table, code block, inline code, command, path, link, and RFC-2119/scenario structure
- [x] 2.3 Review representative diffs for normative meaning drift
  - Reviewed compressed islands in `acceptance-gate-parity`, `analyzer-rigor`, `buffer-geometry-vocabulary`, `config-surface-tiers`, `klipper-motion-tracking`, and `psf-type-p-sensor`; changes are non-normative prose only.

## 3. Compress AI-facing files

- [x] 3.1 Compress `AGENTS.md`, `AI.md`, `CONTEXT.md`, `CLAUDE.md`, `GEMINI.md`, and `WORKFLOW.md` where safe
  - `AI.md` and `WORKFLOW.md` compressed safely. `AGENTS.md` and `CONTEXT.md` reviewed but left unchanged to avoid weakening critical workflow/runtime instructions; `CLAUDE.md` and `GEMINI.md` already one-line pointers.
- [x] 3.2 Keep safety warnings, irreversible-action instructions, command order, and commit rules unambiguous

## 4. Ratchet and validate

- [x] 4.1 Measure post-compression density and lower `MAX_FILLER_DENSITY_PCT` only if current corpus passes
  - 2026-06-05: post max 5.13%; ratcheted threshold from 16.0% to 6.0%.
- [x] 4.2 Run `openspec validate bulk-compress-agent-context --strict`
- [x] 4.3 Run `python3 scripts/test_spec_compression.py`
- [x] 4.4 Run `scripts/validate_regression.sh` if `scripts/test_spec_compression.py` changes
- [ ] 4.5 Record validation and commit SHA in this task ledger

## Validation - 2026-06-05

- `openspec validate bulk-compress-agent-context --strict` PASS.
- `python3 scripts/test_spec_compression.py` PASS.
- `python3 -m py_compile scripts/*.py` PASS.
- `scripts/validate_regression.sh` PASS, including generated `tune.h`, firmware build via `ninja -C build_local`, unittest discovery, mock MMU status self-test, and `git diff --check`.
