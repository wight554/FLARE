## Context

FLARE documentation contains advanced, mathematics-heavy explanations (centroids, variance, sigma calculations) and AI-targeted onboarding rules mixed with operator-facing procedures. Similarly, the python test suite contains obsolete components (e.g. legacy M118 marker comparison parity test), and the main regression script only runs a fraction of the unit tests.

## Goals / Non-Goals

**Goals:**
- Clean up the test suite by removing the broken/stale `test_phase_2_10_parity.py` file.
- Update `test_flare_mmu_status.py` to allow normal test discovery by tools like `unittest` and `pytest`.
- Expand `validate_regression.sh` to run the full set of unit tests in the repository.
- Reorganize and clarify operator documentation to put physical setup, wiring, flashing, and 5-minute tuning guides first.
- Isolate AI and developer docs from operator manuals clearly using explicit warning banners.

**Non-Goals:**
- Rewriting the RP2040 firmware C logic or the Klipper mock controller behavior.
- Adding new hardware-dependent test cases requiring dynamic HIL loops.

## Decisions

### 1. Unified Test Execution via `unittest`
- **Decision**: Update `validate_regression.sh` to run the entire unit test suite using python's `unittest` test discovery rather than hardcoding individual script invocations.
- **Rationale**: Ensures any new test file added to the `scripts/` directory is automatically executed during regression runs without needing manual scripting updates.
- **Alternatives Considered**: Using `pytest` (rejected because it introduces an external dependency, violates the "stdlib-only" constraint).

### 2. Module Exit Safeguard in `test_flare_mmu_status.py`
- **Decision**: Move the `sys.exit` code inside the `if __name__ == '__main__'` block in `test_flare_mmu_status.py`.
- **Rationale**: Standard unittest runner imports the module, which currently triggers a premature module-level exit. Wrapping the exit preserves direct CLI run support while allowing import for discovery.

### 3. Clear Separation of AI/Operator Documentation
- **Decision**: Add explicit headers to `AGENTS.md`, `AI.md`, and `CONTEXT.md` identifying them as developer and AI assistant context. Restructure the remaining documents to put simple physical walkthroughs first.
- **Rationale**: Retains the important session-start protocols and architecture guides required by AI agents, while preventing human operators from getting lost in math or LLM instructions.

## Risks / Trade-offs

- **Risk**: Deleting `test_phase_2_10_parity.py` might lose test coverage.
  - **Mitigation**: The Sidecar JSON matcher is already thoroughly covered in `test_klipper_motion_tracker.py` and `test_gcode_marker.py`, so the legacy M118 parity comparison is redundant and obsolete.
- **Risk**: Moving documentation content breaks links/structure in manuals.
  - **Mitigation**: Perform a global grep for file links and cross-references, updating all links to point to the newly restructured paths.
