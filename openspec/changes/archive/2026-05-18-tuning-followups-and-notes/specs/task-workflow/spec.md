## ADDED Requirements

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
