# Design: agent-token-efficiency

## Context

Source: analysis of `prior-project` token optimizations (2026-06-10, A/B-validated there). FLARE already ports core pieces: `openspec/COMPRESSION.md`, `openspec/COMMS.md`, 1-line tool stubs → `AGENTS.md`, global-first skills (`AI.md`), lazy spec reads. Delta = team memory store, read-mode tags, config.yaml artifact-rule hardening, compression tiers, flow triage, self-review checklist, targeted-edits rule.

Current pain:

- Prior-art root causes (relay min-flip deadlock, purge-grind macro, estimator failures) live in Claude-private auto-memory + cavemem only. Gemini/Codex/Copilot sessions re-derive via git/spec archaeology.
- Root docs big: `BEHAVIOR.md` 41 KB, `MANUAL.md` 39 KB, `TEST_CASES.md` 28 KB, `openspec/specs/sync-refactor/spec.md` 25 KB. AGENTS.md has lazy-spec rule but no per-doc read modes; wholesale reads still happen.
- `openspec/config.yaml` tasks rules minimal: generated tasks not self-contained, no readiness checklist, no strict-validate gate, no `HW:` tagging at generation time.
- `config.yaml` compresses `openspec/specs/**` fully while archive workflow has spec-readability tripwire — config contradicts practice. prior-project resolved same tension: specs light, changes full.
- No triage criteria for direct-vs-OpenSpec routing; "substantial" undefined.
- No committed review checklist; review re-loads `AGENTS.md` + `CONTEXT.md` wholesale.

Constraint: tool-agnostic. Everything must work for Claude Code, Codex, Gemini/Antigravity, Copilot via plain committed markdown/yaml. No tool-local config (`.claude/`, `.github/agents/`, `applyTo` frontmatter) — AI.md rule holds.

## Goals / Non-Goals

**Goals:**

- Cross-tool durable prior-art store; read-before-re-derive protocol.
- Cut wholesale reads of large docs via explicit read modes + grep recipes.
- Generated tasks executable without re-reading proposal/design (self-contained).
- Archive gate: strict validation + observation append + readiness checklist.
- Codify spec-readable / change-compressed tiers.
- Measurable direct-vs-OpenSpec triage.
- Single self-review checklist replacing wholesale rule-doc re-reads.

**Non-Goals:**

- No firmware/`scripts/` changes. No `config.ini`/`tune.h` surface.
- No retroactive decompression of existing specs (churn, git-blame noise).
- No port of Copilot-only mechanisms: `.github/instructions/*.instructions.md` `applyTo` auto-attach, agent handoff chains, `.agent.md` frontmatter.
- No nested per-dir AGENTS.md (repo small; Key Files table suffices).
- No CHANGELOG mechanism (prior-project ticket-specific).
- cavemem / Claude auto-memory unchanged — they stay personal layers.

## Decisions

1. **Memory store path `memories/repo/`** (repo root, prior-project convention). Alternative `openspec/memories/` rejected: memory outlives change lifecycle, convention portability across repos worth keeping. README in dir owns write/read rules; one file per archived change `<change-name>.md`.
2. **Two-layer memory model documented, not merged.** `memories/repo/` = team, git-tracked, curated. cavemem/Claude auto-memory = personal, automatic. Agents write team layer only at archive readiness; personal layers untouched.
3. **Seed store from known prior art.** Initial observations for already-archived high-value changes (relay-fallback-only, compression-overfeed-stop, psf stale-fault-timers, audit rounds, shell-to-python-port). Source: existing session memory + git history. Caps: 3–5 lines each, dated, no secrets/source snippets.
4. **task-workflow delta uses ADDED requirements only.** New concerns (read modes, triage, readiness, tiers, self-review, targeted edits) — existing requirement blocks untouched, avoids MODIFIED copy-drift at archive.
5. **`REVIEW.md` at repo root** next to `AGENTS.md`/`STYLE.md`, plain markdown checklist distilled from Non-Negotiable Rules + protocol parity + persistence/doc-sync invariants. Alternative `.github/agents/firmware-reviewer.agent.md` rejected: tool-local, violates AI.md; frontmatter useless to non-Copilot tools.
6. **Compression tiers forward-only.** `COMPRESSION.md` gains Scope & Tiers section (specs light/readable, changes full caveman); `config.yaml` specs rule flips to light compression. Existing compressed specs left as-is; rewrite only when touched for content reasons.
7. **Enforcement via `config.yaml` rules + spec contract.** config.yaml rules shape artifact generation (OpenSpec-aware tools read them); task-workflow spec makes same behavior durable contract for non-OpenSpec-aware sessions. Both updated together.
8. **`HW:` tagging at generation time.** config.yaml tasks rule: hardware-dependent validation tasks MUST carry `HW:` prefix. Pairs with existing AGENTS.md rule 12 (never check off without user confirmation + hardware results).

## Risks / Trade-offs

- [Stale observations mislead] → read protocol mandates verify named files/params still exist before acting; observations dated.
- [Store bloat] → 3–5 line cap, one file per archived change, README enforces.
- [config.yaml rules grow instruction payload per artifact] → rules terse one-liners; index docs not restated (prior-project anti-duplication rule).
- [Tier split leaves mixed-style specs] → accepted; forward-only policy documented, readability tripwire unaffected (checks Purpose placeholder).
- [Triage misroute direct → spec drift] → asymmetry stated in rule: unsure → OpenSpec; wrong-OpenSpec costs only tokens.
- [REVIEW.md drifts from AGENTS.md rules] → REVIEW.md links rule numbers instead of restating where possible; doc-sync rule 6 covers renames.

## Migration Plan

Docs-only, single change, direct commits to `main` per WORKFLOW.md. No rollback complexity — `git revert` per commit. Archive folds `team-memory-store` spec + `task-workflow` delta; set real Purpose on new spec before commit (archive-Purpose gotcha).

## Open Questions

None blocking.
