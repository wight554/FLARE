# Task Workflow Specification

## Purpose
Workflow contract (supersedes AGENTS.md and former TASK.md) behavioral requirements.
## Requirements
### Requirement: Load Context First
Agents MUST read `AGENTS.md`, `openspec/README.md`, and relevant specs before starting work.

#### Scenario: Session Start
- **WHEN** agent starts session
- **THEN** onboarding docs are read
- **AND** session-start banner is posted

### Requirement: OpenSpec Changes
Agents SHALL record findings and a file-level plan in `openspec/changes/<id>/` before implementation.

#### Scenario: Edit Required
- **WHEN** task requires code modification
- **THEN** findings, risks, and plan recorded in change artifact

### Requirement: Record Completion
The implementer SHALL update the change task list and target spec after durable work is complete.

#### Scenario: Milestone Complete
- **WHEN** durable unit is implemented and validated
- **THEN** completed step and commit SHA are recorded

### Requirement: NO root TASK.md
Handoff and scratch notes SHALL belong in `openspec/changes/` while active.

#### Scenario: Developer Handoff
- **WHEN** task is partially complete
- **THEN** active work state is tracked in change artifact
- **AND** root TASK.md remains absent

### Requirement: Small Commits
The agent MUST commit and push small, attributed units of work promptly.

#### Scenario: Durable Unit Complete
- **WHEN** unit passes validation
- **THEN** unit is committed and pushed immediately

### Requirement: NO Local AI Config in Commits
The repository SHALL keep `.agents/`, `.claude/`, `.gemini/` etc. OUT of the commits.

#### Scenario: Accidental Config Creation
- **WHEN** AI tool creates local config directory
- **THEN** directory is excluded from commits

### Requirement: AI-Assisted Commit Attribution

Commits MUST retain the Claude `Co-Authored-By` trailer. When code in a
commit was generated or substantially assisted by another AI tool, the
commit MUST additionally carry a `Generated-By: <tool> (<model>)` trailer
— in addition to, not replacing, the Claude `Co-Authored-By` line. If
multiple tools contributed, each MUST appear on its own `Generated-By:`
line.

#### Scenario: Single non-Claude tool generated the code

- **WHEN** an implementation commit's code was produced by another AI tool
- **THEN** the commit message contains both the Claude `Co-Authored-By`
  trailer and a `Generated-By: <tool> (<model>)` trailer

#### Scenario: Multiple tools contributed

- **WHEN** more than one AI tool contributed to a commit
- **THEN** each tool has its own `Generated-By:` line and the Claude
  `Co-Authored-By` trailer is still present

### Requirement: Tasks File Completion Hygiene

Implementation MUST NOT empty, truncate, or delete the content of a
change's `tasks.md`. Completing work MUST mark the corresponding items
`[x]` and MAY append dated validation notes beneath them. Task history
MUST remain reconstructable from `tasks.md` at archive time.

#### Scenario: Implementation completes tasks

- **WHEN** an implementation commit finishes tasks in a change
- **THEN** the affected items are marked `[x]` with their text preserved,
  optionally followed by a dated validation note, and no task lines are
  removed

#### Scenario: Attempt to clear tasks.md

- **WHEN** a commit would leave `tasks.md` empty or without its task lines
- **THEN** this violates the convention and the task content must be
  restored before the change is archived

### Requirement: Doc Read Modes

The `AGENTS.md` Key Files table SHALL tag each entry with a read mode: `[always]` (read every session) or `[lookup]` (grep on demand, never wholesale). Agents MUST NOT read `[lookup]` docs wholesale; they grep the topic and read matched sections only. At minimum `MANUAL.md`, `BEHAVIOR.md`, `TEST_CASES.md`, `TUNING.md`, and `openspec/changes/archive/**` are `[lookup]`.

#### Scenario: Runtime parameter lookup

- **WHEN** a task needs one runtime parameter's behavior or bounds
- **THEN** the agent runs `grep -n '<PARAM>' MANUAL.md` and reads matched rows only, not the whole file

#### Scenario: Archive archaeology

- **WHEN** a task needs prior-change context
- **THEN** the agent greps `memories/repo/` and `openspec/changes/archive/**` for the topic and never reads archive change directories wholesale

