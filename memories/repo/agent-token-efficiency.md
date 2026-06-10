# agent-token-efficiency (implemented 2026-06-11)

- Ported prior-project A/B-validated token optimizations tool-agnostically: this store (`memories/repo/`), `[always]`/`[lookup]` read modes + grep recipes in AGENTS.md, flow triage, config.yaml self-contained-task/readiness/`HW:` rules, spec-light/changes-full compression tiers (COMPRESSION.md), root `REVIEW.md`, COMMS.md targeted-edits rule.
- Deliberately NOT ported (Copilot-only): `.github/instructions/*.instructions.md` `applyTo` auto-attach, `.agent.md` frontmatter/handoffs — would violate AI.md no-tool-local-config rule.
- Tier split forward-only: existing compressed specs stay as-is until touched for content.
- Seeded 10 prior-art observations from archive; attribution-uncertain root causes (e.g., sync limit-cycle estimator era) intentionally skipped rather than misattributed.
- Specs delta: new `team-memory-store`, ADDED-only delta to `task-workflow` (8 requirements) — set real Purpose on team-memory-store at archive (placeholder gotcha).
