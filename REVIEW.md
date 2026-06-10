# REVIEW.md — Pre-Commit Self-Review Checklist

Check the staged diff against this list before every non-doc-only commit (AGENTS.md rule 13).
Rule numbers refer to AGENTS.md "Non-Negotiable Rules"; this file indexes them, it does not replace them.

## Build & Validation

- [ ] Dev-tuning superset build passes: `ninja -C build_local` with `-DFLARE_DEV_TUNING=ON` configured (rule 1; Build section).
- [ ] Scripts touched → `python3 -m py_compile scripts/*.py` passes (rule 2).
- [ ] No mock/stub hardware paths; compiles against the real Pico SDK target (rule 5).

## Settings & Tunables

- [ ] `settings_t` field added/removed → `SETTINGS_VERSION` bumped in `settings_store.c` (rule 4).
- [ ] New/changed tunable wired end to end: `config.ini` + `config.ini.example` → `scripts/gen_config.py` → `tune.h` → `CONF_*` consumer (rule 7).
- [ ] Runtime/serial tunable has the full protocol surface: `SET:` handler, matching `GET:`, `scripts/flare_cmd.py --dump` entry where it belongs in live dumps, and docs (rule 8).

## Behavior & Scope

- [ ] New feature → regression impact reviewed across affected flows: preload, load, unload, toolchange, sync, RELOAD, persistence, protocol (rule 9).
- [ ] Spec'd behavior changed → matching OpenSpec change/spec delta exists (AGENTS.md flow triage).
- [ ] Hardware-dependent validation left unchecked and `HW:`-tagged (rule 12).

## Docs & Hygiene

- [ ] Renamed/added parameters and commands updated everywhere in docs — grep `MANUAL.md`, `BEHAVIOR.md`, `KLIPPER.md`, `TUNING.md` (rule 6).
- [ ] No local AI config staged: `.agents/`, `.claude/`, `.codex/`, `.gemini/`, `skills-lock.json` (rule 11; AI.md).
- [ ] Commit message follows project format with `Generated-By:` attribution; push follows immediately (rule 3; Commit Format).
