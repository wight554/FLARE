> [!IMPORTANT]
> **DEVELOPER AND AI AGENT GUIDE ONLY** — onboarding rules, session protocol, and constraints for developers and coding agents. Printer operators wiring/building/configuring FLARE: see the operator guides in the [README](README.md).

# FLARE — Agent Onboarding

For AI agents (Claude, Gemini, Codex, Opus, Copilot, etc.). Read this first, then `openspec/README.md` and relevant `openspec/specs/` before touching anything. AI environment setup (skills, MCPs): [AI.md](./AI.md). Project uses OpenSpec for durable design tracking.

## Session Start Protocol

Before anything else:
1. Use caveman-full chat style per `openspec/COMMS.md` (tool-agnostic; no Claude-only skill required).
2. Use `cavemem` MCP (or similar persistent memory) for cross-session context.
3. Post: **AGENTS.md ✓ | OpenSpec: [one-line active change/spec summary, or "no active change"]** — lets user verify context loaded.

---

## What This Project Is

**FLARE** = **Filament Lane Automation and Reload Engine**: standalone dual-lane MMU / RELOAD controller firmware for RP2040 on FYSETC ERB V2.0. Name applies to firmware + host tooling, not the mechanical module. Active development — breaking namespace changes allowed when they keep the tree coherent.

- Two TMC2209 stepper drivers, one per lane, over UART
- Per-lane IN / OUT filament switches; optional Y-splitter switch; optional toolhead sensor (TS:)
- Sync-Feedback Sensor: type D dual-endstop (default, `BUF_SENSOR_TYPE=0`, D=0) or type P analog / Hall-effect (`BUF_SENSOR_TYPE=1`, P=1)
- USB CDC serial at 115200 baud — `CMD:params\n`, responses `OK:...` / `ER:...`, events `EV:...`
- No Klipper plugin required; `scripts/flare_cmd.py` bridges to Klipper macros when needed

Two modes via `RELOAD_MODE`:
- `0` = MMU (manual or Klipper-triggered toolchange via `TC:`)
- `1` = RELOAD (auto-switch on runout — 3-state: WAIT_Y → APPROACH → FOLLOW)

---

## OpenSpec Workflow

Durable design history + behavioral contracts live in `openspec/`. Context windows finite — research-then-code without writing down risks losing work mid-task. **Write first in OpenSpec artifacts.**

- Current durable behavior → `openspec/specs/`. Substantial active work → `openspec/changes/<change-id>/` (`proposal.md`, `design.md`, `tasks.md`) before implementation.
- Author artifact prose compressed per `openspec/COMPRESSION.md`, preserving RFC-2119 clauses and requirement/scenario structure exactly.
- Historical phase/task prose not kept in-tree post-migration; use git history for archaeology. Do not recreate root `TASK.md`.

**Required before writing any code:**
1. **Research** — read source, grep symbols, understand current state. Substantial work: write findings into `design.md` (what read, what learned, constraints).
2. **Plan** — draft implementation plan in the change/design note before opening editor. Per file to modify: path, exact change + why, risk/invariant to watch. If task changes durable behavior or workflow, create/update the OpenSpec artifact first.
   ```
   ### firmware/src/protocol.c + firmware/src/settings_store.c
   - Add GET/SET plumbing for JOIN_RATE so reload approach speed is tunable
   - Wire the runtime value through the persistence helpers in settings_store.c
   - Risk: keep config.ini, MANUAL.md, and generated tune.h in sync
   ```
3. **Implement** — work file by file. After each durable unit: update task list/design note with steps, validation, commit SHA; commit + push immediately. For new features, note which flows could regress and how to validate.
4. **Never hold more than one file's worth of changes in memory** before committing.
5. **Preserve `tasks.md` history** — never empty/truncate/delete the task list of an active change. Mark lines `[x]`, append dated validation notes. Must stay reconstructable at archive.

**Read specs lazily — do NOT load whole specs.** Specs are large dense contracts; reading one in full burns context for one edit. Instead: read the spec's `## Purpose` + the `### Requirement:` headers as an index, then read only the specific requirement/scenario block you are touching (use offset/range reads). Pull more sections only when a task actually needs them.

**Spec reading map:** sync/calibration/tuner/analyzer work → start at `openspec/specs/sync-refactor/spec.md` (Purpose + headers), then the relevant phase spec (`calibration-workflow`, `bucket-locking`, `analyzer-rigor`, …). Workflow/task rules → `openspec/specs/task-workflow/spec.md`. Firmware architecture/gotchas → `openspec/specs/project-architecture/spec.md` + `CONTEXT.md`. Old rationale/prompts → git history.

---

## Key Files

