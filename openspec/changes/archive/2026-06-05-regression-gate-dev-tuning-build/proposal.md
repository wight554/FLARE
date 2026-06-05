## Why

`protocol.c` exposes Tier-3 tuning knobs behind `#ifdef FLARE_DEV_TUNING`. The CMake
option defaults OFF, so `build_local` (and `validate_regression.sh`) never compile that
code, while the Pi builds with `-DFLARE_DEV_TUNING=1`. The global g_-rename ran
`clang-tidy` against the default (OFF) config, so it never saw the dev-block identifiers
(clang-tidy only parses the active preprocessor branch) — and the local build never
compiled them. Result: a "clean" local build that broke the Pi with undeclared
`RAMP_TICK_MS`/`BUF_HYST_MS`/… (fixed in 9eb4439). The gate must compile the dev
superset so this class of bug fails locally.

## What Changes

- `scripts/validate_regression.sh` SHALL configure + build firmware with
  `FLARE_DEV_TUNING=ON` (the superset), so the `#ifdef FLARE_DEV_TUNING` SET/GET handlers
  are compiled by the gate.
- Document in `AGENTS.md` that pre-commit firmware verification builds the dev superset,
  and that bulk lint/refactor tooling must cover dev-guarded code (run with
  `-DFLARE_DEV_TUNING=1` / against a dev-ON compile DB).

No firmware behavior change. Production builds may still set `FLARE_DEV_TUNING=OFF`; the
gate simply compiles the larger configuration to catch dev-only breakage.

## Capabilities

### Modified Capabilities
- `static-regression-validation`: the gate's firmware build step adds a dev-tuning-ON
  compile requirement.

## Impact

- Touched: `scripts/validate_regression.sh`, `AGENTS.md`.
- Risk: minimal — building a superset only compiles more code; the gate gets slightly
  slower but catches dev-guarded regressions.
