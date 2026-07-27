## Context

`flare_sim` links real `sync.c`/`sync_buf.c`/`sync_relay.c`/`sync_analog.c`/
`motion.c`/`toolchange.c`/`cutter.c`/`settings_store.c` against host fakes —
no `protocol.c`, no Klipper, no python. 42 `openspec/specs/*`, ~499 `####
Scenario:` blocks total; ~15 specs / ~240 scenarios are structurally
runtime-reachable (rest need protocol.c wire format, Klipper macros, python
daemon/tuner — unlinked). Done inline before this change existed:
sync-state-model (2/22), buffer-state-lock (4/13), cutter-feed-timeout (4/7),
motion-safety (2/13, 1 skipped for setup complexity, 1 untestable
autopreload). 5 spec/code mismatches + 1 dead-code path found so far, all in
`memories/repo/host-sync-sim.md`.

## Goals / Non-Goals

**Goals:**
- Work remaining in-scope specs one at a time, same rigor as done inline:
  build scenario -> run against real firmware -> verify empirically (never
  assume) -> python assertion -> full regression -> memory/doc update.
- Fix real `tests/host/` bugs found along the way (architecture gaps, not
  just scenario additions) — same standard as `feed_gain` per-lane fix and
  the PWM motor-rate-decode fix for BL-driven motion.
- Flag spec/code mismatches found in live `openspec/specs/*`; never
  hand-edit them (out of this change's authority — those are merged specs,
  not change-scoped).

**Non-Goals:**
- Not attempting the ~27 structurally-unreachable specs (Klipper/python/
  protocol-wire/process-docs).
- Not chasing 100% scenario coverage per spec — code-structure-only
  scenarios ("no early return", "isolated static function") aren't
  black-box-observable via CSV trace; skip them, note why.
- Not fixing firmware bugs found (e.g. the hard-wall-critical dead code) —
  flag with a regression-guard test documenting current behavior, leave the
  firmware decision to whoever owns that area.

## Decisions

- **One spec at a time, in the order picked opportunistically** (small/
  bounded/untested-area specs first: cutter-feed-timeout before
  sync-refactor's 60 scenarios) rather than a fixed priority queue — matches
  what already happened inline and kept each pass reviewable.
- **Scenario triggers**: reuse the established pattern — scripted-time direct
  API calls (`manual_reload_at_ms`, `bl_arm_at_ms`, `cutter_start_at_ms`
  precedent) for host-command-equivalent behavior; scenario-level tunable
  overrides (`cut_feed_mm_override` precedent) when a spec scenario needs a
  config combination `main.c` defaults don't produce.
- **Measure, don't assume**: every scenario gets an empirical run against the
  real binary before a python assertion is written, including scenarios that
  end up disproving a spec's stated behavior (hard-wall-critical). This
  already caught 2 sim-harness bugs (`lane_id` uninit, `feed_gain` global-vs-
  per-lane) and 1 total motor-tracking blind spot (BL) that pure code-reading
  wouldn't have surfaced.
- **Live spec mismatches get flagged, not fixed** — `openspec/specs/*` is
  merged/current state; changing it needs its own change with its own
  authority, not a side effect of test-coverage work.

## Risks / Trade-offs

- [Diminishing returns on huge specs] sync-refactor (60) and psf-type-p-sensor
  (48) are large; many scenarios there may be historical/parity-focused
  (behavior-preservation during a past refactor) rather than fresh runtime
  contracts, and may substantially overlap what's already covered. -> Skim
  first, build only the scenarios with genuine new runtime surface, note
  skipped ones with a one-line reason (same as sync-feedback's disposition).
- [Context growth across many specs] -> This change's tasks.md is the
  resumption point; a fresh session picks up from the task list, not from
  conversation history.
- [False "regressions" from inconsistent CLI invocations] already hit once
  (both_switches_fault missing `--ticks`) -> always reproduce a suspected
  regression with the exact original invocation before reporting it as real.
