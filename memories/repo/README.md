# Repo memories — usage for AI tools

Team memory store. Spec: `openspec/specs/team-memory-store/spec.md` (after archive). Two memory layers — know which one you're touching.

| Layer | `memories/repo/` (this dir) | cavemem / Claude auto-memory |
|---|---|---|
| Scope | Team, git-tracked, reviewed | Personal, per-developer/per-tool |
| Write | Curated: one file per archived change | Automatic capture; not committed |
| Read | `grep`/read by any tool (Claude, Codex, Gemini, Copilot, human) | Tool-specific (cavemem MCP `search`/`timeline`; Claude memory dir) |
| Content | Decisions, gotchas, deviations from design | Raw session context |

This dir is committed BY DESIGN — team store, not tool-local AI config. `AI.md` no-local-AI-config rule does not apply here.

## Write rules

- One file per archived change: `<change-name>.md`, matching the `openspec/changes/` name.
- 3–5 caveman-compressed lines: decisions made, gotchas hit, deviations from design + why. Date them.
- Name specs/components explicitly so files stay greppable (e.g., `sync_relay`, `psf-type-p-sensor`, `SETTINGS_VERSION`).
- NEVER include secrets, tokens, credentialed URLs, or firmware/source code snippets.
- Written as part of the change's "Readiness and Delivery Checks" before archiving (enforced via `openspec/config.yaml` tasks rules).
- Do NOT bulk-copy personal memory stores here; only curated per-change observations.

## Read protocol (all AI tools)

Before proposing a change or re-deriving prior art on existing specs/components:

1. `grep -ril '<component-or-spec>' memories/repo/` — team memory.
2. Personal layer when available (cavemem MCP `search`, Claude auto-memory).
3. Cite findings in proposal/design instead of re-investigating from source or git history.
4. Verify before acting: observations reflect the tree at write time — named files/params/behaviors may have changed. Update or flag stale entries.
