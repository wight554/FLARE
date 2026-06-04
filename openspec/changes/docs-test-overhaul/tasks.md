## 1. Test Suite Cleanup & Optimization

- [x] 1.1 Remove stale file `scripts/test_phase_2_10_parity.py`
- [x] 1.2 Modify `scripts/test_flare_mmu_status.py` to wrap the `sys.exit` code in an `if __name__ == '__main__'` block
- [x] 1.3 Update `scripts/validate_regression.sh` to run the full set of unit tests using Python's standard unittest discovery mode (`python3 -m unittest discover -s scripts -p "test_*.py"`)
- [x] 1.4 Run `scripts/validate_regression.sh` and verify all tests pass cleanly

## 2. Documentation Overhaul

- [x] 2.1 Refactor `README.md` to be user/operator oriented (intro, quick start, features list, simple concept overview)
- [x] 2.2 Reorganize `BUILD_FLASH.md` to put Precompiled UF2 / simple flashing steps first, moving CMake developer source instructions to the bottom
- [x] 2.3 Add a step-by-step wiring section in `HARDWARE.md` for human operators to wire up steppers and sensors easily
- [x] 2.4 Refactor `KLIPPER.md` to present a clear Fluidd/Mainsail macros copy-paste guide and dashboard setup
- [x] 2.5 Restructure `TUNING.md` to show a simple 5-minute tuning checklist first, relocating mathematical analyzer details to an appendix
- [x] 2.6 Prepend developer warning banners at the top of `AGENTS.md`, `AI.md`, and `CONTEXT.md` to prevent operator confusion

## 3. Link Verification & Final Check

- [x] 3.1 Perform a global repository scan of documentation links and update any broken references
- [x] 3.2 Run the static regression validation script (`validate_regression.sh`) to confirm the entire gate remains green

## 4. Spec Readability (human summaries)

- [x] 4.1 Backfill an uncompressed `## Purpose` block (1-3 prose lines, no normative restatement) at the top of every `openspec/specs/*/spec.md` that lacks one
- [x] 4.2 Add a spec→doc index table to `openspec/README.md`: one row per capability spec with a one-line human summary and its paired human doc (`TUNING.md`, `KLIPPER.md`, `README.md`, …) or an explicit "none" marker for orphan specs
- [x] 4.3 Extend `scripts/test_spec_compression.py` (or add a sibling test) to flag any `openspec/specs/*/spec.md` missing a `## Purpose` section; keep the `## Purpose` prose exempt from the filler-density tripwire
- [x] 4.4 Run `scripts/validate_regression.sh`; confirm Purpose-presence check and compression tripwire both pass

## Validation - 2026-06-05

- `openspec validate docs-test-overhaul --strict` PASS.
- `python3 scripts/test_spec_compression.py` PASS.
- `scripts/validate_regression.sh` PASS, including `python3 -m py_compile scripts/*.py`, unittest discovery, mock MMU status self-test, firmware build via `ninja -C build_local`, and `git diff --check`.
- `rg -n "TBD|created by archiving" openspec/specs/*/spec.md` found no placeholder Purpose text.
