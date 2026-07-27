> [!IMPORTANT]
> **DEVELOPER AND AI AGENT GUIDE ONLY** — onboarding rules, session protocol, and constraints for developers and coding agents. Printer operators wiring/building/configuring FLARE: see the operator guides in the [README](README.md).

# FLARE — Agent Onboarding

For AI agents (Claude, Gemini, Codex, Opus, Copilot, etc.). Read this first, then `openspec/README.md` and relevant `openspec/specs/` before touching anything. AI environment setup (skills, MCPs): [AI.md](./AI.md). Project uses OpenSpec for durable design tracking.

## Session Start Protocol

Before anything else:
1. Use caveman-full chat style per `openspec/COMMS.md` (tool-agnostic; no Claude-only skill required).
2. Use `cavemem` MCP (or similar persistent memory) for cross-session context.
3. Post: **AGENTS.md ✓ | OpenSpec: [one-line active change/spec summary, or "no active change"]** — lets user verify context loaded.

### AI Assistant Mode (Strict)

Respond in caveman mode (full intensity): terse, no filler, drop articles, fragments OK, short synonyms, technical terms exact. Chat prose only — code, commit messages, PR descriptions stay normal; OpenSpec artifact files follow `openspec/COMPRESSION.md` tiers (`changes/**` full, `specs/**` light).

Pattern:
- **Wrong:** "This repository uses type-P analog sensors because it helps to measure proportional filament position instead of simple endpoints."
- **Right:** "Type-P analog sensor. Proportional position, not endpoints. `BUF_SENSOR_TYPE=1`."

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
- Author change-artifact prose fully compressed per `openspec/COMPRESSION.md`; keep `openspec/specs/**` readable (light compression only). Preserve RFC-2119 clauses and requirement/scenario structure exactly.
- Historical phase/task prose not kept in-tree post-migration; use git history for archaeology. Do not recreate root `TASK.md`.

**Flow triage — direct vs OpenSpec.** Direct implementation only when ALL hold: no spec'd-behavior change (`grep -ril '<topic>' openspec/specs/` empty, or hits but behavior unchanged); no `settings_t`, protocol command, or runtime-tunable surface change; ≤2–3 files; single session; no hardware validation needed. Anything else → OpenSpec change. Unsure → OpenSpec: wrong-direct loses spec sync, wrong-OpenSpec loses only tokens.

**Required before writing any code:**
1. **Research** — use CodeGraph call-graph MCP tools (`codegraph_query`, `codegraph_callers`, `codegraph_impact`) to trace dependencies and find symbol definitions instead of broad file reads/greps. Substantial work: write findings into `design.md` (what read, what learned, constraints).
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

**Grep recipes for `[lookup]` docs:**
- Runtime parameter lookup: `grep -n '<PARAM>' MANUAL.md` — read matched rows only, never the whole file.
- Prior-change context: grep `memories/repo/` first, then `grep -ril '<topic>' openspec/changes/archive/` — **never read archive change dirs wholesale**.

**Spec reading map:** sync/calibration/tuner/analyzer work → start at `openspec/specs/sync-refactor/spec.md` (Purpose + headers), then the relevant phase spec (`calibration-workflow`, `bucket-locking`, `analyzer-rigor`, …). Workflow/task rules → `openspec/specs/task-workflow/spec.md`. Firmware architecture/gotchas → `openspec/specs/project-architecture/spec.md` + `CONTEXT.md`. Old rationale/prompts → git history.

## Memory

Two layers: `memories/repo/` (team, git-tracked, one curated observation file per archived change) and cavemem / tool-private memory (personal, automatic). Full rules: `memories/repo/README.md`.
Before proposing or re-deriving prior art: `grep -ril '<topic>' memories/repo/`, then personal memory — cite hits instead of re-investigating; verify stale refs against the tree.
Observation file is written as part of each change's Readiness checklist, before archive. Never put secrets or source snippets in observations.

---

## Key Files

Read modes: `[always]` = read every session (`AGENTS.md`, `openspec/COMMS.md` — nothing else). `[lookup]` = grep on demand, read matched sections only, **never wholesale** (several are 28–41 KB).

