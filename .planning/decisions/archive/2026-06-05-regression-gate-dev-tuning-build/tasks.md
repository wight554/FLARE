## 1. Gate builds the dev superset

- [x] 1.1 In `scripts/validate_regression.sh`, configure `build_local` with `-DFLARE_DEV_TUNING=ON` before the firmware build step (idempotent reconfigure)
- [x] 1.2 Keep the existing `ninja -C build_local` build; confirm it now compiles the `#ifdef FLARE_DEV_TUNING` handlers

## 2. Docs

- [x] 2.1 Note in `AGENTS.md` that pre-commit firmware verification builds the dev superset, and bulk lint/refactor tooling must cover dev-guarded code (`-DFLARE_DEV_TUNING=1`)

## 3. Verify

- [x] 3.1 Run `scripts/validate_regression.sh`; confirm firmware builds with dev tuning ON and the full gate passes

- 2026-06-06 validation: validate_regression.sh reconfigures build_local with -DFLARE_DEV_TUNING=ON then builds; full gate passed (dev build, ruff clean, 46 unittests, mock 26/26). AGENTS.md build section documents the superset + lint coverage.
