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
- **Workflow**: Follow `AGENTS.md` and `openspec/specs/task-workflow/spec.md`.

## OpenSpec / OpsX Setup

Repo keeps only project OpenSpec data in `openspec/`. Tool-specific
OpenSpec/OpsX skills and commands stay global so every project reuses workflow
without committed local agent config.

Global OpenSpec skill locations on this machine:

| Tool | Global path |
|---|---|
| Codex | `~/.codex/skills/openspec-*` |
| Claude | `~/.claude/skills/openspec-*`, `~/.claude/commands/opsx/*.md` |
| Gemini | `~/.gemini/skills/openspec-*`, `~/.gemini/commands/opsx/*.toml` |
| Generic agents | `~/.agents/skills/openspec-*`, `~/.agents/workflows/opsx-*.md` |
| GitHub/Copilot | `~/.github/skills/openspec-*`, `~/.github/prompts/opsx-*.prompt.md` |

To initialize OpenSpec in a project, commit project spec dir only:

```bash
mkdir -p openspec
cat > openspec/config.yaml <<'YAML'
schema: spec-driven
context: |
  Tech stack: <project stack>
  Domain: <project domain>
  Conventions: <commit/test/doc rules>
YAML
```

Do not copy `.claude/`, `.codex/`, `.gemini/`, `.agent/`, or `.github/skills`
into project. If project needs custom OpenSpec behavior, encode it in
`openspec/config.yaml` or committed specs, not tool-local skill folders.

Durable behavioral contracts live in `openspec/specs/<area>/spec.md`.
Substantial changes start in `openspec/changes/<change-id>/` with
proposal/design/tasks, then fold durable outcomes back into `openspec/specs/`.
Do not keep migrated historical phase/task archives in-tree; use git history.

See `AGENTS.md` for firmware engineering mandates and full session start protocol.
