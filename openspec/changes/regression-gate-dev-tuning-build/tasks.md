## 1. Gate builds the dev superset

- [ ] 1.1 In `scripts/validate_regression.sh`, configure `build_local` with `-DFLARE_DEV_TUNING=ON` before the firmware build step (idempotent reconfigure)
- [ ] 1.2 Keep the existing `ninja -C build_local` build; confirm it now compiles the `#ifdef FLARE_DEV_TUNING` handlers

## 2. Docs

- [ ] 2.1 Note in `AGENTS.md` that pre-commit firmware verification builds the dev superset, and bulk lint/refactor tooling must cover dev-guarded code (`-DFLARE_DEV_TUNING=1`)

## 3. Verify

- [ ] 3.1 Run `scripts/validate_regression.sh`; confirm firmware builds with dev tuning ON and the full gate passes
