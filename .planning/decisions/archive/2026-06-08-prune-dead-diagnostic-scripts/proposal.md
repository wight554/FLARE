## Why

Two standalone operator diagnostic scripts are dead by reference: never imported,
absent from every live doc and spec, and only mentioned in archived OpenSpec changes.
Carrying them adds 45 KB of unmaintained CLI that drifts from the serial protocol and
clutters the `scripts/` surface during pre-stable development.

- `scripts/flare_trace_filament.py` — built to debug the drawn-tip (#4) and
  standalone-load (#1) filament-tracking issues. Both resolved and archived
  (`2026-06-04-host-ui-integration`); the tool's mission is over. Only live trace via
  archive; never imported; no test.
- `scripts/flare_unload_tracker.py` — one-off 50 Hz manual-unload telemetry ASCII
  plotter. Created once (`254aa98`), never wired anywhere, only re-touched by the bulk
  ruff lint pass. Only archive reference (`2026-06-04-psf-analog-rig`); no test.

`scripts/klipper_motion_tracker.py` is NOT in scope — it is imported by
`flare_live_tuner.py` and backed by the `klipper-motion-tracking` spec.

## What Changes

- Delete `scripts/flare_trace_filament.py`.
- Delete `scripts/flare_unload_tracker.py`.
- No tests reference either (none exist); the regression gate
  (`unittest discover -p test_*.py`) is unaffected.
- Recovery path if ever needed: `git show <rev>:scripts/<name>.py`.

Tests are explicitly out of scope: every `test_*.py` is auto-discovered and run by the
regression gate and each maps to a live module or firmware/spec target — zero orphans.

## Impact

- Affected files: `scripts/flare_trace_filament.py` (deleted),
  `scripts/flare_unload_tracker.py` (deleted).
- No spec, no firmware, no live tool depends on either; regression gate output unchanged
  except two fewer files for `py_compile`/`ruff` to scan.
- Operator-facing: a hand-run bringup diagnostic is no longer present in the tree;
  recoverable from git history.
