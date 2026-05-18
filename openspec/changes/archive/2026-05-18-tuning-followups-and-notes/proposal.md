## Why

Validating the three shipped sync/tuning changes
(`standalone-sync-relief-model`, `sync-tuning-and-relief-finish`,
`flow-keyed-param-schedule`) surfaced recurring non-blocking findings:
inaccurate "byte-for-byte" wording in a shipped contract, inconsistent
AI-commit attribution, one change that destroyed its own `tasks.md`
history, untracked hardware-only validation, and a lingering test
artifact. None block behavior, but left untracked they erode the
spec/process contract. This change records and resolves them. No firmware
or control behavior changes.

## What Changes

- Correct the `flow-keyed-param-schedule` degenerate-equivalence wording:
  it is **milli-resolution bounded** (bias quantized to integer milli;
  ≤0.0005 absolute bias delta for non-milli-aligned fractions), not
  literal byte-for-byte. Milli-aligned (default/typical) configs retain
  practical zero regression. Fix the change's `spec.md` + `design.md`.
- Define the project convention for AI-assisted commit attribution
  (trailer format, Claude `Co-Authored-By` expectation, multi-tool case)
  in `AGENTS.md`.
- Codify a `tasks.md` hygiene rule: implementation MUST NOT empty
  `tasks.md`; mark items `[x]` and append dated validation notes (the
  pattern the later two changes already followed correctly).
- Add explicit `TEST_CASES.md` entries for the hardware-only checks that
  were static/automated-validated only: FAULT_HOLD entry+recovery,
  `cannot_refill`/`cannot_relieve` effort events, flow-schedule live
  flow-sweep parity.
- Repo cleanup: make the recommender test self-contained and stop
  `scripts/test_stream.log` from lingering untracked (gitignore or
  committed fixture).
- Document that the deterministic analyzer reducer's Python 3 `round()`
  banker's rounding is an intentional, determinism-preserving choice
  (comment in `scripts/flare_analyze.py`) so it is not "corrected" later.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `task-workflow`: add an AI-assisted commit attribution convention and a
  `tasks.md` completion-hygiene rule (do not empty; check + append
  validation notes).

## Impact

- Docs/process: `AGENTS.md` (attribution + tasks.md hygiene),
  `TEST_CASES.md` (hardware backlog entries).
- Shipped change artifacts: `openspec/changes/flow-keyed-param-schedule/`
  `spec.md` + `design.md` wording (not yet archived; edited in place).
- Repo: `.gitignore` or a committed fixture for the recommender test;
  `scripts/flare_analyze.py` clarifying comment.
- No firmware, control, retuning, or feature changes.
