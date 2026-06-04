## Context

OpenSpec stores durable behavior as prose specs (~4201 lines today) and grounds each `apply` by reading the referenced specs. Prose is token-heavy; a sync change pulls 569 lines of `sync-refactor/spec.md` before one edit. Claude's `caveman-compress` skill already solves prose bloat (~75% reduction) but ships as a Claude-Code plugin invoking the Claude API — unusable from the other UIs this repo is authored from (Codex, Gemini, Copilot). The compression *logic*, however, is pure markdown rules; only its packaging is Claude-specific.

## Goals / Non-Goals

**Goals:**
- Same semantic compression as `caveman-compress`, available to every agent UI, with zero runtime/LLM dependency in-repo.
- Compression rules live where all agents already look (repo files + AGENTS.md), so the directive is honored regardless of tool.
- Enforcement rides the existing `scripts/test_*.py` regression flow, not a separate mechanical git hook.
- Protect spec contracts: normative meaning must survive compression.

**Non-Goals:**
- Migrating off OpenSpec to cavekit (the 47-change archive, 30 specs, and `validate` CLI are kept).
- Git pre-commit hook (user prefers regression-suite wiring).
- Bulk-compressing the 30 existing specs in this change (infra first; bulk pass is a reviewed follow-up).
- Verifying compression *correctness* mechanically (impossible without an LLM; out of scope).

## Decisions

**D1 — Compressor is a markdown rules file, not a binary or skill.**
Port the `caveman-compress` SKILL.md rules (Remove / Preserve EXACTLY / Preserve Structure / Compress / CRITICAL code-block rule / Boundaries / Pattern) into `openspec/COMPRESSION.md`. Any agent reads it and applies the same transformation in-context. *Alternatives:* (a) per-UI plugin ports — N times the maintenance, drift across tools; (b) a deterministic regex compressor — lossy on contracts, can't match semantic quality. Rejected both.

**D2 — Add an RFC-2119 contract guard the original skill lacks.**
The source skill compressed notes, never contracts. `COMPRESSION.md` adds: never alter SHALL/MUST/SHOULD/MAY/REQUIRED keywords, never drop or merge a normative clause, treat `### Requirement:`/`#### Scenario:` headers as read-only structure. This is the one substantive addition over the verbatim port.

**D3 — Directive placement: AGENTS.md + config.yaml `rules:`.**
AGENTS.md is read by all agents at session start; `openspec/config.yaml` `rules:` is injected by the OpenSpec CLI at artifact-creation time. Putting the directive in both covers both the "writing a spec now" and "general session" paths. *Alternative:* AGENTS.md only — misses the CLI-injected creation context. Rejected.

**D4 — Enforcement = density tripwire, detect not compress.**
`scripts/test_spec_compression.py` globs `openspec/specs/**/spec.md`, strips code fences / tables / headings, computes banned-filler density (the/a/an, just/really/basically/actually/simply, "in order to", "you should", "make sure to"). Over threshold ⇒ fail. It never rewrites. *Alternative:* mechanical auto-compressor in CI — would silently mangle contracts. Rejected; detection-only is safe.

**D5 — Ship the tripwire in threshold/warn posture.**
Existing 30 specs are uncompressed, so a hard zero-tolerance gate would red-fail CI immediately. Pick a threshold that passes today's specs and tightens after the bulk pass, OR scope the test to changed specs only. Start permissive, ratchet later.

## Risks / Trade-offs

- **Semantic compression corrupts a contract's meaning** → D2 guard (RFC-2119 read-only) + one-time human/LLM diff review of the first bulk compression; the tripwire is density-only and cannot catch meaning loss.
- **Agents ignore the directive (no hard enforcement at author time)** → tripwire backstops at regression; non-compliant specs surface as test failures regardless of UI.
- **Threshold mis-tuned (false fails or lets bloat through)** → D5 starts permissive against current specs; tune after bulk pass with real density data.
- **Filler-density is a crude proxy for "compressed"** → accepted; it's a tripwire (catches gross non-compression), not a quality score. Correctness stays a human-review concern.
- **Compressed specs harder for humans to skim** → mitigated: structure/headings/code/tables preserved exactly, only prose tightens; matches the caveman-mode the team already uses.

