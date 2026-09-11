# Decision Archive

Read-only history of the 64 archived OpenSpec changes (2026-05-17 → 2026-06-25),
restored from git history after `eb0a942` removed `openspec/` without migrating
them. Each `design.md` holds the D-/G-/K-numbered decisions, hardware A/B
evidence and verdicts (e.g. relay confident-path REMOVE, compression-overfeed-stop,
psf-stale-fault-timers) that `.planning/specs/*/spec.md` cite but do not restate.

Lookup only: `grep -rn '<term>' .planning/decisions/` — never read wholesale.
New decisions go in phase `*-SPEC.md` / `*-PLAN.md`; do not add here.
