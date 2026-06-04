## 1. Measure and plan

- [ ] 1.1 Measure current filler density for active specs and AI-facing docs before compression
- [ ] 1.2 List exact files in scope and files explicitly excluded

## 2. Compress OpenSpec specs

- [ ] 2.1 Compress `openspec/specs/*/spec.md` requirement/body prose per `openspec/COMPRESSION.md`
- [ ] 2.2 Preserve each `## Purpose`, heading, table, code block, inline code, command, path, link, and RFC-2119/scenario structure
- [ ] 2.3 Review representative diffs for normative meaning drift

## 3. Compress AI-facing files

- [ ] 3.1 Compress `AGENTS.md`, `AI.md`, `CONTEXT.md`, `CLAUDE.md`, `GEMINI.md`, and `WORKFLOW.md` where safe
- [ ] 3.2 Keep safety warnings, irreversible-action instructions, command order, and commit rules unambiguous

## 4. Ratchet and validate

- [ ] 4.1 Measure post-compression density and lower `MAX_FILLER_DENSITY_PCT` only if current corpus passes
- [ ] 4.2 Run `openspec validate bulk-compress-agent-context --strict`
- [ ] 4.3 Run `python3 scripts/test_spec_compression.py`
- [ ] 4.4 Run `scripts/validate_regression.sh` if `scripts/test_spec_compression.py` changes
- [ ] 4.5 Record validation and commit SHA in this task ledger
