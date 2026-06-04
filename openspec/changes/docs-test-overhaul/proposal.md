## Why

FLARE documentation is heavily developer and AI-centric, making it difficult for human operators to assemble, wire, configure, and tune the MMU. Additionally, the test suite contains stale test files and incomplete regression script coverage, leaving several active tests unexecuted during static validation checks.

## What Changes

- **Documentation**:
  - Restructure `README.md` to be operator-oriented, emphasizing human setup, features, and quick start guides.
  - Simplify `BUILD_FLASH.md` to place precompiled firmware paths and `flash_flare.sh` usage first.
  - Add a human-friendly step-by-step physical wiring guide to `HARDWARE.md`.
  - Refactor `KLIPPER.md` to present structured Fluidd/Mainsail macros and dashboard setups.
  - Streamline `TUNING.md` to include a simple 5-minute tuning path for spring-trolley and relay-mode buffers, moving advanced analysis math to an appendix.
  - Clear and warning-tag AI-only developer documents (`AGENTS.md`, `AI.md`, `CONTEXT.md`) so users do not mistake them for operator manuals.
- **Test Suite**:
  - Remove the stale, broken `scripts/test_phase_2_10_parity.py` file.
  - Fix `scripts/test_flare_mmu_status.py` to move the top-level module exit check inside the standard main block to allow standard test discovery.
  - Overhaul `scripts/validate_regression.sh` to run the full test suite instead of just two tests.

## Capabilities

### New Capabilities
- `static-regression-validation`: Automated python unit testing and regression gating checks on the host.

### Modified Capabilities
- None

## Impact

- Affects documentation files: `README.md`, `BUILD_FLASH.md`, `HARDWARE.md`, `KLIPPER.md`, `TUNING.md`.
- Affects test scripts: `scripts/test_phase_2_10_parity.py` (deleted), `scripts/test_flare_mmu_status.py` (modified), `scripts/validate_regression.sh` (modified).
- Zero runtime impact on RP2040 C firmware or Klipper plugin integration files.