### Requirement: Flow Triage

Agents SHALL route work between direct implementation and the OpenSpec flow using measurable criteria. Direct only when ALL hold: no spec'd-behavior change (`grep -ril '<topic>' openspec/specs/` empty, or hits but behavior unchanged); no `settings_t`, protocol command, or runtime-tunable surface change; at most 2–3 files touched; single session; no hardware validation needed. Any other case — or uncertainty — SHALL use the OpenSpec flow (misrouted direct work loses spec sync; misrouted OpenSpec work loses only tokens).

#### Scenario: Trivial fix

- **WHEN** a change is a doc typo, comment fix, or single-file non-behavioral cleanup meeting all direct criteria
- **THEN** the agent implements directly without creating OpenSpec artifacts

#### Scenario: Tunable surface change

- **WHEN** a change adds or modifies a runtime tunable, protocol command, or spec'd behavior
- **THEN** the agent creates an OpenSpec change before implementation

### Requirement: Readiness and Delivery Checks

Generated `tasks.md` SHALL end with a final section named "Readiness and Delivery Checks" whose items gate archiving. The section MUST require: dev-tuning superset build passes (`ninja -C build_local` configured with `-DFLARE_DEV_TUNING=ON`) for firmware-touching changes; `python3 -m py_compile scripts/*.py` for script-touching changes; documentation sync verified for renamed/added parameters; `openspec validate <change-name> --strict` and `openspec validate --specs --strict` pass; and the team memory observation `memories/repo/<change-name>.md` appended.

#### Scenario: Archive attempt with failing checks

- **WHEN** a change is proposed for archive and any Readiness and Delivery Checks item is unchecked or failing
- **THEN** the change is not archived until the item passes

### Requirement: Self-Contained Tasks

Every task in generated `tasks.md` SHALL name its target file path, the exact change, and specific acceptance criteria so it can be executed without re-reading proposal or design. Mechanical steps SHALL be expressed as CLI commands rather than manual-edit instructions.

#### Scenario: Task execution without context reload

- **WHEN** an agent picks up a single task from `tasks.md` in a fresh session
- **THEN** the task text alone identifies the file, the edit, and the acceptance check

### Requirement: Hardware Task Tagging

Generated `tasks.md` SHALL prefix every hardware-dependent validation task with `HW:`. `HW:` tasks MUST NOT be checked off without explicit user confirmation backed by real-hardware test results.

#### Scenario: Task generation with hardware validation

- **WHEN** a change includes validation that requires physical printer or MMU hardware
- **THEN** those tasks are written with the `HW:` prefix and left unchecked until the user confirms hardware results

### Requirement: Compression Tiers

OpenSpec prose SHALL follow two compression tiers: `openspec/specs/**` stays lightly compressed or uncompressed (stable long-lived contracts, human readability paramount); `openspec/changes/**` artifact prose is fully compressed per `openspec/COMPRESSION.md` (iteration-heavy drafts). The tier split is forward-only: existing compressed specs are not rewritten for style alone.

#### Scenario: Authoring change artifacts

- **WHEN** an agent writes proposal, design, or tasks prose under `openspec/changes/**`
- **THEN** the prose is fully compressed per `openspec/COMPRESSION.md`

#### Scenario: Authoring or updating a spec

- **WHEN** an agent writes or edits `openspec/specs/**` content
- **THEN** the prose stays readable with light or no compression while normative structure is preserved exactly

### Requirement: Pre-Commit Self-Review

Before committing non-trivial code changes, agents SHALL review the staged diff against the `REVIEW.md` checklist (settings versioning, protocol parity, config wiring, build superset, doc sync, regression impact) instead of re-reading full rule documents. Doc-only commits MAY skip the checklist.

#### Scenario: Firmware commit

- **WHEN** an agent stages a firmware change for commit
- **THEN** it checks the staged diff against `REVIEW.md` and resolves violations before committing

### Requirement: Targeted Output Edits

Agents SHALL report edits as targeted changes only: never echo unchanged code blocks into chat, commit messages, or PR descriptions; reference file paths and line ranges instead.

#### Scenario: Reporting an edit

- **WHEN** an agent reports a completed file edit
- **THEN** it references the path and the changed lines without pasting surrounding unchanged code

