# Proposal: agent-token-efficiency

## Why

prior-project A/B-validated token optimizations exist that FLARE has not ported: team memory store, doc read-mode tags, self-contained task rules, compression tiers, flow triage. FLARE pays the cost today: prior-art decisions (relay deadlock, purge-grind macro root cause) live only in Claude-private memory, so Gemini/Codex/Copilot sessions re-derive them; 28–41 KB root docs get read wholesale; trivial fixes carry full OpenSpec overhead; specs compressed against own readability tripwire.

## What Changes

- Add git-tracked team memory store `memories/repo/` — one caveman-compressed observation file per archived change + read protocol (grep before re-deriving prior art). Works for any agent tool.
- Tag AGENTS.md Key Files with `[always]` / `[lookup]` read modes; add grep-don't-read pitfalls for big docs (`MANUAL.md`, `BEHAVIOR.md`, `TEST_CASES.md`, big specs, `openspec/changes/archive/**`).
- Harden `openspec/config.yaml` artifact rules: self-contained tasks (file path + exact change + acceptance criteria per task), mechanical steps as CLI commands, final "Readiness and Delivery Checks" section (build superset, py_compile, doc sync, `openspec validate --strict`, memory observation), `HW:` tagging for hardware-dependent tasks, grep memories before proposal/design.
- Split compression tiers in `openspec/COMPRESSION.md` + `config.yaml`: `openspec/specs/**` light/readable, `openspec/changes/**` full caveman. Forward-only; no retroactive decompression.
- Add flow triage rule to AGENTS.md: measurable direct-vs-OpenSpec routing criteria; unsure → OpenSpec.
- Add tool-agnostic `REVIEW.md` self-review checklist distilled from non-negotiables; AGENTS.md requires staged-diff self-review against it before commit.
- Add targeted-edits-only output rule (never echo unchanged code blocks in chat/PR) to `openspec/COMMS.md`.
- No firmware code changes. Docs/config/workflow only.

## Capabilities

### New Capabilities

- `team-memory-store`: git-tracked cross-tool memory layer — write rules (one file per archived change, 3–5 compressed lines, no secrets/source), read protocol (grep before re-deriving), archive-time enforcement.

### Modified Capabilities

- `task-workflow`: add requirements — doc read modes (lookup = grep, never wholesale), flow triage (direct vs OpenSpec criteria), readiness/delivery checks incl. strict validation + `HW:` task tagging, compression tiers (specs light, changes full), self-review against `REVIEW.md`, targeted-edits output rule.

## Impact

- Edited: `AGENTS.md`, `openspec/COMPRESSION.md`, `openspec/config.yaml`, `openspec/COMMS.md`, `openspec/README.md` (layout row for memories), `.gitignore` untouched (memories tracked).
- New: `memories/repo/README.md`, `REVIEW.md`, `openspec/specs/task-workflow` delta, `openspec/specs/team-memory-store/spec.md`.
- No `firmware/`, `scripts/`, `config.ini` changes. No tool-local AI config committed (AI.md rule holds).
