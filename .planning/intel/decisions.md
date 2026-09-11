# Synthesized Decisions

No ADRs were ingested by `/gsd-ingest-docs` (all inputs were SPECs), but the
project's decision record does exist: the 64 archived OpenSpec changes under
`.planning/decisions/archive/<date>-<change>/design.md` carry the D-/G-/K-numbered
decisions and hardware A/B verdicts that `.planning/specs/*/spec.md` cite without
restating (restored from git history in `eeb6cff` after `eb0a942` dropped them).

Lookup only — `grep -rn '<term>' .planning/decisions/`. Newer decisions live in
the owning phase's `*-PLAN.md` (e.g. D12.1 daemon bind host, D12.2 commit
attribution in `phases/12-*/12-01-PLAN.md`).
