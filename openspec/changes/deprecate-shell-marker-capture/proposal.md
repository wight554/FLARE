## Why

The standalone shell-marker capture path injects per-layer
`RUN_SHELL_COMMAND` / `M118` lines into the printed G-code.
`RUN_SHELL_COMMAND` blocks Klipper's G-code queue, so markers cause
visible print lag/stutter. The Klipper sidecar path injects nothing into
printed G-code (offline JSON) and has zero print-time overhead. Shell-marker
also requires Klipper (`[gcode_shell_command]` in `printer.cfg`) just like
the sidecar path, so it provides no Klipper-less capability — it is a
strictly worse, laggier route to the same result. `gcode_marker.py`
already warns it is deprecated and already defaults to sidecar; the docs
still wrongly present shell-marker as "first-class".

## What Changes

- Sidecar becomes the single supported/recommended live-capture path.
- **DEPRECATED:** shell-marker capture (`gcode_marker.py --emit
  m118|mark|file|both`, `flare_live_tuner.py --klipper-mode off` /
  `--marker-file`). Soft-deprecation only: paths keep working; nothing is
  removed or broken.
- Strengthen the runtime deprecation signal: `gcode_marker.py`'s existing
  shell-mode stderr warning gains the print-lag rationale;
  `flare_live_tuner.py` prints a one-line deprecation warning when started
  with `--klipper-mode off` or `--marker-file` (still functions).
- `TUNING.md`: demote "Capture Path B: Standalone Shell-Marker Fallback"
  to a clearly-marked **Deprecated** section that states the print-lag
  cause and redirects to the sidecar path; remove the contradictory
  "first-class for capture" wording; TL;DR and capture sections present
  sidecar as THE path.
- Amend the not-yet-applied `tuning-operator-guide` change in place: its
  spec requirement "Both capture paths are first-class" becomes "sidecar
  is the supported capture path; shell-marker is deprecated (print-lag)
  but still documented", with matching task/TUNING wording (same in-place
  pattern `tuning-followups-and-notes` used for the unshipped flow-keyed
  change).
- `MANUAL.md` and any other doc presenting shell-marker as a normal
  option gets the deprecation note + sidecar redirect.

## Capabilities

### New Capabilities
- `marker-capture-policy`: which live-capture path is supported, the
  shell-marker deprecation (with the print-lag rationale), the
  no-removal/no-break guarantee, and the required runtime deprecation
  warnings.

### Modified Capabilities
<!-- none in openspec/specs; the unarchived tuning-operator-guide spec is
     edited in place via tasks, not as a delta -->

## Impact

- Docs: `TUNING.md`, `MANUAL.md`, in-place edits to
  `openspec/changes/tuning-operator-guide/` (spec + TUNING tasks/notes).
- Scripts (warnings only, no behavior change): `scripts/gcode_marker.py`
  (augment existing stderr warning with lag rationale),
  `scripts/flare_live_tuner.py` (one stderr deprecation warning on
  `--klipper-mode off` / `--marker-file`).
- No firmware/control change, no sidecar change, no flag/code removal,
  commands stay copy-paste accurate, no internal Phase labels in operator
  docs.
