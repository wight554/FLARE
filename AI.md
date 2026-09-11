> [!IMPORTANT]
> **DEVELOPER AND AI AGENT GUIDE ONLY**
> AI developer environment setup: skills, MCP servers, global tool config. Printer operators: use operator guides in the [README](README.md).

# FLARE — AI Assistance & MCP Setup

**Global-first** AI config. Skills and MCP servers live at user level, shared by `claude-code`, `gemini-cli`, `antigravity`, IDE Copilot.

## Global Configuration Overview

Skills and MCP servers are configured globally at user level, shared by Antigravity, Claude Code, and IDE tools.

## Active MCPs & Tooling

| Tool / MCP | Purpose | Source |
|---|---|---|
| `git` | Version control integration | Global |
| `context7` | Documentation & library search | Global (HTTP/S) |
| `codegraph` | Call-graph and dependency analysis | Global |

## Workspace Rules

- **No local config commits**: `.agent/`, `.agents/`, `.claude/`, `.codex/`, `.gemini/`, `.github/skills/`, `.github/prompts/`, and `skills-lock.json` must NOT be committed. Relies on global config above.
- **Model Attribution**: Include `Generated-By: <Agent> (<Model>)` in commit messages.
- **Workflow**: Follow `AGENTS.md` and `.planning/ROADMAP.md`.

## Antigravity & GSD Setup

Project planning and tracking uses GSD (Get Shit Done) in `.planning/`.
Global skills and MCP tools are managed via Antigravity (`~/.gemini/antigravity/` / `~/.gemini/extensions/`), while Claude Code maintains full compatibility through standard markdown in `.planning/`.

- **Project Vision & Scope**: `.planning/PROJECT.md`
- **Requirements & Traceability**: `.planning/REQUIREMENTS.md`
- **Execution Roadmap**: `.planning/ROADMAP.md`
- **Live State & Session Tracking**: `.planning/STATE.md`
- **Synthesized Specifications**: `.planning/intel/`

### Claude Compatibility

Claude Code uses `CLAUDE.md` to discover project conventions. Because GSD state is entirely markdown-based, Claude can plan and execute directly within `.planning/` without custom extensions.

See `AGENTS.md` for firmware engineering mandates and full session start protocol.

