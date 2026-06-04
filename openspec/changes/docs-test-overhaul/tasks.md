## 1. Test Suite Cleanup & Optimization

- [x] 1.1 Remove stale file `scripts/test_phase_2_10_parity.py`
- [x] 1.2 Modify `scripts/test_flare_mmu_status.py` to wrap the `sys.exit` code in an `if __name__ == '__main__'` block
- [x] 1.3 Update `scripts/validate_regression.sh` to run the full set of unit tests using Python's standard unittest discovery mode (`python3 -m unittest discover -s scripts -p "test_*.py"`)
- [x] 1.4 Run `scripts/validate_regression.sh` and verify all tests pass cleanly

## 2. Documentation Overhaul

- [ ] 2.1 Refactor `README.md` to be user/operator oriented (intro, quick start, features list, simple concept overview)
- [ ] 2.2 Reorganize `BUILD_FLASH.md` to put Precompiled UF2 / simple flashing steps first, moving CMake developer source instructions to the bottom
- [ ] 2.3 Add a step-by-step wiring section in `HARDWARE.md` for human operators to wire up steppers and sensors easily
- [ ] 2.4 Refactor `KLIPPER.md` to present a clear Fluidd/Mainsail macros copy-paste guide and dashboard setup
- [ ] 2.5 Restructure `TUNING.md` to show a simple 5-minute tuning checklist first, relocating mathematical analyzer details to an appendix
- [ ] 2.6 Prepend developer warning banners at the top of `AGENTS.md`, `AI.md`, and `CONTEXT.md` to prevent operator confusion

## 3. Link Verification & Final Check

- [ ] 3.1 Perform a global repository scan of documentation links and update any broken references
- [ ] 3.2 Run the static regression validation script (`validate_regression.sh`) to confirm the entire gate remains green
