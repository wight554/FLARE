## Why

FLARE is in active pre-stable development. Carrying deprecated code paths
and deprecation notices adds maintenance and confusion for no
compatibility benefit — breaking changes are acceptable now. The
shell-marker capture path also causes print lag and offers nothing the
sidecar path does not. Separately, docs are littered with stale internal
"Phase 2.x" labels that describe behavior as historical milestones instead
of as the system that exists today.

## What Changes

- **BREAKING — remove the shell-marker capture path entirely:**
  - `gcode_marker.py`: drop `--emit m118|mark|file|both` (sidecar only;
    `--emit` becomes sidecar-only or is removed), drop the deprecated
    `--every-layer` flag and the `marker_lines()` shell branches.
  - `flare_live_tuner.py`: drop `--klipper-mode off`, `--marker-file`,
    `--keep-marker-file` and the marker-file input path
    (`--klipper-mode` becomes `auto|on`).
  - Delete `scripts/flare_marker.py` and `scripts/flare_logger.py`.
- **BREAKING — remove other deprecated items:** `SAFETY_K` in
  `flare_analyze.py`; any remaining deprecated flags/shims.
- Delete the now-obsolete tests and test branches for the removed paths
  (`test_gcode_marker.py` shell assertions, any `flare_marker`/
  `flare_logger` tests).
- **Remove ALL "deprecated" notices project-wide** (code stderr lines and
  doc `**DEPRECATED**` labels) — the features are gone, so the notes are
  too.
- **Purge internal "Phase 2.x" labels from all docs** (`CONTEXT.md`,
  `BEHAVIOR.md`, `KLIPPER.md`, `MANUAL.md`); reword surviving content as
  current behavior, not a milestone history.
- **Enhance `CONTEXT.md`:** drop the Phase 2.8–2.14 prose block; rewrite
  as a phase-free current snapshot; expand the agent navigation (module/
  directory map, a "where do I look for X" index, cross-links to
  `openspec/specs/`, `TUNING.md`, `BEHAVIOR.md`).
- Update `TUNING.md` / `KLIPPER.md` / `MANUAL.md` / `README.md`: remove
  the shell-marker sections and the `flare_marker`/`flare_logger` entries
  (sidecar is the only capture path; no "deprecated" wording because the
  feature no longer exists).
- Supersede the prior `deprecate-shell-marker-capture` proposal (removed)
  and amend the unarchived `tuning-operator-guide` in place: its
  capture-path requirement becomes sidecar-only (no shell-marker
  documented at all).

## Capabilities

### New Capabilities
- `marker-capture-policy`: the Klipper sidecar path is the single
  capture mechanism; the shell-marker path and legacy marker/logger
  scripts do not exist; operator docs describe current behavior with no
  phase labels and no removed-feature references.

### Modified Capabilities
<!-- none in openspec/specs; the unarchived tuning-operator-guide spec is
     edited in place via tasks, not as a delta -->

## Impact

- Scripts (BREAKING): `gcode_marker.py`, `flare_live_tuner.py`,
  `flare_analyze.py`; delete `flare_marker.py`, `flare_logger.py`; prune
  `test_gcode_marker.py` and any obsolete tests.
- Docs: `CONTEXT.md` (rewrite/expand), `BEHAVIOR.md`, `KLIPPER.md`,
  `MANUAL.md`, `README.md`, `TUNING.md`; in-place edits to unarchived
  `openspec/changes/tuning-operator-guide/`.
- Removed change dir: `openspec/changes/deprecate-shell-marker-capture`.
- No firmware/control change; no sidecar behavior change. Acceptable
  breakage: pre-stable active development, no compatibility guarantee.
