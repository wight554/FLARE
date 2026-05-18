## 1. Remove shell-marker from scripts (BREAKING)

- [ ] 1.1 `scripts/gcode_marker.py`: remove the `m118`/`mark`/`file`/`both` branches in `marker_lines()`; set `--emit choices=["sidecar"], default="sidecar"`; delete `--every-layer` and its warning; delete the shell-mode deprecation warning
- [ ] 1.2 `scripts/flare_live_tuner.py`: remove the marker-file input path; drop `--marker-file`, `--keep-marker-file`; `--klipper-mode choices=["auto","on"]` (remove `off`); delete the four shell-marker deprecation warnings
- [ ] 1.3 Delete `scripts/flare_marker.py` and `scripts/flare_logger.py`
- [ ] 1.4 `scripts/flare_analyze.py`: remove `SAFETY_K` and any other dead deprecated shim
- [ ] 1.5 Prune obsolete tests: `scripts/test_gcode_marker.py` shell/`--every-layer`/deprecation assertions; delete any `flare_marker`/`flare_logger` tests; fix references in remaining tests

## 2. Remove deprecation notices + legacy refs from docs

- [ ] 2.1 `TUNING.md`: delete the shell-marker section entirely; ensure capture narrative is sidecar-only with no "deprecated" wording; no dangling links to the removed section
- [ ] 2.2 `KLIPPER.md`: remove shell-marker / `flare_marker` / `RUN_SHELL_COMMAND` marker content and DEPRECATED labels; keep sidecar/API motion-tracking content as current
- [ ] 2.3 `MANUAL.md`: remove DEPRECATED lines and shell-marker / `--marker-file` / `--klipper-mode off` references; keep sidecar flow
- [ ] 2.4 `README.md`: delete the `flare_logger.py` and `flare_marker.py` entries

## 3. Purge phase labels; reword as current

- [ ] 3.1 `BEHAVIOR.md`: remove "Phase 2.x" labels; reword surviving behavior in present tense
- [ ] 3.2 `KLIPPER.md`, `MANUAL.md`: remove remaining "Phase 2.x" labels; reword as current
- [ ] 3.3 Grep gate: `Phase [0-9]`, `DEPRECATED`, `flare_marker`, `flare_logger`, `--marker-file`, `--every-layer`, `klipper-mode off` return zero in tracked docs/scripts outside `openspec/changes/`

## 4. Rewrite + expand CONTEXT.md

- [ ] 4.1 Delete the Phase 2.8–2.14 prose block; fold any still-true behavior into current-tense architecture text
- [ ] 4.2 Refresh module/directory ownership map to match current `firmware/src` + `scripts/`
- [ ] 4.3 Add a "Where do I look for X" index (capture, analyze, sync control, buffer, toolchange, config generation, tests)
- [ ] 4.4 Cross-link `openspec/specs/`, `TUNING.md`, `BEHAVIOR.md`, `MANUAL.md`; keep it compact, point to specs for durable contracts

## 5. Amend unarchived tuning-operator-guide in place

- [ ] 5.1 `openspec/changes/tuning-operator-guide/specs/operator-tuning-guide/spec.md`: capture-path requirement → sidecar-only (no shell-marker documented at all); update scenarios
- [ ] 5.2 Update that change's TUNING task 2.6 + design notes to sidecar-only, appending a dated amendment note (do not rewrite history)
- [ ] 5.3 `openspec validate tuning-operator-guide --strict` still passes

## 6. Closeout

- [ ] 6.1 `openspec validate remove-deprecated-and-phase-cleanup --strict`
- [ ] 6.2 Host tests + `scripts/validate_regression.sh` green; `gcode_marker.py --help` / `flare_live_tuner.py --help` show no removed flags; sidecar capture still works
- [ ] 6.3 Grep gates from 3.3 green repo-wide; `git status` clean; confirm no firmware/control/sidecar diff
