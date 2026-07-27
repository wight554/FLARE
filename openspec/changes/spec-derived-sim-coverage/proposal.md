## Why

host-sync-sim shipped `flare_sim` (59/62 tasks, not archived — RELOAD H4/H5/H6
scenarios). Follow-up work derived sim scenarios directly from existing
`openspec/specs/*` `#### Scenario:` blocks instead of ad-hoc invention —
sync-state-model, buffer-state-lock, cutter-feed-timeout, motion-safety done
inline this session, found 5 real spec/code mismatches + 1 dead-code path
(`memories/repo/host-sync-sim.md`). Doing this ad-hoc burns context re-deriving
setup each spec; tracking as OpenSpec tasks lets work resume cold.

## What Changes

- Continue spec-by-spec: for each in-scope spec, read `#### Scenario:` blocks,
  build `tests/host/sim_scenario.c` entries for runtime-observable ones (skip
  protocol.c-gated / doc-only / pure-code-structure scenarios — not sim-testable),
  add `scripts/test_sync_sim.py` assertions, verify against real firmware,
  record findings in `memories/repo/host-sync-sim.md` + `TEST_CASES.md`.
- ~11 specs remain: sync-refactor(60), psf-type-p-sensor(48),
  toolchange-orchestration(16), sync-feedback(10, mostly code-structure —
  low yield, skip unless a runtime angle appears), type-d-dynamic-flow(7),
  persistence-contract(7), flow-keyed-schedule(7), sync-refactor-foundation(5),
  reserve-safety-floor(5), buffer-geometry-vocabulary(10), relay-fallback-only(8).
- Fix any real bugs/architecture gaps found in `tests/host/` itself along the
  way (same pattern as `feed_gain` per-lane fix, motor-level PWM tracking).
- Flag (never hand-edit) spec/code mismatches found in LIVE `openspec/specs/*`
  files — those aren't change-scoped, editing them isn't this change's job.

## Capabilities

### New Capabilities
(none — extends host-sync-sim's `tests/host/` harness, not yet archived to
openspec/specs/, so no existing spec name to modify against)

### Modified Capabilities
(none — no spec-level requirement changes; this is test coverage, not behavior)

## Impact

- `tests/host/sim_scenario.c`, `sim_scenario.h`, `sim_main.c`, `sim_plant.c`,
  `sim_fakes.c`/`.h` (if architecture gaps found)
- `scripts/test_sync_sim.py`
- `memories/repo/host-sync-sim.md`, `TEST_CASES.md`
- Zero `firmware/src/**` / `firmware/include/**` changes (same constraint as
  host-sync-sim)
