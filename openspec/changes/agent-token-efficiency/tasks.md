# Tasks: agent-token-efficiency

Docs/config only — no firmware, no `scripts/`, no `config.ini`. Each task self-contained: path, exact change, acceptance.

## 1. Team Memory Store

- [x] 1.1 Create `memories/repo/README.md`: layer table (team `memories/repo/` git-tracked curated vs cavemem/Claude auto-memory personal automatic); write rules (one file per archived change `<change-name>.md`, 3–5 compressed lines: decisions/gotchas/deviations + why, greppable spec/component names, NEVER secrets/tokens/credentialed URLs/source snippets); read protocol (`grep -ril '<topic>' memories/repo/` before proposal/design touching existing specs/components, cite hits, verify stale refs against tree). Accept: covers all 4 `team-memory-store` delta requirements.
- [x] 1.2 Seed observations: (done 2026-06-11 — 10 files seeded, attribution-confident subset) `ls openspec/changes/archive/`, write one `memories/repo/<change-name>.md` per archived change with durable root-cause value (expected: relay fallback-only verdict, compression overfeed stop, psf stale fault timers, shell-to-python port, earlier sync work). Source: git log + change artifacts (grep, not wholesale). Accept: each file ≤5 lines, dated, names specs/components, no secrets/source.

## 2. AGENTS.md Read Modes + Triage + Memory Protocol

- [x] 2.1 `AGENTS.md` Key Files table: add read-mode tag per row — `[always]` only for entries needed every session; `[lookup]` for `MANUAL.md`, `BEHAVIOR.md`, `TEST_CASES.md`, `TUNING.md`, `KLIPPER.md`, `HARDWARE.md`, specs. Add legend line: `[lookup]` = grep on demand, never wholesale. Accept: every row tagged.
- [x] 2.2 `AGENTS.md`: add grep recipes near lazy-spec rule — param lookup `grep -n '<PARAM>' MANUAL.md` read matched rows only; never read `openspec/changes/archive/**` dirs wholesale (grep topic, prefer `memories/repo/`). Accept: both recipes present.
- [x] 2.3 `AGENTS.md`: add "Flow Triage" subsection under OpenSpec Workflow — direct only when ALL: no spec'd-behavior change (`grep -ril '<topic>' openspec/specs/` empty or behavior unchanged), no `settings_t`/protocol/tunable surface, ≤2–3 files, single session, no HW validation; else OpenSpec; unsure → OpenSpec (wrong-direct loses spec sync, wrong-OpenSpec loses only tokens). Accept: criteria verbatim-equivalent to task-workflow delta.
- [x] 2.4 `AGENTS.md`: add "Memory" subsection — two layers, read protocol before re-deriving, observation written at archive readiness; link `memories/repo/README.md`. Accept: session-start protocol unchanged, subsection ≤6 lines.
- [x] 2.5 `AGENTS.md`: add self-review rule to Non-Negotiable Rules — before committing non-doc-only changes, check staged diff against `REVIEW.md`. Accept: rule references REVIEW.md, existing rule numbering preserved (append, don't renumber).

## 3. Compression Tiers + config.yaml Hardening

- [x] 3.1 `openspec/COMPRESSION.md`: replace `## Scope` with "Scope & Tiers" — `openspec/specs/**` lightly compressed or uncompressed (stable long-lived contracts, readability paramount); `openspec/changes/**` fully compressed (iteration-heavy drafts); forward-only, no retroactive spec rewrite. Contract Guard unchanged. Accept: tiers match task-workflow Compression Tiers requirement.
- [x] 3.2 `openspec/config.yaml` specs rules: change "Author OpenSpec prose compressed" to keep specs readable, light compression only per `openspec/COMPRESSION.md`; normative-structure rules unchanged. Accept: no full-compression mandate on specs remains.
- [x] 3.3 `openspec/config.yaml` proposal+design rules: add grep `memories/repo/` for touched specs/components before drafting; cite findings instead of re-deriving. Accept: one rule line each.
- [x] 3.4 `openspec/config.yaml` tasks rules: add — every task names target file path, exact change, acceptance criteria (self-contained execution); mechanical steps as CLI commands; final section "Readiness and Delivery Checks" requiring dev-tuning superset build (`cmake -S firmware -B build_local -DFLARE_DEV_TUNING=ON` + `ninja -C build_local`) for firmware changes, `python3 -m py_compile scripts/*.py` for script changes, doc sync verification, `openspec validate <change-name> --strict` + `openspec validate --specs --strict`, append `memories/repo/<change-name>.md`; hardware-dependent validation tasks prefixed `HW:`, never checked without explicit user confirmation + real-hardware results. Accept: all six rule lines present, terse.

## 4. REVIEW.md Checklist

- [ ] 4.1 Create `REVIEW.md` (repo root): staged-diff self-review checklist distilled from AGENTS.md Non-Negotiable Rules — build superset passes; `SETTINGS_VERSION` bumped on `settings_t` field add/remove; runtime tunable full surface (`SET:`/`GET:`/`flare_cmd.py --dump`/docs); `config.ini` → `gen_config.py` → `tune.h` wiring for new tunables; doc sync (`MANUAL.md`, `BEHAVIOR.md`, …) on renames; regression impact review for new features (preload/load/unload/toolchange/sync/RELOAD/persistence/protocol); no local AI config committed; commit format + attribution. Reference rule numbers, don't restate detail. Accept: every checklist item maps to an AGENTS.md rule or spec; ≤40 lines; readable prose (operator-adjacent doc, no caveman).

## 5. COMMS.md Output Rule

- [ ] 5.1 `openspec/COMMS.md`: add targeted-edits rule to Exclusions-adjacent section — report edits as targeted changes; never echo unchanged code blocks in chat, commit messages, or PR descriptions; reference paths + line ranges. Accept: single bullet, placement coherent.

## 6. Readiness and Delivery Checks

- [ ] 6.1 Doc sync: cross-references consistent — `AGENTS.md` ↔ `REVIEW.md` ↔ `memories/repo/README.md` ↔ `openspec/COMPRESSION.md` links resolve; `AI.md` workspace rules not contradicted (memories/repo is committed BY DESIGN — team store, not tool-local config).
- [ ] 6.2 `openspec validate agent-token-efficiency --strict` passes.
- [ ] 6.3 `openspec validate --specs --strict` passes for touched specs after archive folding; set real `## Purpose` on new `openspec/specs/team-memory-store/spec.md` before archive commit (archive stamps placeholder otherwise).
- [ ] 6.4 Append `memories/repo/agent-token-efficiency.md` observation (3–5 lines) before archiving.
