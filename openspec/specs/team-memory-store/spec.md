# team-memory-store Specification

## Purpose

Define the git-tracked team memory store at `memories/repo/`: a tool-agnostic
prior-art layer of curated per-change observations (decisions, gotchas,
deviations) that any agent — Claude, Codex, Gemini, Copilot — reads before
re-deriving past work, and writes as part of each change's readiness checks.
Complements, never replaces, personal memory layers (cavemem, tool-private
memory).

## Requirements
### Requirement: Team memory store location and format

The project SHALL keep a git-tracked team memory store at `memories/repo/` with one observation file per archived change, named `<change-name>.md` matching the `openspec/changes/` directory name. Each file MUST contain 3–5 compressed lines covering decisions made, gotchas hit, and deviations from design with their reasons, MUST name affected specs/components explicitly so files stay greppable, and MUST NOT contain secrets, tokens, credentialed URLs, or firmware/source code snippets.

#### Scenario: Change archived with observation

- **WHEN** an OpenSpec change completes its readiness checklist and is archived
- **THEN** `memories/repo/<change-name>.md` exists with 3–5 compressed observation lines naming the affected specs/components

#### Scenario: Observation contains sensitive content

- **WHEN** a draft observation includes a secret, token, credentialed URL, or source code snippet
- **THEN** the agent removes the sensitive content before writing the file

### Requirement: Read protocol before re-derivation

Agents SHALL search the team memory store before re-deriving prior art: before drafting a proposal or design that touches existing specs or components, run `grep -ril '<topic>' memories/repo/` and cite relevant hits in the artifact instead of re-investigating from source or git history. Recalled observations MUST be verified against the current tree (named files, parameters, behaviors may have changed since written).

#### Scenario: Proposal touching prior-art area

- **WHEN** an agent drafts a proposal or design touching a spec or component with matching `memories/repo/` observations
- **THEN** the artifact cites the relevant observation files instead of re-deriving their findings

#### Scenario: Stale observation

- **WHEN** a cited observation names a file, parameter, or behavior that no longer exists in the tree
- **THEN** the agent verifies current state before acting and updates or flags the stale observation

### Requirement: Tool-agnostic store format

The memory store SHALL be plain markdown readable by any agent tool via file read and `grep`. Files MUST NOT depend on tool-specific frontmatter, skills, plugins, or MCP servers to be consumed. `memories/repo/README.md` SHALL document the write rules and read protocol.

#### Scenario: Non-Claude agent session

- **WHEN** a Codex, Gemini, or Copilot session needs prior art for an area
- **THEN** plain `grep`/read of `memories/repo/` returns usable observations without tool-specific support

### Requirement: Personal memory layers stay separate

Personal memory layers (cavemem, Claude auto-memory) SHALL remain personal and uncommitted. Agents MUST NOT commit personal memory content wholesale into `memories/repo/`; only curated per-change observations belong in the team store.

#### Scenario: Personal memory migration attempt

- **WHEN** an agent considers copying its personal memory store into the repo
- **THEN** it writes only curated per-change observation files that follow the store format rules