| File | Read | Contains |
|------|------|----------|
| `firmware/src/main.c` | [lookup] | Top-level init, runtime globals, autopreload, LEDs, main-loop orchestration |
| `firmware/src/motion.c` | [lookup] | Per-lane motion, debounced sensors, motor helpers, lane tasks |
| `firmware/src/sync.c` | [lookup] | Sync orchestration, buffer-lock state, boot stabilization |
| `firmware/src/sync_buf.c` | [lookup] | Buffer sensing, virtual position, Type-D/Type-P signal publishing, estimator updates |
| `firmware/src/sync_relay.c` | [lookup] | Type-D relay control, neutral feed sampling, relay trim |
| `firmware/src/sync_analog.c` | [lookup] | Type-P analog sync control helpers, reserve metrics, PSF control law |
| `firmware/src/toolchange.c` | [lookup] | Cutter sequencing, toolchange flow, RELOAD orchestration |
| `firmware/src/protocol.c` | [lookup] | USB serial command parsing, motion/system commands, SET/GET |
| `firmware/src/protocol_status.c` | [lookup] | `ST:` status dump formatting |
| `firmware/src/protocol_tmc.c` | [lookup] | Advanced TMC serial commands (`CA:`, `TW:`, `TR:`, `RR:`) |
| `firmware/src/settings_store.c` | [lookup] | Flash-backed settings defaults/save/load, TMC apply helpers |
| `firmware/include/controller_shared.h` | [lookup] | Shared runtime types, globals, conversion helpers |
| `firmware/include/protocol_internal.h` | [lookup] | Shared protocol split-unit declarations |
| `firmware/include/sync_internal.h` | [lookup] | Shared sync split-unit declarations |
| `firmware/include/buf_signal.h` | [lookup] | Buffer signal source/types shared by sync modules |
| `firmware/include/config.h` | [lookup] | Board constants + pin map; includes generated `tune.h` |
| `firmware/include/tune.h` | [lookup] | Generated by `scripts/gen_config.py` from `config.ini` — **gitignored** |
| `CONTEXT.md` | [lookup] | Deep firmware reference: data structures, settings pattern, gotchas, navigation (see "When to Read CONTEXT.md") |
| `BEHAVIOR.md` | [lookup] | State machines, RELOAD flow, sync estimator, tuning procedures |
| `MANUAL.md` | [lookup] | Full command + runtime parameter reference |
| `TUNING.md` | [lookup] | Operator tuning guide, analyzer/tuner workflows |
| `TEST_CASES.md` | [lookup] | Hardware/regression test case catalog |
| `KLIPPER.md` | [lookup] | Klipper integration: shell helper, toolchange macros, sync tuning |
| `HARDWARE.md` | [lookup] | Board pinout, sensor wiring |
| `BUILD_FLASH.md` | [lookup] | Build + flash instructions |
| `scripts/flare_cmd.py` | [lookup] | Single-command serial helper for Klipper shell integration |
| `STYLE.md` | [lookup] | Coding style, naming standards, formatting, linting guide |
| `REVIEW.md` | [lookup] | Pre-commit staged-diff self-review checklist |
| `memories/repo/` | [lookup] | Team memory: per-change observations (prior art — grep first) |
| `openspec/specs/` | [lookup] | Current OpenSpec behavioral contracts |
| `tests/host/` | [lookup] | Host-compiled sync simulation (`flare_sim`) — links real `sync*.c`/`motion.c`/`toolchange.c`/`cutter.c`/`settings_store.c` against fakes; sim screens deadlock/sign/timer defects, rig remains sole authority on tuning quality — a sim pass never satisfies an `HW:` task |

---

## Build

```bash
ninja -C build_local        # verify before every commit
```

Cross-compiler must be in PATH. If `build_local/` missing:
```bash
cmake -S firmware -B build_local -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk
```

Build the **dev-tuning superset** before committing firmware: the Pi builds with
`-DFLARE_DEV_TUNING=1`, and code behind `#ifdef FLARE_DEV_TUNING` (Tier-3 SET/GET in
`protocol.c`) is invisible to a default (OFF) build and to `clang-tidy` (inactive
preprocessor branch). `scripts/validate_regression.py` configures `build_local` with
`-DFLARE_DEV_TUNING=ON`; reconfigure manually with `cmake -S firmware -B build_local
-DFLARE_DEV_TUNING=ON` if building by hand. Bulk lint/refactor tooling must also cover
dev-guarded code (`clang-tidy ... --extra-arg=-DFLARE_DEV_TUNING=1`).

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
13. **Self-review before commit** — for any non-doc-only commit, check the staged diff against `REVIEW.md` and resolve violations first. Doc-only commits may skip.

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
