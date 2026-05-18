## Context

Findings from validating three shipped changes. All non-blocking; this is
a docs/process/cleanup pass with zero firmware or control impact. The only
spec-level change is to `task-workflow` (two conventions). Everything else
is text edits, doc entries, and a repo-hygiene fix.

## Goals / Non-Goals

**Goals:**
- Make the `flow-keyed-param-schedule` equivalence contract accurate.
- Codify AI-commit attribution and `tasks.md` hygiene as enforceable
  workflow conventions.
- Track the deferred hardware validation so it is not forgotten.
- Stop the recommender test from leaving an untracked artifact.

**Non-Goals:**
- No firmware/control/behavior change, no retuning, no new features.
- Not re-opening shipped behavior; only wording/process/cleanup.

## Decisions

### D1 — Wording fix, not behavior change

Edit `flow-keyed-param-schedule` `spec.md`/`design.md` to state
milli-resolution equivalence (≤0.0005 abs bias delta off the milli grid;
exact for milli-aligned configs). The change is unshipped/unarchived, so
edit in place rather than as a delta. No code changes — the firmware
already behaves this way; only the contract text was overstated.

### D2 — Conventions land in `task-workflow` + `AGENTS.md`

Two new `task-workflow` requirements: (a) AI-assisted commit attribution —
keep the existing Claude `Co-Authored-By` line; when another tool
generated code, add a `Generated-By:` trailer in addition, not instead;
(b) `tasks.md` hygiene — implementation marks `[x]` and appends dated
validation notes; emptying or deleting task content is prohibited.
`AGENTS.md` gets the human-readable version. Alternative (memory-only
preference) rejected: this is a repo contract for any contributor/agent,
belongs in versioned docs.

### D3 — Hardware backlog is tracked, not blocking

Add `TEST_CASES.md` entries (FAULT_HOLD entry+recovery, effort events,
flow-sweep parity) marked as pending manual hardware validation. Keeps the
known gap visible without blocking the shipped changes (no MMU available
in the working environment).

### D4 — Recommender test self-contained

Make `scripts/test_flare_baseline_recommender.py` create/clean its stream
fixture in a temp dir (or commit a small fixture) so no
`scripts/test_stream.log` is left untracked. Prefer temp-dir self-cleanup
over a committed binary-ish log. Add the path to `.gitignore` as a
belt-and-suspenders.

## Risks / Trade-offs

- [Editing a shipped change's artifacts mid-flight] → It is unarchived;
  spec sync at archive time will carry the corrected wording. Acceptable
  and preferable to archiving a known-wrong contract.
- [Convention churn] → Conventions match what the last two changes already
  did; codifying is low-friction and prevents regression of practice.

## Open Questions

- Commit a fixture vs temp-dir generation for the recommender test —
  default temp-dir self-cleanup; revisit only if the stream needs to be a
  stable golden.

## Implementation Notes (2026-05-18)

### Current source findings
- `flow-keyed-param-schedule` still says the length-1 path is
  "byte-for-byte" identical even though the generated schedule stores bias as
  integer milli. Baseline SPS is exact; bias is exact only for milli-aligned
  values and otherwise bounded by half a milli.
- `AGENTS.md` already requires `Generated-By:` footers but does not mention
  retaining Claude `Co-Authored-By` trailers or multiple AI tools.
- `AGENTS.md` requires OpenSpec task updates but does not explicitly forbid
  emptying a `tasks.md` file or require dated validation notes.
- `TEST_CASES.md` has static gates and core hardware flows, but no pending
  manual entries for the recently deferred FAULT_HOLD, effort-event, or
  flow-schedule parity checks.
- `scripts/test_flare_baseline_recommender.py` writes `test_stream.log` in the
  process working directory and removes it only on success; a prior run left
  `scripts/test_stream.log` untracked.
- `.gitignore` does not ignore `scripts/test_stream.log`.
- `scripts/flare_analyze.py` uses Python 3 `round()` in the deterministic
  reducer without a comment explaining that banker's rounding is intentional.

### File-level plan

#### `openspec/changes/flow-keyed-param-schedule/*`
- Replace byte-identical scalar wording with milli-resolution bounded wording.
- Preserve byte-identical wording only for analyzer schedule output where it
  still describes file determinism.
- Mark tasks 1.1-1.3 complete after strict validation.

#### `AGENTS.md`
- Add an explicit AI-assisted commit attribution convention under commit rules:
  keep Claude `Co-Authored-By`; add one `Generated-By:` line per contributing
  non-Claude tool/model.
- Add a task-file hygiene convention in the OpenSpec workflow section:
  preserve task text, mark `[x]`, and append dated validation notes.

#### `TEST_CASES.md`
- Add a pending manual hardware validation section for FAULT_HOLD
  entry/recovery, effort events, and flow-schedule scalar parity.

#### `scripts/test_flare_baseline_recommender.py` + `.gitignore`
- Move the replay fixture into a `tempfile.TemporaryDirectory()` and use
  absolute paths so successful and failing runs do not leave repo-local logs.
- Add `scripts/test_stream.log` to `.gitignore` as a safety net.

#### `scripts/flare_analyze.py`
- Add a local comment near deterministic reducer rounding to document Python 3
  banker's rounding as intentional and determinism-preserving.
