## ADDED Requirements

### Requirement: Reviewed bulk compression scope
The project SHALL allow reviewed compression of active OpenSpec spec bodies and AI-facing repository context files, while excluding operator/user documentation from compression.

#### Scenario: Compress eligible context
- **WHEN** a bulk compression pass runs
- **THEN** `openspec/specs/*/spec.md` requirement/body prose and AI-facing docs may be compressed
- **AND** operator/user docs remain normal prose

### Requirement: Contract and structure preservation
Bulk compression SHALL preserve RFC-2119 normative clauses, `### Requirement:` headings, `#### Scenario:` headings, WHEN/THEN structure, fenced code, inline code, commands, paths, links, tables, and markdown heading structure.

#### Scenario: Normative spec compressed safely
- **WHEN** a spec body is compressed
- **THEN** normative clauses and scenario structure remain semantically unchanged
- **AND** code and command regions remain byte-identical

### Requirement: Purpose sections remain readable
Every active spec's `## Purpose` section SHALL remain uncompressed human-readable prose and SHALL remain exempt from filler-density scoring.

#### Scenario: Human opens compressed spec
- **WHEN** a human opens a compressed spec
- **THEN** the Purpose section remains readable without caveman compression
- **AND** compressed requirement prose appears below that orientation section

### Requirement: Compression threshold ratchet
The compression tripwire SHALL be ratcheted only after measuring compressed spec density and choosing a threshold that passes the reviewed compressed corpus.

#### Scenario: Threshold updated
- **WHEN** compression finishes
- **THEN** the test threshold is set from measured post-compression density with margin
- **AND** `scripts/test_spec_compression.py` passes on the committed corpus
