## 1. Portable compressor

- [ ] 1.1 Create `openspec/COMPRESSION.md`: port the `caveman-compress` SKILL.md rules (source: `~/.claude/plugins/marketplaces/caveman/caveman-compress/SKILL.md` lines 38-112) — Remove / Preserve EXACTLY / Preserve Structure / Compress / CRITICAL code-block rule / Boundaries / Pattern examples
- [ ] 1.2 Add the RFC-2119 contract guard section: never alter SHALL/MUST/SHOULD/MAY/REQUIRED, never drop/merge/reorder a normative clause, treat `### Requirement:`/`#### Scenario:` and WHEN/THEN as read-only structure
- [ ] 1.3 State scope explicitly: applies to `openspec/specs/**` and `openspec/changes/**` artifact prose; no Claude-only tool required

## 2. Cross-UI authoring directive

- [ ] 2.1 Add a directive line + pointer to `AGENTS.md` (near the OpenSpec Workflow section): author spec/change bodies compressed per `openspec/COMPRESSION.md`
- [ ] 2.2 Uncomment and extend the `rules:` block in `openspec/config.yaml` with per-artifact compression rules so the OpenSpec CLI injects the directive at artifact-creation time

## 3. Regression tripwire

- [ ] 3.1 Measure current filler density across `openspec/specs/**/spec.md` to pick a threshold/scope that does not red-fail existing specs (resolve the open question: all-specs-permissive vs changed-specs-strict)
- [ ] 3.2 Write `scripts/test_spec_compression.py` (stdlib unittest, matches existing `scripts/test_*.py`): glob specs, strip code fences/tables/headings, compute banned-filler density (the/a/an, just/really/basically/actually/simply, "in order to", "you should", "make sure to"), fail over threshold, name offenders, never rewrite
- [ ] 3.3 Verify the test passes against current uncompressed specs in the chosen posture, and fails on a deliberately bloated fixture

## 4. Caveman comms default (cross-UI)

- [ ] 4.1 Create `openspec/COMMS.md`: port the caveman comms rules (source: `~/.claude/rules/caveman.md` + caveman skill) — drop articles/filler/pleasantries/hedging, fragments OK, exact technical terms, code blocks unchanged, intensity full
- [ ] 4.2 Add the exclusions section to `openspec/COMMS.md`: commits/PRs, code + comments, user-facing docs (README/onboarding/operator guides), security warnings + irreversible-action confirmations stay normal prose
- [ ] 4.3 Reword `AGENTS.md` session-start line ("Activate `caveman` skill (intensity: full)") into a tool-agnostic directive pointing to `openspec/COMMS.md`, so Codex/Gemini/Copilot get the actual rules, not an unloadable skill name

## 5. Verify

- [ ] 5.1 Run `openspec validate spec-compression-workflow --strict` and the existing `scripts/test_*.py` suite; confirm green
- [ ] 5.2 Sanity-check: compress one short spec by hand per `openspec/COMPRESSION.md`, diff to confirm normative clauses and code/structure survive
- [ ] 5.3 Sanity-check comms: confirm `AGENTS.md` directive reads tool-agnostic and `openspec/COMMS.md` exclusions cover all 4 human-readable surfaces
