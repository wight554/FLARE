## Why

Host-script path handling is inconsistent. The `~` expansion crash was
just fixed, but two gaps remain: (1) missing/unreadable path arguments
raise raw Python tracebacks (e.g. `flare_baseline_recommender.py --file`
on a missing file) instead of a clean operator error; (2) glob patterns
work only for `flare_analyze.py --in` — every other path argument treats
a quoted `*`/`?`/`[...]`/`**` literally, so operators cannot use shell-
style globs (or must rely on the shell expanding them, which fails when
quoted).

## What Changes

- Add full glob syntax support (`*`, `?`, `[...]`, recursive `**`) for
  **input/read** path arguments across the host scripts, reusing the
  existing `flare_analyze.py` glob behavior as the reference:
  - `gcode_marker.py` positional `input`
  - `flare_baseline_recommender.py --file`
  - `flare_analyze.py --in` (already), `--profile-fast`, `--profile-slow`
  - `flare_live_tuner.py --sidecar`
  - List args accept many matches; single-value args require exactly one
    match (0 or >1 → clean error).
- **Output/write** path args (`--output`, `--out`, `--state`,
  `--csv-out`) are expanduser-normalized only — never globbed.
- Replace raw tracebacks for path errors with a single stderr line
  `Error: <path>: <reason>` and a non-zero exit, for missing,
  unreadable, not-a-file, or no-glob-match cases.
- Centralize this in a small stdlib-only `scripts/path_utils.py`
  (`expand_input_paths`, `resolve_input`, `normalize_output`) reused by
  the scripts; behavior matches today's analyzer glob semantics.
- `~` expansion stays (already shipped) and is folded into the helper.

## Capabilities

### New Capabilities
- `script-path-handling`: how host scripts resolve path arguments —
  `~` expansion, glob support for input paths, write paths never globbed,
  and clean error+exit (no traceback) for bad paths.

### Modified Capabilities
<!-- none in openspec/specs -->

## Impact

- New `scripts/path_utils.py` (+ `scripts/test_path_utils.py`).
- `scripts/gcode_marker.py`, `scripts/flare_baseline_recommender.py`,
  `scripts/flare_analyze.py`, `scripts/flare_live_tuner.py` use the
  helper; `flare_analyze.py` glob behavior is preserved (the helper is
  modeled on it).
- No firmware/control change. Host-only. Active pre-stable dev:
  acceptable to change error/exit text and broaden glob acceptance.
