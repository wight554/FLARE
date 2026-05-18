## Context

`~` expansion is fixed (commit `d32a5d3`, expanduser at parse boundary).
`flare_analyze.py` already globs `--in` via `glob.has_magic(path)` →
`sorted(glob.glob(path))` inside `read_csv_runs`. Other scripts treat
glob characters literally and raise raw tracebacks on bad paths
(`flare_baseline_recommender.py` opens `--file` with no guard;
`gcode_marker.py` has an ad-hoc not-found print). Pure stdlib + pyserial
constraint; host-only; no firmware impact.

## Goals / Non-Goals

**Goals:**
- One stdlib helper for path resolution reused across the scripts.
- Full glob for input/read paths; deterministic, sorted, like the
  analyzer today.
- Clean `Error: <path>: <reason>` + non-zero exit, no traceback.
- Preserve existing `flare_analyze.py --in` glob behavior exactly.

**Non-Goals:**
- No globbing of write/output paths.
- No firmware/control change; no new dependencies.
- Not changing what the scripts do with resolved files, only how paths
  are resolved and errors reported.

## Decisions

### D1 — Shared `scripts/path_utils.py`, modeled on the analyzer

Add `expand_input_paths(patterns) -> list[str]` (expanduser → glob if
`glob.has_magic` else literal, `sorted`, dedup, error if a non-magic
path is missing or a magic pattern matches nothing),
`resolve_input(pattern, *, must_exist=True) -> str` (single match
required; 0 or >1 → `PathError`), and `normalize_output(path) -> str`
(expanduser only). `flare_analyze.py`'s current semantics
(`glob.has_magic`, `sorted(glob.glob())`) are the reference so `--in`
output ordering and behavior do not change. Alternative (inline per
script) rejected: four divergent copies are how this drift started.

### D2 — Glob only inputs; never outputs

Read/consumed args glob: `gcode_marker.py input`,
`flare_baseline_recommender.py --file`, `flare_analyze.py --in/
--profile-fast/--profile-slow`, `flare_live_tuner.py --sidecar`. Write
args (`--output`, `--out`, `--state`, `--csv-out`) only expanduser —
globbing a destination is ambiguous/dangerous. Single-value input args
require exactly one match; >1 is a `PathError` (ambiguous) not a silent
pick. Rationale: predictable, no accidental wrong-file writes.

### D3 — Errors are messages, not tracebacks

`PathError` raised by the helper is caught at each script's top-level
`main()` and printed as `Error: <path>: <reason>` to stderr with
`sys.exit(2)` (usage-style). Cases: no glob match, missing file, not a
regular file, permission denied. Unrelated exceptions still propagate
(real bugs stay visible). Alternative (catch-all) rejected: would hide
genuine defects.

### D4 — Preserve analyzer behavior under test

A parity test asserts `flare_analyze.py --in` with a glob and with
explicit files produces the same run set/order as before this change
(helper is a refactor of existing behavior, not a change to it).

## Risks / Trade-offs

- [Refactor changes analyzer --in output subtly] → D4 parity test;
  helper reuses `glob.has_magic` + `sorted(glob.glob())` verbatim.
- [Quoted glob now expands where it did not] → Intended; documented;
  pre-stable. Literal-path behavior unchanged when no glob magic present.
- [>1 match on a single-value arg now errors instead of arbitrary pick] →
  Desired: explicit beats silent; message tells the operator to narrow.
- [exit code change (2) for path errors] → Acceptable in active dev;
  scripts previously crashed (exit 1/traceback) anyway.

## Open Questions

- Recursive `**` requires `glob.glob(..., recursive=True)`; the analyzer
  currently calls `glob.glob(path)` (no `recursive`). Default: enable
  `recursive=True` in the helper so `**` works everywhere, and update the
  analyzer path through the helper — verified equivalent for non-`**`
  patterns by the D4 parity test.