## Migration Plan

1. Land infra (COMPRESSION.md, directive, tripwire in permissive mode). No existing spec changes ⇒ trivially reversible (delete 2 files + revert 2 edits).
2. Follow-up change: bulk-compress the 30 specs, human-review the diff, then ratchet the tripwire threshold down.
3. Rollback: remove the two new files and the directive lines; specs remain valid either way.

## Open Questions

- Tripwire scope: all specs (with permissive threshold) vs. changed-specs-only (strict)? Lean changed-specs-only so it can ship strict — resolve in tasks.
- Exact banned-filler list and threshold value — finalize against measured density of current specs during implementation.

## Apply Notes - 2026-06-05

### Findings

- Read `AGENTS.md`, `openspec/README.md`, `openspec/specs/task-workflow/spec.md`, this change's proposal/specs/tasks/design, `openspec/config.yaml`, `scripts/test_path_utils.py`, and `scripts/test_gen_config.py`.
- Source compression rules exist at `/Users/Volodymyr_Zhdanov/.claude/plugins/marketplaces/caveman/caveman-compress/SKILL.md`; source comms rule exists at `/Users/Volodymyr_Zhdanov/.claude/rules/caveman.md` plus the active caveman skill.
- Current OpenSpec `rules:` block is still commented; AGENTS.md still names the Claude-specific `caveman` skill.
- Existing script tests use plain Python files with stdlib-only helpers; some use `unittest`, some use top-level assert functions with a `main()`.
- Measured current `openspec/specs/**/spec.md` filler density after stripping code fences, tables, and headings: 32 specs, max 14.61% (`type-d-dynamic-flow/spec.md`), next 13.42%, 12.60%, 11.96%.

### Resolved Tripwire Posture

Use all-specs permissive threshold at 16.0% banned-filler density for this infra change.
Rationale: it passes current uncompressed specs with small headroom over 14.61%, still fails deliberately bloated prose, and is simpler than changed-spec detection without relying on git state. Follow-up bulk-compression pass can ratchet threshold down.

### File Plan

#### openspec/COMPRESSION.md
- Add portable markdown compression rules ported from the caveman-compress skill.
- Add OpenSpec-specific RFC-2119 guard and scope statement for `openspec/specs/**` and `openspec/changes/**` artifact prose.
- Risk: do not imply mechanical auto-rewrite or Claude-only dependency.

#### AGENTS.md
- Add OpenSpec compression directive near OpenSpec Workflow.
- Reword session-start caveman line into tool-agnostic `openspec/COMMS.md` directive.
- Risk: preserve existing session-start banner and cavemem instruction.

#### openspec/config.yaml
- Uncomment/add `rules:` for proposal/design/specs/tasks artifacts.
- Inject pointer to `openspec/COMPRESSION.md`.
- Risk: YAML must remain valid for OpenSpec CLI.

#### scripts/test_spec_compression.py
- Add stdlib `unittest` density tripwire over `openspec/specs/**/spec.md`.
- Strip fenced code, headings, and markdown tables before counting banned filler.
- Add fixture tests proving compressed prose passes, bloated prose fails, code/headings/tables ignored, and repository specs pass at 16.0%.
- Risk: banned terms should count phrases and articles without double-counting phrase words.

#### openspec/COMMS.md
- Add tool-agnostic caveman-full chat rules.
- Add exclusions for commits/PRs, code/comments, user-facing docs, security warnings, and irreversible-action confirmations.
- Risk: exclusions must keep human-facing docs normal prose.

#### openspec/changes/spec-compression-workflow/tasks.md
- Mark completed tasks only after implementation/validation, preserving all task text.
- Add dated validation notes for measured threshold and final checks.
