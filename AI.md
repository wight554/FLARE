> [!IMPORTANT]
> **DEVELOPER AND AI AGENT GUIDE ONLY**
> AI developer environment setup: skills, MCP servers, global tool config. Printer operators: use operator guides in the [README](README.md).

# FLARE — AI Assistance & MCP Setup

**Global-first** AI config. Skills and MCP servers live at user level, shared by `claude-code`, `gemini-cli`, `antigravity`, IDE Copilot.

## Global Configuration Overview

All AI tools use shared home-dir env:

- **Primary Source**: `~/.gemini/extensions/caveman/`
- **Claude Integration**: `~/.claude/skills/` (linked to Gemini source)
- **MCP Servers**: Managed via global `node` and `npx`
- **Memory**: Persistent cross-session memory via `cavemem` MCP

## Prerequisites

- **Node.js**: v22+ (v22.20.0 recommended)
- **Python**: v3.12+ (v3.14.4 recommended)
- **Anthropic / Google API Keys**: Export in shell profile

## Initial Setup (One-Time Global)

### 1. Install Caveman Extension
Follow instructions at [JuliusBrussee/caveman](https://github.com/JuliusBrussee/caveman).

### 2. Link Claude Skills
Create global symlinks so `claude-code` uses same skills:
```bash
ln -sfn ~/.gemini/extensions/caveman/skills/caveman ~/.claude/skills/caveman
ln -sfn ~/.gemini/extensions/caveman/skills/caveman-commit ~/.claude/skills/caveman-commit
ln -sfn ~/.gemini/extensions/caveman/skills/caveman-help ~/.claude/skills/caveman-help
ln -sfn ~/.gemini/extensions/caveman/skills/caveman-review ~/.claude/skills/caveman-review
ln -sfn ~/.gemini/extensions/caveman/skills/caveman-stats ~/.claude/skills/caveman-stats
ln -sfn ~/.gemini/extensions/caveman/skills/cavecrew ~/.claude/skills/cavecrew
ln -sfn ~/.gemini/extensions/caveman/skills/compress ~/.claude/skills/compress
ln -sfn ~/.gemini/extensions/caveman/skills/caveman-compress ~/.claude/skills/caveman-compress
```

### 3. Configure MCP Servers
Add `cavemem` MCP to `~/.claude/settings.json` and `~/.gemini/settings.json`:

```json
{
  "mcpServers": {
    "cavemem": {
      "command": "node",
      "args": ["/path/to/your/global/node_modules/cavemem/dist/index.js", "mcp"]
    }
  }
}
```

## Active MCPs in this Repo

| MCP | Purpose | Source |
|---|---|---|
| `cavemem` | Persistent, compressed cross-agent memory | Global (via node) |
| `context7` | Documentation & library search | Global (HTTP/S) |
| `git` | Git integration (branching, commits) | Global (via npx) |

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

