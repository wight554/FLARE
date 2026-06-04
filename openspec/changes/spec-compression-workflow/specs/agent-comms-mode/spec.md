## ADDED Requirements

### Requirement: Portable caveman comms ruleset
The project SHALL maintain an in-repo file `openspec/COMMS.md` that fully defines the caveman-full chat-response style, such that any agent UI can adopt it by reading the file alone, with no dependency on a Claude-specific skill or plugin.

#### Scenario: Non-Claude agent adopts caveman comms
- **WHEN** an agent running in a non-Claude UI (Codex, Gemini, Copilot) reads the repository instructions
- **THEN** `openspec/COMMS.md` provides every rule needed to produce caveman-full responses (drop articles/filler/pleasantries/hedging, fragments allowed, exact technical terms, code unchanged)
- **AND** the agent does not need to load any Claude-only skill to comply

### Requirement: Caveman-full is the default response mode
The repository SHALL direct every agent, via `AGENTS.md`, to respond in caveman-full style by default, and the directive SHALL be phrased tool-agnostically rather than as an instruction to activate a Claude-specific skill.

#### Scenario: AGENTS.md directive is tool-agnostic
- **WHEN** any agent reads the session-start protocol in `AGENTS.md`
- **THEN** it finds a directive to use caveman-full comms per `openspec/COMMS.md`
- **AND** the directive contains no instruction that only a Claude skill can satisfy

### Requirement: Human-readable exclusions
The caveman comms default SHALL NOT apply to human-readable surfaces. The ruleset SHALL exclude commit messages, pull-request descriptions, source code and code comments, user-facing documentation (including `README`, onboarding, and operator guides), and security warnings or irreversible-action confirmations, which SHALL remain normal prose.

#### Scenario: Excluded surface stays full prose
- **WHEN** an agent writes a commit message, PR description, code comment, user-facing doc, or a security/irreversible-action confirmation
- **THEN** the output is normal readable prose, not caveman-compressed

#### Scenario: Chat response uses caveman by default
- **WHEN** an agent produces an ordinary chat response that is not an excluded surface
- **THEN** the response follows the caveman-full rules in `openspec/COMMS.md`
