## Why

OpenSpec apply and the durable specs burn many input tokens before any code runs — `openspec/specs/` is ~4201 lines of prose (sync-refactor 569, operator-tuning 429, psf-type-p 343), and grounding a change pulls the full referenced specs. The token-saving idea exists (Claude's `caveman-compress` skill, ~75% reduction) but is Claude-Code-only, while this project is authored from multiple agent UIs (Claude, Codex, Gemini, Copilot). Need the same semantic compression as a tool-agnostic, in-repo convention plus an enforcement that rides the existing regression flow.

## What Changes

- Add `openspec/COMPRESSION.md` — portable compressor: the caveman-compress rules lifted into the repo so any agent UI applies identical semantic compression, plus a spec-contract guard the original skill lacks (never alter RFC-2119 keywords, never drop a normative clause).
- Add authoring directive in `AGENTS.md` and `openspec/config.yaml` `rules:` so every agent sees "author spec/change bodies compressed per `openspec/COMPRESSION.md`" at artifact-creation time.
- Add `scripts/test_spec_compression.py` — regression tripwire (stdlib unittest, matches existing `scripts/test_*.py`). Detects non-compression via filler density; never rewrites.
- Add a tool-agnostic **caveman-full comms default**: reword the Claude-only `AGENTS.md` line ("Activate `caveman` skill") into a tool-agnostic directive + lift the caveman comms rules into `openspec/COMMS.md` so non-Claude UIs (Codex, Gemini, Copilot) have the actual rules, not an unloadable skill name. Excludes human-readable surfaces: commits/PRs, code + comments, user-facing docs, security/irreversible-action prose.
- **Non-goal**: not migrating to cavekit; not a git pre-commit hook; not bulk-compressing the 30 existing specs in this change (infra first, bulk pass is a follow-up).

## Capabilities

### New Capabilities
- `spec-compression-workflow`: Tool-agnostic semantic compression convention for OpenSpec artifacts — the in-repo compressor rules, the cross-UI authoring directive, and the density tripwire that gates uncompressed specs in regression.
- `agent-comms-mode`: Tool-agnostic caveman-full default for agent chat responses — in-repo comms rules (`openspec/COMMS.md`) + a tool-agnostic AGENTS.md directive, with explicit human-readable exclusions (commits/PRs, code + comments, user-facing docs, security/irreversible prose).

### Modified Capabilities
<!-- none — no existing spec's behavioral requirements change -->

## Impact

- New: `openspec/COMPRESSION.md`, `openspec/COMMS.md`, `scripts/test_spec_compression.py`.
- Edited: `AGENTS.md` (compression directive + pointer; reword caveman line tool-agnostic), `openspec/config.yaml` (uncomment + extend `rules:`).
- No firmware change. No new runtime deps (stdlib only). Existing 30 specs untouched until a separate bulk-compression pass; tripwire ships in warn/threshold mode so it does not fail CI on day one against still-uncompressed specs.
