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
