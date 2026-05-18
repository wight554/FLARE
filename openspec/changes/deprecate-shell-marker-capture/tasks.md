## 1. Runtime deprecation warnings (no behavior change)

- [x] 1.1 `scripts/gcode_marker.py`: extend the existing shell-mode deprecation stderr line (~472-473) to include the print-lag cause ("injects per-layer RUN_SHELL_COMMAND/M118 into printed G-code, blocking Klipper's gcode queue; use --emit sidecar"); keep it a single line, output still written
- [x] 1.2 `scripts/flare_live_tuner.py`: emit exactly one stderr deprecation warning when started with `--klipper-mode off` (~1222) or with `--marker-file` and no UDS; tuner then runs unchanged
- [x] 1.3 Confirm no flags removed, no behavior changed: `python3 scripts/gcode_marker.py --help` and `flare_live_tuner.py --help` unchanged; host tests still pass

## 2. TUNING.md demotion

- [x] 2.1 Retitle "Capture Path B: Standalone Shell-Marker Fallback" → a clearly Deprecated heading; lead with the print-lag rationale + redirect to the sidecar section; remove the "first-class for capture" sentence
- [x] 2.2 Ensure TL;DR and the capture narrative present sidecar as THE path (no "Path A/Path B" equivalence); keep Path B steps intact below the deprecation banner so existing users can still follow them
- [x] 2.3 Grep TUNING.md: no "first-class" applied to shell-marker; still zero internal Phase labels

## 3. Amend unarchived tuning-operator-guide in place

- [x] 3.1 `openspec/changes/tuning-operator-guide/specs/operator-tuning-guide/spec.md`: change requirement "Both capture paths are first-class" to "Sidecar is the supported capture path; shell-marker is deprecated (print-lag) but still documented", with scenarios updated to match
- [x] 3.2 Update that change's TUNING tasks/notes wording (the 2.6 "first-class" task + design notes) to reflect deprecation, appending a dated amendment note (do not empty/rewrite history)
- [x] 3.3 `openspec validate tuning-operator-guide --strict` still passes

## 4. MANUAL.md + other docs

- [x] 4.1 `MANUAL.md`: add a deprecation note + sidecar redirect wherever shell-marker / `--emit file` / `--klipper-mode off` is presented as a normal option (no content deletion)
- [x] 4.2 Grep repo docs for shell-marker presented without a deprecation note; add the note/redirect where found

## 5. Closeout

- [x] 5.1 `openspec validate deprecate-shell-marker-capture --strict`
- [x] 5.2 Run host tests + `scripts/validate_regression.sh`; confirm only the two intended stderr-warning diffs in `scripts/*`, no firmware/sidecar/flag changes; `git status` clean
