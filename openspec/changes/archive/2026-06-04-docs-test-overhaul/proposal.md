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
  - Add a spec→doc index table to `openspec/README.md` mapping each spec to a one-line human summary and its paired human doc (if any), so humans can navigate the agent-facing specs.
  - Establish a convention that every `openspec/specs/*/spec.md` carries an uncompressed `## Purpose` summary (1-3 lines) at the top, so humans orient without reading the (token-compressed) requirement body. Backfill existing specs.
- **Test Suite**:
  - Remove the stale, broken `scripts/test_phase_2_10_parity.py` file.
  - Fix `scripts/test_flare_mmu_status.py` to move the top-level module exit check inside the standard main block to allow standard test discovery.
  - Overhaul `scripts/validate_regression.sh` to run the full test suite instead of just two tests.

## Capabilities

### New Capabilities
- `static-regression-validation`: Automated python unit testing and regression gating checks on the host.
- `spec-readability`: Human-oriented navigation of the agent-facing specs — a per-spec uncompressed `## Purpose` summary plus a central spec→doc index in `openspec/README.md`.

### Modified Capabilities
- None

## Impact

- Affects documentation files: `README.md`, `BUILD_FLASH.md`, `HARDWARE.md`, `KLIPPER.md`, `TUNING.md`, `openspec/README.md` (spec→doc index).
- Affects every `openspec/specs/*/spec.md`: adds a non-normative `## Purpose` header; no requirement text changes.
- Affects test scripts: `scripts/test_phase_2_10_parity.py` (deleted), `scripts/test_flare_mmu_status.py` (modified), `scripts/validate_regression.sh` (modified).
- Zero runtime impact on RP2040 C firmware or Klipper plugin integration files.
