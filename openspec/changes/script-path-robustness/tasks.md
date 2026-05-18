## 1. Shared helper

- [ ] 1.1 Add `scripts/path_utils.py` (stdlib only): `PathError`; `normalize_output(path)` (expanduser only); `resolve_input(pattern, must_exist=True)` (expanduser → glob if `glob.has_magic` with `recursive=True` else literal; exactly one existing regular file else `PathError`); `expand_input_paths(patterns)` (list; expanduser+glob each, `sorted`, dedup; non-magic missing or magic-no-match → `PathError`)
- [ ] 1.2 Add `scripts/test_path_utils.py`: `~` expand, `*`/`?`/`[...]`/`**` match, no-match error, single-value >1 ambiguous error, output never globbed, missing/not-a-file/permission messages

## 2. Wire scripts to the helper

- [ ] 2.1 `flare_analyze.py`: route `--in` through `expand_input_paths`, `--profile-fast/--profile-slow` through `resolve_input`; `--out/--state/--config` through `normalize_output`/expanduser; keep behavior (replace the inline `glob.has_magic`/`glob.glob` in `read_csv_runs` with the helper)
- [ ] 2.2 `gcode_marker.py`: positional `input` via `resolve_input`; `--output`/`--sidecar` via `normalize_output`
- [ ] 2.3 `flare_baseline_recommender.py`: `--file` via `resolve_input`
- [ ] 2.4 `flare_live_tuner.py`: `--sidecar` via `resolve_input`; `--state`/`--csv-out`/`--klipper-log`/`--klipper-uds` via `normalize_output`/expanduser (no glob)
- [ ] 2.5 Each script: catch `PathError` in top-level `main()`, print `Error: <path>: <reason>` to stderr, `sys.exit(2)`; let unrelated exceptions propagate

## 3. Validation

- [ ] 3.1 Analyzer `--in` parity: same run set/order for a glob and explicit list vs pre-change (add to `test_flare_analyze.py` or `test_path_utils.py`)
- [ ] 3.2 Repro the original UX: `flare_baseline_recommender.py --file <missing>` and `gcode_marker.py <missing>` now print one-line errors, exit non-zero, no traceback
- [ ] 3.3 `~`/glob smoke: quoted `~/*.gcode` to `gcode_marker.py` resolves and processes
- [ ] 3.4 All host tests + `scripts/validate_regression.sh` green; `python3 -m py_compile scripts/*.py`

## 4. Closeout

- [ ] 4.1 `openspec validate script-path-robustness --strict`
- [ ] 4.2 Confirm no firmware/control diff; `git status` clean; commit
