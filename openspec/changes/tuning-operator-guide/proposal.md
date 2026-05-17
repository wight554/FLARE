## Why

The only end-to-end tuning instructions live in `MANUAL.md` under headings
like "Trailing-Bias Tuning Quickstart (Phase 2.7)" and "Calibration
Workflow". They assume firmware/internals knowledge, lead with a stale
Phase-2.7 `SET:` soak that is misleading as a first step, and never
document the new observe-only `flare_baseline_recommender.py` or how to
read the new status fields/events. A user with no firmware context cannot
tune from these. We need one plain-language guide with exact copy-paste
commands. Docs only — no firmware, scripts, or control changes.

## What Changes

- New `TUNING.md`: a complete operator tuning guide with a top "simplest
  path" TL;DR, plain explanation of what tuning does, exact prerequisites,
  the two-profile bracket mental model (same model printed fastest- and
  slowest-cubic-flow), exact capture commands for **both** the Klipper
  sidecar and the standalone shell-marker fallback, the deterministic
  flow-schedule analyze command, exact review/apply/flash/watermark steps,
  the `flare_baseline_recommender.py` usage, a verification section
  (STATUS fields incl. `SYNC_REFILL_MM`/`SYNC_RELIEVE_MM`; operator
  meaning of `FAULT_HOLD`/`FAULT_HOLD_RECOVERY`/`cannot_refill`/
  `cannot_relieve`), and troubleshooting (acceptance-gate FAIL vs WARN;
  "different numbers each run" → determinism + scalar fallback).
- **No internal "Phase 2.x" labels** anywhere in `TUNING.md`.
- `README.md` + `MANUAL.md`: link to `TUNING.md`; the stale
  "Trailing-Bias Tuning Quickstart (Phase 2.7)" is redirected to
  `TUNING.md` and de-jargoned (not duplicated).
- Git workflow is cross-linked, not copied.

## Capabilities

### New Capabilities
- `operator-tuning-guide`: the required contents, accuracy, and
  jargon-free constraints of the user-facing tuning guide — every command
  copy-paste accurate against the current scripts, all required sections
  present, no Phase labels, both capture paths covered.

### Modified Capabilities
<!-- none — docs-only; no behavioral requirement changes -->

## Impact

- New `TUNING.md`.
- `README.md`, `MANUAL.md`: links + redirect/de-jargon of the stale
  quickstart heading.
- No changes to `scripts/*`, firmware, config values, or behavior. If a
  command cannot be made copy-paste accurate against a script as-is, it is
  recorded as an open question rather than changing code.
- Commands verified against: `flare_live_tuner.py`, `gcode_marker.py`,
  `flare_analyze.py` (`--profile-fast/--profile-slow/--emit-flow-schedule
  /--flow-schedule-cap`), `flare_baseline_recommender.py`
  (`--port/--baud/--file` only), `gen_config.py` (no args),
  `flash_flare.sh`.
