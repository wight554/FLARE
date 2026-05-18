## Context

Active pre-stable development; project docs already state breaking
namespace changes are acceptable before a stable release. Deprecated
surface today: `gcode_marker.py` shell `--emit` modes + `--every-layer`;
`flare_live_tuner.py` `--klipper-mode off` / `--marker-file` /
`--keep-marker-file` marker-file input; `scripts/flare_marker.py`;
`scripts/flare_logger.py`; `SAFETY_K` in `flare_analyze.py`. Stale phase
labels: `CONTEXT.md` (7), `BEHAVIOR.md` (5), `KLIPPER.md` (3),
`MANUAL.md` (3). The sidecar capture path (`--emit sidecar` +
`flare_live_tuner.py --klipper-uds --sidecar`) is unaffected and becomes
the only capture mechanism. A prior `deprecate-shell-marker-capture`
proposal (soft-deprecation) is superseded by this removal.

## Goals / Non-Goals

**Goals:**
- Delete the deprecated code paths, scripts, flags, tests cleanly.
- Remove every deprecation notice (no longer meaningful once removed).
- Make docs describe current behavior; zero internal phase labels.
- Turn `CONTEXT.md` into a phase-free, expanded agent navigation guide.

**Non-Goals:**
- No firmware/control change; no sidecar behavior change.
- No back-compat shims or alias flags for removed options.
- Not rewriting spec content beyond the capture-path policy.

## Decisions

### D1 — Hard removal, no shims

Removed flags/scripts are deleted, not aliased or stubbed. `--emit`
becomes sidecar-only (drop the argument if sidecar is the sole value, or
constrain `choices=["sidecar"]` with sidecar default — choose whichever
keeps `gcode_marker.py` CLI cleanest and tests minimal).
`--klipper-mode` choices become `auto|on`. Rationale: pre-stable, no
users to protect; shims would re-introduce the maintenance this change
removes. Alternative (keep flags, hard-error) rejected: still carries dead
surface and notices.

### D2 — Notices die with the feature

Every `deprecated` stderr line and doc `**DEPRECATED**` label is removed
because its subject is removed. Where a doc section only existed to
describe a removed path (TUNING.md shell-marker, KLIPPER.md shell-marker,
README flare_marker/flare_logger entries), the section/entry is deleted,
not relabelled. Remaining flow stays accurate (sidecar-only).

### D3 — Docs describe the present tense

"Phase 2.x adds/replaces ..." becomes "FLARE does ...". Surviving
technical content is kept and reworded as current behavior; obsolete
milestone narration is dropped (recoverable from git history, which
`CONTEXT.md` already designates as the home for historical prose). A grep
gate (`Phase [0-9]`, `DEPRECATED`, `flare_marker`, `flare_logger`,
`--marker-file`, `--every-layer`, `klipper-mode off`) over tracked docs/
scripts must return zero outside `openspec/changes` and git history.

### D4 — CONTEXT.md becomes an expanded nav

Drop the Phase 2.8–2.14 block. Keep/refresh the architecture + module
ownership map; add a "where do I look for X" index (capture, analyze,
sync control, buffer, toolchange, config generation, tests) and
cross-links to `openspec/specs/`, `TUNING.md`, `BEHAVIOR.md`,
`MANUAL.md`. It stays compact and points to specs for the durable
contract rather than restating it.

### D5 — Amend unarchived tuning-operator-guide in place

`tuning-operator-guide` is not archived; its `operator-tuning-guide`
spec is not in `openspec/specs/`. Its capture-path requirement and
TUNING.md tasks/notes are rewritten in place to sidecar-only (no
shell-marker documented), continuing the established in-place pattern, so
the two changes do not ship a contradiction.

## Risks / Trade-offs

- [Breaking removal strands an in-flight shell-marker user] → Accepted:
  pre-stable, documented no-compat; sidecar is a strictly better
  drop-in and is fully documented.
- [Removing tests reduces coverage] → Only tests for deleted paths are
  removed; sidecar/analyzer/tuner tests stay; regression gate must stay
  green.
- [Over-deleting useful technical detail while de-phasing] → Reword, do
  not delete, content that is still true; only the milestone framing and
  removed-feature text go.
- [Multiple unarchived changes touch TUNING.md / tuning-operator-guide] →
  This change is authored last and explicitly rewrites the conflicting
  capture wording to the final sidecar-only state.

## Migration Plan

1. Scripts: delete `flare_marker.py`, `flare_logger.py`; strip
   shell-marker from `gcode_marker.py` and `flare_live_tuner.py`; remove
   `SAFETY_K`; prune obsolete tests. Run host tests + regression.
2. Docs: remove shell-marker/legacy sections; purge phase labels and
   `**DEPRECATED**`; reword as current; rewrite+expand `CONTEXT.md`.
3. Amend `tuning-operator-guide` (spec + TUNING tasks/notes) to
   sidecar-only.
4. Grep gates green; `openspec validate` for both this and the amended
   change; working tree clean.

Rollback: revert the change; git history retains removed scripts and the
phase prose.

## Open Questions

- `gcode_marker.py --emit`: drop the argument entirely (sidecar implicit)
  vs keep `--emit sidecar` as the only choice. Default: keep
  `choices=["sidecar"]`, `default="sidecar"` for the least-churn CLI and
  stable help text; revisit if the argument then reads as pointless.
