# regression-gate-dev-tuning-build (archived 2026-06-06)

- Blindspot: code behind `#ifdef FLARE_DEV_TUNING` (Tier-3 SET/GET in `protocol.c`) invisible to default `build_local` AND clang-tidy (inactive preprocessor branch) — broke Pi build which compiles `-DFLARE_DEV_TUNING=1`.
- Fix: `scripts/validate_regression.py` configures `build_local` with `-DFLARE_DEV_TUNING=ON`; bulk lint/refactor must add `--extra-arg=-DFLARE_DEV_TUNING=1`.
- Lesson: always build/lint the dev superset before commit; AGENTS.md Build section documents the reconfigure command.
