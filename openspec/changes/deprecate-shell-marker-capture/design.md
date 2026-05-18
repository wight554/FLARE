## Context

`gcode_marker.py` injects markers into the printed G-code per layer
(`marker_lines()` at lines ~398/435). Shell modes emit `M118` and
`RUN_SHELL_COMMAND`; `RUN_SHELL_COMMAND` is synchronous and blocks
Klipper's G-code queue, producing print lag. The sidecar mode writes a
separate offline JSON and injects nothing into the printed file. The tool
already defaults `--emit` to `sidecar` and already prints a shell-mode
deprecation warning (lines ~472-473); `flare_live_tuner.py` selects the
shell path via `--klipper-mode off` (line ~1222) or `--marker-file`
(~1296) with no deprecation warning. Both capture paths require Klipper, so
shell-marker offers no unique capability. Docs-only + minimal stderr
additions; no removal, no behavior change.

## Goals / Non-Goals

**Goals:**
- Make sidecar the single recommended capture path in docs and policy.
- Loud, accurate, single deprecation warnings at the two entry points.
- Keep every existing flag/path working unchanged.

**Non-Goals:**
- No removal of shell-marker flags or code paths.
- No firmware/control/sidecar change, no retuning.
- No new Klipper-less path (none exists or is needed).

## Decisions

### D1 — Soft-deprecate via warnings + docs, zero behavior change

Keep `--emit m118|mark|file|both`, `--klipper-mode off`, `--marker-file`
fully functional. Deprecation is communicated, not enforced. Rationale:
users mid-workflow must not break; the lag is a quality issue, not a
correctness one. Alternative (remove/hard-fail the flags) rejected:
breaks in-flight users and exceeds the request ("deprecate", not
"remove").

### D2 — Warnings: augment, do not duplicate

`gcode_marker.py` already emits one shell-mode deprecation line — extend
that single line with the print-lag cause; do not add a second warning.
`flare_live_tuner.py` gets exactly one stderr line when shell-marker
input is selected (`--klipper-mode off` or `--marker-file` without UDS).
One warning per process, no spam, no per-layer noise. Rationale: a single
clear message beats repeated lines an operator will filter out.

### D3 — Docs: demote, do not delete

`TUNING.md` keeps a Capture Path B section so existing users can still
follow it, but it is retitled Deprecated, leads with the lag rationale and
a sidecar redirect, and the "first-class for capture" wording is removed.
The TL;DR/capture narrative presents sidecar as the only path. `MANUAL.md`
gets a deprecation note + redirect where it presents shell-marker
normally. Rationale: deleting the section would strand users who already
built shell-marker into `printer.cfg`; demotion both warns and supports
them.

### D4 — Fix the unarchived tuning-operator-guide in place

`tuning-operator-guide` is proposed/committed but not archived; its
`operator-tuning-guide` spec is not yet in `openspec/specs/`. Edit that
change's spec requirement and TUNING tasks/notes in place (the pattern
`tuning-followups-and-notes` already established for the unshipped
flow-keyed change), so the two changes do not ship a contradiction
("both paths first-class" vs "shell-marker deprecated"). Alternative
(delta against openspec/specs) is impossible — the capability is not
synced there yet.

## Risks / Trade-offs

- [Two changes edit the same TUNING.md / tuning-operator-guide spec] →
  This change is authored after `tuning-operator-guide` and explicitly
  rewrites the conflicting wording; apply order is
  tuning-operator-guide-aware (its spec/text already exist on `main`).
- [Operators ignore stderr] → Docs are the primary channel; the warning
  is a backstop. Single clear line maximizes the chance it is read.
- [Shell-marker users feel abandoned] → Path B stays documented and
  functional; only its status and prominence change.

## Open Questions

- Whether `flare_live_tuner.py` should also surface the deprecation in its
  normal startup banner in addition to the one-time stderr warning —
  default: single stderr warning only, to avoid banner clutter; revisit
  if users still miss it.
