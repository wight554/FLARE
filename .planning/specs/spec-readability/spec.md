# spec-readability Specification

## Purpose
Human-oriented navigation of the agent-facing specs — a per-spec uncompressed `## Purpose` summary plus a central spec→doc index in `openspec/README.md`.
## Requirements
### Requirement: Per-spec human Purpose summary
Every `openspec/specs/*/spec.md` SHALL begin with an uncompressed `## Purpose` section of 1-3 plain-prose lines that states, for a human reader, what the spec governs and why it exists. The Purpose text SHALL be exempt from caveman/token compression and SHALL NOT restate normative requirements.

#### Scenario: Human opens a compressed spec
- **WHEN** a human opens any `openspec/specs/*/spec.md`
- **THEN** the file starts with a `## Purpose` block of readable prose summarizing the spec
- **AND** the Purpose prose is not caveman-compressed even when the requirement body below it is

#### Scenario: Spec missing Purpose is detectable
- **WHEN** a spec file has no `## Purpose` section
- **THEN** the static regression validation surfaces it as a gap

### Requirement: Central spec-to-doc index
`openspec/README.md` SHALL contain an index table mapping each capability spec to a one-line human summary and to its paired human-facing document (for example `TUNING.md`, `KLIPPER.md`) where one exists, or marking specs that have no paired human doc.

#### Scenario: Human navigates from index to the right surface
- **WHEN** a human reads the index in `openspec/README.md`
- **THEN** each spec row shows a one-line summary and the human doc that explains it (or an explicit "none" marker)
- **AND** a spec with a paired human doc links a human to that doc rather than the compressed spec

