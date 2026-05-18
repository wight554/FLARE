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

## Historical Phase Ledger (Sync Refactor)
- **Phase 0-1**: Sync Foundation and Adapter Logic.
- **Phase 2.0-2.7**: PSF/Analog Adapter, Estimator, and Dwell Guards.
- **Phase 2.8**: Live Tuner Foundation (Buckets, EWMA).
- **Phase 2.9**: Calibration Workflow (Observe-only, patch emission).
- **Phase 2.10**: Klipper Motion Tracking (Sidecar synthesis, UDS).
- **Phase 2.11**: Bucket Locking (Hysteresis, 3-channel unlock).
- **Phase 2.12**: Analyzer Rigor (Safe mode, precision-weighted recommendations).
- **Phase 2.13**: Acceptance Gate Parity (Consistency reduction).
- **Phase 2.14**: Gate Semantics (FAIL/WARN separation, denominator floor).
