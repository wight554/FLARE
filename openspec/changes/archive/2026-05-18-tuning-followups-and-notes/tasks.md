## 1. Spec/design wording fix (unshipped change, edit in place)

- [x] 1.1 In `openspec/changes/flow-keyed-param-schedule/specs/flow-keyed-schedule/spec.md`, change the degenerate-equivalence wording from "byte-for-byte" to milli-resolution bounded (exact for milli-aligned configs; ≤0.0005 abs bias delta off the milli grid)
- [x] 1.2 In `openspec/changes/flow-keyed-param-schedule/design.md` D2/risks, align the equivalence claim with 1.1; note it is a contract-wording fix, no code change
- [x] 1.3 `openspec validate flow-keyed-param-schedule --strict` still passes
      Validation 2026-05-18: `openspec validate flow-keyed-param-schedule --strict`.

## 2. Workflow conventions

- [x] 2.1 Add the AI-Assisted Commit Attribution convention to `AGENTS.md` (Claude `Co-Authored-By` retained; add `Generated-By: <tool> (<model>)` in addition; multi-tool = one line each)
- [x] 2.2 Add the Tasks File Completion Hygiene rule to `AGENTS.md` (never empty `tasks.md`; mark `[x]` + append dated validation notes)
      Validation 2026-05-18: doc/process wording only; reviewed against
      `openspec/changes/tuning-followups-and-notes/specs/task-workflow/spec.md`.

## 3. Hardware validation backlog

- [x] 3.1 Add `TEST_CASES.md` entries marked pending-manual-hardware: FAULT_HOLD entry (hard-wall + advance-dwell) and auto-recovery; `cannot_refill`/`cannot_relieve` effort events at 50mm; flow-schedule live flow-sweep parity vs scalar config
      Validation 2026-05-18: manual-hardware backlog entries added; not run
      locally because no MMU hardware is available in this environment.

## 4. Repo cleanup

- [x] 4.1 Make `scripts/test_flare_baseline_recommender.py` self-contained: generate its input stream in a temp dir and clean up (no `scripts/test_stream.log` left behind)
- [x] 4.2 Add `scripts/test_stream.log` to `.gitignore` (belt-and-suspenders); confirm `git status` clean after running the recommender test
- [x] 4.3 Add a comment in `scripts/flare_analyze.py` documenting that Python 3 `round()` banker's rounding in the deterministic reducer is intentional and determinism-preserving (do not "fix" to round-half-up)
      Validation 2026-05-18: `python3 scripts/test_flare_baseline_recommender.py`;
      `python3 -m py_compile scripts/*.py`; `git status` no longer reports
      `scripts/test_stream.log`.

## 5. Closeout

- [x] 5.1 `openspec validate tuning-followups-and-notes --strict`
- [x] 5.2 Run host tests + `scripts/validate_regression.sh`; confirm `git status` clean (no untracked test artifact); no firmware/control diff in this change
      Validation 2026-05-18: `openspec validate tuning-followups-and-notes --strict`;
      all seven `scripts/test_*.py` host tests; `scripts/validate_regression.sh`;
      `git status` clean before marking closeout.
