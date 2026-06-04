## ADDED Requirements

### Requirement: Portable compression ruleset
The project SHALL maintain an in-repo file `openspec/COMPRESSION.md` that fully defines the semantic compression applied to OpenSpec artifact prose, such that any agent UI can apply it by reading the file alone, with no dependency on a Claude-specific skill, plugin, API, or binary.

#### Scenario: Agent compresses without Claude tooling
- **WHEN** an agent running in a non-Claude UI (Codex, Gemini, Copilot) is directed to compress a spec body
- **THEN** `openspec/COMPRESSION.md` provides every rule needed (Remove / Preserve EXACTLY / Preserve Structure / Compress / code-block handling / Boundaries / Pattern examples)
- **AND** the agent produces the same class of output as the original `caveman-compress` skill without invoking any Claude-only tool

### Requirement: Contract preservation under compression
The compression ruleset SHALL forbid altering RFC-2119 normative keywords (SHALL, MUST, SHOULD, MAY, REQUIRED) and SHALL forbid dropping, merging, or reordering any normative clause, and SHALL treat `### Requirement:` and `#### Scenario:` headers and their WHEN/THEN structure as read-only.

#### Scenario: Normative clause survives compression
- **WHEN** a spec body containing a `SHALL`/`MUST` requirement is compressed per `openspec/COMPRESSION.md`
- **THEN** the keyword and the full normative clause remain present and unchanged
- **AND** requirement/scenario headers and WHEN/THEN markers are byte-identical to the source

#### Scenario: Code and structure preserved exactly
- **WHEN** a spec body contains fenced code, inline code, tables, or markdown headings
- **THEN** those regions are copied exactly and only surrounding prose is compressed

### Requirement: Cross-UI authoring directive
The repository SHALL instruct every agent, via `AGENTS.md` and the `openspec/config.yaml` `rules:` block, to author OpenSpec spec and change bodies in compressed form per `openspec/COMPRESSION.md`.

#### Scenario: Directive visible at session start and artifact creation
- **WHEN** an agent reads `AGENTS.md` at session start, or the OpenSpec CLI injects `config.yaml` rules while creating an artifact
- **THEN** the agent encounters the directive pointing to `openspec/COMPRESSION.md`
- **AND** the directive applies regardless of which agent UI is in use

### Requirement: Compression regression tripwire
The project SHALL provide `scripts/test_spec_compression.py`, a stdlib-only regression test consistent with existing `scripts/test_*.py`, that detects uncompressed spec prose by filler-word density and never modifies any file.

#### Scenario: Bloated spec fails the tripwire
- **WHEN** the test runs against a `openspec/specs/**/spec.md` whose prose (excluding code fences, tables, and headings) exceeds the configured banned-filler density threshold
- **THEN** the test fails and names the offending spec
- **AND** no file is rewritten by the test

#### Scenario: Tripwire posture does not red-fail current specs
- **WHEN** the test runs against the existing specs before any bulk-compression pass
- **THEN** the configured threshold or scope is such that the suite does not fail solely because the existing specs are not yet compressed