| File | Contains |
|------|----------|
| `firmware/src/main.c` | Top-level init, runtime globals, autopreload, LEDs, main-loop orchestration |
| `firmware/src/motion.c` | Per-lane motion, debounced sensors, motor helpers, lane tasks |
| `firmware/src/sync.c` | Buffer sensing, estimator-driven sync, boot stabilization |
| `firmware/src/toolchange.c` | Cutter sequencing, toolchange flow, RELOAD orchestration |
| `firmware/src/protocol.c` | USB serial command parsing, status dump, SET/GET, advanced TMC commands |
| `firmware/src/settings_store.c` | Flash-backed settings defaults/save/load, TMC apply helpers |
| `firmware/include/controller_shared.h` | Shared runtime types, globals, conversion helpers |
| `firmware/include/config.h` | Board constants + pin map; includes generated `tune.h` |
| `firmware/include/tune.h` | Generated by `scripts/gen_config.py` from `config.ini` — **gitignored** |
| `CONTEXT.md` | Deep firmware reference: data structures, settings pattern, gotchas, navigation |
| `BEHAVIOR.md` | State machines, RELOAD flow, sync estimator, tuning procedures |
| `MANUAL.md` | Full command + runtime parameter reference |
| `KLIPPER.md` | Klipper integration: shell helper, toolchange macros, sync tuning |
| `HARDWARE.md` | Board pinout, sensor wiring |
| `BUILD_FLASH.md` | Build + flash instructions |
| `scripts/flare_cmd.py` | Single-command serial helper for Klipper shell integration |
| `STYLE.md` | [STYLE.md](file:///Users/Volodymyr_Zhdanov/playground/FLARE/STYLE.md) | Coding style, naming standards, formatting, linting guide |
| `openspec/specs/` | Current OpenSpec behavioral contracts |

---

## Build

```bash
ninja -C build_local        # verify before every commit
```

Cross-compiler must be in PATH. If `build_local/` missing:
```bash
cmake -S firmware -B build_local -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk
```

## When to Read CONTEXT.md

Load `CONTEXT.md` when task touches: a runtime parameter (full 10-step checklist there); RELOAD approach/follow logic; `settings_t`, `lane_t`, or any state machine; exact data layout or known gotchas. Skip for doc-only, script, or build/config work — save context budget.

---

## Non-Negotiable Rules

1. **Build must pass before EVERY commit.** `ninja -C build_local` (or `cmake --build build_clang`). Skip only if purely docs. Broken build = failed task.
2. **Python validation** — `python3 -m py_compile scripts/*.py` before every commit touching scripts.
3. **Commit + push after every change, automatically, without asking.** Don't ask "should I commit?" — do it.
4. **Bump `SETTINGS_VERSION`** in `settings_store.c` when a `settings_t` field is added/removed. Grep current version.
5. **No mock/stub hardware** — all changes must compile against real Pico SDK target.
6. **Documentation sync** — every finished task validates against all docs (`MANUAL.md`, `BEHAVIOR.md`, etc.). Code parameter renames MUST update everywhere in docs.
7. **Runtime tunables live in `config.ini`** — no tuning defaults only in firmware C headers. Add keys to `config.ini`/`config.ini.example`, wire through `scripts/gen_config.py` into `tune.h`, consume `CONF_*` in firmware.
8. **Runtime tunable protocol parity** — for any runtime/serial tunable, verify the full surface together: `SET:` handler, matching `GET:` handler, `scripts/flare_cmd.py --dump` entry (if it belongs in live dumps), and docs. Don't stop after `SET:`.
9. **Regression impact for new features** — unless user asks to change behavior, every new feature needs code-level impact review of affected flows (preload, load, unload, toolchange, sync, RELOAD, persistence, protocol, docs) and validation they stay intact.
10. **Prefer Git MCP for git ops when available** — `status`, `diff`, `add`, `commit`, `log`, `show`, `branch`, `checkout`. Fall back to non-interactive shell git when Git MCP is unavailable, lacks the op, fails, or for push/remotes.
11. **Do NOT commit local AI config** — never commit `.agents/`, `.claude/`, or `skills-lock.json`. AI config stays global per `AI.md`.
12. **Never check physical validation / `HW:` tasks** — never check off hardware-dependent tasks (or `HW:`-prefixed) without explicit user confirmation + real-hardware test results.

## Commit Format

```
<short description>

<body — what changed and why>

Generated-By: <Agent Name> (<Model>)
```

- Subject: lowercase, imperative, no period, ≤ 72 chars. Body: explain *why*.
- **Always include model in `Generated-By`:** e.g. `GitHub Copilot (Claude Haiku 4.5)`, `Gemini 3.1 Pro (High)`, `Antigravity (Gemini 3.1 Flash)`. Audit trail; no need to repeat in chat.
- **Preserve attribution.** If Claude produced/substantially assisted, keep its `Co-Authored-By:` trailer. Other tools add `Generated-By:` lines *in addition*, not instead. Multi-tool commit = one `Generated-By:` line per tool/model.
- Push immediately: `git push` (shell git unless a reliable push-capable MCP exists).

---

## Current Work

No root `TASK.md`. Sources: `openspec/changes/` (active spec-driven work), `openspec/specs/` (durable contracts), git history (old ledgers, migrated phase prose). If no active change: run `git log --oneline -20`, read relevant specs, infer/ask the active task.
