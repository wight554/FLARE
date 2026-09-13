---
phase: 13-type-p-sync-relief-fault-trip
plan: 02
subsystem: sync
tags: [type-p, buffer-sync, tension-fault-trip, tlv-persistence, flare_sim]

# Dependency graph
requires:
  - phase: 13-01
    provides: TAG_SYNC_PSF_RELIEF_MULT (65) and the settings_apply_clamps() post-load
      re-clamp pattern this plan's TAG_SYNC_TENSION_STOP_MM (66) extends; the
      53-test scripts.test_sync_sim baseline this plan's Task 3 grows to 61
provides:
  - SYNC_TENSION_STOP_MM distance-based type-P tension fault trip (TAG 66),
    evaluated before the pre-existing ms dwell trip so distance is
    structurally primary
  - g_sync_trip_armed shared arm/disarm gate for both the mm and ms traps,
    set on any observed buffer-state transition while sync_enabled, cleared
    at the same three re-entry points g_sync_tension_extreme resets at
  - REVIEW-04 hold-suppression: sync_type_p_hold_in_progress() /
    sync_trip_track_hold_edge(), sampled unconditionally as the first
    statement of sync_tick(), above all four of that function's early returns
  - TM:/ARM: telemetry on the ST: line with a machine-proven line-budget test
    (scripts/test_status_line_budget.py)
  - 12 new flare_sim scenarios (6 base + 6 rail-scale-0.7 twins) proving the
    trip fires, arms correctly, excludes type-D, disables at 0, orders mm
    before ms, and suppresses/resets across deliberate holds
  - REQ-type-p-sync-relief-distance-trip registered complete
affects: [13-03-feed-probe, 13-04-hw-validation]

# Actuals (#2632)
actuals:
  tokens: 18159
  tasks: 3
  commits: 3
plan_head_before: 04f2714

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Shared arm/disarm flag (g_sync_trip_armed) gating two independent fault
      trips (mm distance + ms dwell) off one flag set in the single
      transition-observation point (sync_on_transition) and cleared at the
      same re-entry points an existing tracked-extreme variable resets at"
    - "Hold-suppression via an edge-sampled predicate called unconditionally
      as the first statement of a tick function, ahead of every early
      return, so a falling edge can never be swallowed by a gated
      sub-routine (sync_trip_track_hold_edge)"
    - "Provably-bounded telemetry fields (%c for a value that can only ever
      be '0'/'1') used to make a machine-checked worst-case line-budget
      assertion pass without touching CMD_LINE_MAX"
    - "Scenario-level knob override (tension_stop_mm_disabled) mirroring the
      existing tension_ramp_delay_ms_override idiom, for exercising/
      isolating a persisted knob the sim harness cannot SET: at runtime"

key-files:
  created:
    - scripts/test_status_line_budget.py
  modified:
    - firmware/src/sync.c
    - firmware/include/sync.h
    - firmware/include/settings_store.h
    - firmware/src/settings_store.c
    - firmware/include/controller_shared.h
    - firmware/src/main.c
    - firmware/src/protocol.c
    - firmware/src/protocol_status.c
    - scripts/gen_config.py
    - config.ini.example
    - scripts/flare_cmd.py
    - scripts/test_sync_sim.py
    - tests/host/sim_scenario.c
    - tests/host/sim_scenario.h
    - tests/host/sim_main.c
    - MANUAL.md

key-decisions:
  - "Arm gate uses sync_enabled (SYNC_ACTIVE), not g_sync_auto_started as the plan's action text literally specified -- see Deviations"
  - "sem_psf_ms_fallback isolates the ms path via a knob-disable override rather than an organic demand-rate split -- see Deviations"
  - "ARM: rendered %c not %d, and TM: rendered %d not %.1f, to fit the machine-proven ST: line budget (1 char headroom) without touching CMD_LINE_MAX"

patterns-established:
  - "Distance-primary / time-fallback fault trip pair sharing one accumulator and one arm flag"
  - "Unconditional hold-edge sampler positioned above all early returns in a tick function"

requirements-completed: [REQ-type-p-sync-relief-distance-trip]
# REQ-type-p-sync-relief-rail-relative-triggers and REQ-type-p-sync-relief-sim-coverage
# are also declared by 13-01 (done) and 13-03 (not yet done) -- per the shared-ID gate
# (#2388) they stay Pending until 13-03 finishes too. Verified via
# `gsd_run query requirements.ready-ids`: 1/3 ready, both blocked ones named above.

coverage:
  - id: D1
    description: "SYNC_TENSION_STOP_MM knob: full 7-touchpoint persisted-knob chain (config.ini -> gen_config.py -> tune.h -> g_sync_tension_stop_mm -> TLV emit/load -> settings_apply_clamps() re-clamp -> release-build SET:/GET: -> --dump -> MANUAL.md)"
    requirement: "REQ-type-p-sync-relief-distance-trip"
    verification:
      - kind: unit
        ref: "scripts.test_settings_parity.TestSettingsParity (both tests)"
        status: pass
      - kind: other
        ref: "python3 scripts/validate_regression.py (exit 0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Distance trip fires before the ms dwell fallback, both gated on a shared arm flag that requires a real buffer-state transition since the last re-entry/hold-release, type-D excluded, disables at 0"
    requirement: "REQ-type-p-sync-relief-distance-trip"
    verification:
      - kind: integration
        ref: "scripts.test_sync_sim.TensionStopMmTripTests (4 methods)"
        status: pass
      - kind: integration
        ref: "scripts.test_sync_sim.TensionStopMmOrderingTests (3 methods)"
        status: pass
    human_judgment: false
  - id: D3
    description: "REVIEW-04 hold-falling-edge reset sampled unconditionally above every early return in sync_tick(), proven both by acceptance-criteria line-ordering checks and by the sem_psf_trip_hold_release sim scenario"
    verification:
      - kind: integration
        ref: "scripts.test_sync_sim.TensionStopMmOrderingTests#test_hold_release_resets_accumulator_and_arm"
        status: pass
      - kind: other
        ref: "sed-based source-order acceptance criteria (sampler before g_bypass; absent from sync_check_tension_dwell_and_ramp)"
        status: pass
    human_judgment: false
  - id: D4
    description: "TM:/ARM: telemetry on the ST: line, machine-proven to fit STATUS_LINE_MAX with 1 char headroom reported"
    verification:
      - kind: unit
        ref: "scripts.test_status_line_budget.TestStatusLineBudget#test_status_line_fits_budget_with_headroom"
        status: pass
    human_judgment: false
  - id: D5
    description: "Sim coverage widened from 53 (13-01 baseline) to 61 tests: mm/ms ordering, arming, type-D exclusion, disable path, hold suppression/release, and rail-scale (0.7) twins, plus the 13-01 bound invariant extended to every new saturating scenario"
    requirement: "REQ-type-p-sync-relief-sim-coverage"
    verification:
      - kind: other
        ref: "python3 -m unittest scripts.test_sync_sim (61/61 pass)"
        status: pass
    human_judgment: true
    rationale: "REQ-type-p-sync-relief-sim-coverage is also declared by 13-03 (not yet run); per the shared-ID gate it cannot be marked complete from this plan alone even though this plan's own share of the work is done and green."

# Metrics
duration: ~150min active (session interrupted by a rate limit mid-Task-3; wall-clock span ~5h)
completed: 2026-09-13
status: complete
---

# Phase 13 Plan 02: SYNC_TENSION_STOP_MM Distance Trip Summary

**Type-P tension fault trip: a shared-arm-flag mm/ms trip pair (mm primary, ms fallback) reusing the existing refill-effort accumulator, suppressed during deliberate rail holds via an edge sampler that runs unconditionally ahead of every early return in `sync_tick()`, with proven-budget `TM:`/`ARM:` telemetry and 61-test sim coverage.**

## Performance

- **Duration:** ~150 min active work (session interrupted by a rate limit mid-Task-3; wall-clock span ~5h)
- **Started:** 2026-09-13T07:28Z (approx, following 13-01 completion)
- **Completed:** 2026-09-13T12:39Z
- **Tasks:** 3 (all completed)
- **Files modified:** 17 (1 created)

## Accomplishments

- `SYNC_TENSION_STOP_MM` ships as a new persisted, live-tunable knob (TAG 66, default 32mm, 0 disables) with the full 7-touchpoint chain: `config.ini`/`config.ini.example` → `gen_config.py` → `tune.h` → `g_sync_tension_stop_mm` → TLV emit/load → `settings_apply_clamps()` re-clamp on every load (REVIEW-05) → release-build `SET:`/`GET:` → `flare_cmd.py --dump` → `MANUAL.md`.
- `g_sync_trip_armed` gates both the new mm distance trip and the pre-existing ms dwell trip so they arm/disarm in lockstep off one shared flag: set in `sync_on_transition()` on any observed buffer-state edge while `sync_enabled`, cleared in `sync_disable()`/`sync_rearm_active()`/the AUTO_START branch of `sync_tick_auto_start_stop()` (the same three re-entry points `g_sync_tension_extreme` resets at).
- `sync_type_p_hold_in_progress()` / `sync_trip_track_hold_edge()` (REVIEW-04): BL lock/prime/follow, tail assist, buffer-stabilize, and RELOAD follow all suppress the trip; the hold's falling edge is sampled unconditionally as the very first statement of `sync_tick()`, above all four of that function's early returns, so a hold releasing mid-bypass/toolchange/gate cannot swallow the accumulator/arm reset.
- The mm trip is evaluated before the ms fallback so ordering is structural (D-08/D-09); D-14 type-D exclusion, D-21 IN-sensor stand-down, and the REVIEW-06 `cannot_refill` dormancy interaction are all recorded as source comments at the trip site.
- `TM:`/`ARM:` appended to the extended `ST:` tail next to `TT:`, exposed via a new `sync_tension_stop_trip_mm()` accessor (0 while unarmed/held, otherwise the accumulated tension travel). The tail `snprintf`'s return value is now captured and latches one `SYS,ST_TRUNC` event on overflow — previously silently discarded.
- `scripts/test_status_line_budget.py`: a new stdlib-only `unittest.TestCase` that regex-reads both format-string literals out of `cmd_handle_status_dump()`, computes a documented worst-case width per conversion specifier, and asserts the line fits `STATUS_LINE_MAX` — currently 759/760 chars, 1 char headroom, reported so the next field addition knows the budget.
- 12 new `flare_sim` scenarios (`sem_psf_mm_trip`, `sem_psf_trip_unarmed`, `sem_psf_mm_trip_disabled`, `sem_psf_mm_before_ms`, `sem_psf_ms_fallback`, `sem_psf_trip_held_suppressed`, `sem_psf_trip_hold_release`, plus 6 rail-scale-0.7 twins) and two new `scripts/test_sync_sim.py` test classes (`TensionStopMmTripTests`, `TensionStopMmOrderingTests`) grow the suite from 53 (13-01 baseline) to 61 tests, all green. The 13-01 bound invariant (no path commands raw max while physically saturated) is extended to every new scenario that saturates.

## Task Commits

Each task was committed atomically:

1. **Task 1: SYNC_TENSION_STOP_MM knob, arm-after-transition gate, and the distance trip** - `0c31c36` (feat)
2. **Task 2: TM:/ARM: telemetry with a proven ST: line budget** - `06449bb` (feat)
3. **Task 3: Sim coverage — trip fires, ordering, arming, suppression, shallow-rail twins** - `ee5bf7b` (test)

**Plan metadata:** committed as part of this SUMMARY (see below).

## Files Created/Modified

- `firmware/src/sync.c` — `g_sync_trip_armed`, `sync_type_p_hold_in_progress()`, `sync_trip_track_hold_edge()`, `sync_tension_trip_fire()` helper, mm-trip branch (before the ms trip) in `sync_check_tension_dwell_and_ramp`, arm-set in `sync_on_transition`, arm-clear at the 3 re-entry points, `sync_tension_stop_trip_mm()` accessor
- `firmware/include/sync.h` — `g_sync_trip_armed` extern, `sync_tension_stop_trip_mm()` prototype
- `firmware/include/settings_store.h` — `TAG_SYNC_TENSION_STOP_MM = 66`
- `firmware/src/settings_store.c` — `SYNC_TENSION_STOP_MIN_MM`/`_MAX_MM` bounds constants, defaults seed, TLV emit/load, `settings_apply_clamps()` re-clamp
- `firmware/include/controller_shared.h`, `firmware/src/main.c` — `g_sync_tension_stop_mm` global
- `firmware/src/protocol.c` — release-build `SET:`/`GET:SYNC_TENSION_STOP_MM`, mirrored bounds `#define`s
- `firmware/src/protocol_status.c` — `TM:%d,ARM:%c` on the extended tail, truncation-return capture + `ST_TRUNC` latch
- `scripts/gen_config.py`, `config.ini.example` — `sync_tension_stop_mm` default (32.0) + `CONF_SYNC_TENSION_STOP_MM` emission + documented example row
- `scripts/flare_cmd.py` — `SYNC_TENSION_STOP_MM` added to `DUMP_PARAMS`
- `scripts/test_status_line_budget.py` — new file, worst-case ST: line budget proof
- `scripts/test_sync_sim.py` — `TensionStopMmTripTests` (4 methods), `TensionStopMmOrderingTests` (3 methods), `BOUND_INVARIANT_SCENARIOS` widened
- `tests/host/sim_scenario.c` — 12 new scenarios (see Accomplishments)
- `tests/host/sim_scenario.h`, `tests/host/sim_main.c` — `tension_stop_mm_disabled` scenario field
- `MANUAL.md` — `SYNC_TENSION_STOP_MM` parameter row, `TM`/`ARM` field rows, `TENSION_STOP:MM`/`:MS` event names, `cannot_refill` reachability clause

## Decisions Made

- **Arm gate uses `sync_enabled`, not `g_sync_auto_started`** — the plan's action text literally specified gating the arm-set in `sync_on_transition()` on `g_sync_auto_started`. Implemented literally first, then empirically discovered this breaks 13-01's own `sem_psf_relief_bound` scenario: `g_sync_auto_started` stays `false` for any session engaged via a direct force (a host `SET:`, a manual toolhead-insert engage with `auto_mode` off, or the sim's `start_sync_active` shortcut — none of these call `sync_rearm_active()` or `sync_tick_auto_start_stop()`, the only two places that set it true) even though sync is genuinely driving the buffer and observing real transitions identically. Gating on it suppressed the pre-existing ms dwell trip in nearly every scenario in the suite, not just the false-positive case D-10 targets, silently changing 13-01's own scenario's settled equilibrium (a FAULT_HOLD/recovery cycle that used to fire stopped firing, letting the control loop find a different, un-tested steady state). Per the plan's own stated principle ("if that gate changes the pass/fail outcome of any existing scenario, that is a defect in the gate, not an expectation to update"), fixed by gating on `sync_enabled` instead — the correct predicate for "sync is actively driving and has observed a transition since it started", which `g_sync_auto_started` only approximates for the organic-engage subset of that condition.
- **`sem_psf_ms_fallback` isolates the ms path via `tension_stop_mm_disabled`** rather than an organic demand-rate split. Extensive empirical investigation (documented in Deviations below) found `sync_type_p_relief_bound_sps` floors every relief-zone target at this project's `baseline_sps` (10912 sps ≈ 26.7mm/s, a single-point flow schedule), and relief-zone entry (`sync_type_p_in_relief_zone`) is true for nearly the whole duration of any continuing/worsening tension excursion — so the mm accumulator crosses 32mm in ~1–1.5s regardless of scripted demand or `feed_gain` once a multi-second dwell is sustained at all, confirmed even against 13-01's own unmodified `sem_psf_relief_bound`. A demand profile gentle enough to avoid the relief floor recovers to NEUTRAL/COMPRESSION within ~1s and never sustains a 6s dwell at all. See Deviations for the full investigation trail.
- **`ARM:` rendered `%c` not `%d`, `TM:` rendered `%d` not `%.1f`** — the worst-case line budget (per the plan's own stated per-specifier widths: 11 for `%d`/`%u`, 12 for `%.1f`/`%.2f`) came in 10 chars over `STATUS_LINE_MAX` with the originally-planned formats. `ARM:` can only ever render `'0'` or `'1'` by construction (a ternary literal, not an arbitrary int), so `%c` lets the budget test charge it a structurally-provable 1 char instead of the blanket 11. `TM:` switched to `%d` (int truncation) purely for consistency with its sibling `SYNC_REFILL_MM:%d` over the same underlying float, saving one more char. Net result: 759/760, 1 char headroom — thin but proven, and the test reports it so the next field addition sees the real budget before it silently truncates.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Arm gate predicate changed from `g_sync_auto_started` to `sync_enabled`**
- **Found during:** Task 1, empirical regression testing against the full `scripts.test_sync_sim` suite
- **Issue:** Implementing the plan's literal instruction ("Set it true in `sync_on_transition()` for any buffer-state edge observed while `g_sync_auto_started` is set") caused `sem_psf_relief_bound` (a 13-01 scenario, unmodified) to stop reaching `FAULT_HOLD` at all, changing its settled trajectory and failing `test_bound_never_reaches_max_while_saturated`/`test_extreme_relaxes_within_a_sync_window`. Root cause: `g_sync_auto_started` is only set true by `sync_rearm_active()` and the AUTO_START branch of `sync_tick_auto_start_stop()` — a directly-forced `SYNC_ACTIVE` session (including the sim's own `start_sync_active` shortcut, used by the large majority of existing type-P scenarios) never sets it, so gating arming on it suppressed the pre-existing ms trip almost universally, not just for genuine false positives.
- **Fix:** Gate on `sync_enabled` (`g_sync_state == SYNC_ACTIVE`) instead — arms on any observed transition while sync is actually driving the buffer, regardless of how that active session began.
- **Files modified:** `firmware/src/sync.c`
- **Verification:** Full `scripts.test_sync_sim` suite returned to 53/53 (then grew to 61/61 with Task 3's additions) after the fix; `sem_psf_no_fault_on_idle_engagement` (the plan's explicitly-named regression guard) confirmed still emits zero `FAULT_HOLD` under both sensor types.
- **Committed in:** `0c31c36` (Task 1 commit)

**2. [Rule 3 - Blocking / test infrastructure] `tension_stop_mm_disabled` scenario field added**
- **Found during:** Task 1 (needed to prove the `SET:SYNC_TENSION_STOP_MM:0` disable path), Task 3 (needed to isolate the ms fallback path)
- **Issue:** `flare_sim` cannot process `SET:` commands at all (documented limitation, same as the pre-existing `tension_ramp_delay_ms_override` field's own comment), so there was no way to exercise the knob's disable value or isolate one trip from the other within the sim harness.
- **Fix:** Added `bool tension_stop_mm_disabled` to `sim_scenario_t`, applied in `sim_main.c` by forcing `g_sync_tension_stop_mm = 0.0f` post-`settings_defaults()` — mirrors the existing override idiom exactly.
- **Files modified:** `tests/host/sim_scenario.h`, `tests/host/sim_main.c`
- **Verification:** `sem_psf_mm_trip_disabled` and `sem_psf_ms_fallback`(`_shallow`) all pass with this override.
- **Committed in:** `0c31c36` (Task 1 commit)

**3. [Rule 1 - Bug] `sem_psf_relief_extreme_stale` (13-01 scenario) needed `tension_stop_mm_disabled` to keep passing**
- **Found during:** Task 1, full-suite regression run after the mm trip landed
- **Issue:** This REVIEW-07 scenario's sustained tension-pin demand shape (by design, to stress extreme-tracking) legitimately crosses the new 32mm default as an unrelated side effect, firing `FAULT_HOLD`/`AUTO_START` and confounding its own `assertNotIn("SYNC,AUTO_START", ...)` assertion (which specifically checks that nothing else resets the tracked extreme).
- **Fix:** Set `.tension_stop_mm_disabled = true` on that one scenario — it isolates extreme-relaxation, not the distance trip; Task 3 adds purpose-built scenarios for the trip itself.
- **Files modified:** `tests/host/sim_scenario.c`
- **Verification:** `test_extreme_relaxes_within_a_sync_window` passes again.
- **Committed in:** `0c31c36` (Task 1 commit)

### Investigation Trail (not a deviation from the plan's own instructions, but extensive empirical work worth recording)

**`sem_psf_ms_fallback` demand-profile search.** The plan's action text says "check the effective `g_sync_tension_dwell_stop_ms` default before picking the flow rate" and asks for "a demand profile slow enough that the ms threshold elapses first." Tried, in order: demand steps from 4mm/s to 15mm/s combined with `feed_gain` reductions from 0.3 to 0.95; a permanent full jam (`feed_gain=0` forever) at both 3mm/s and default demand; both `buf_max_travel_override=16` and the default 25mm; a tiny scripted retract (`DEMAND_RETRACT`) sized to nudge the buffer through exactly one zone transition without overshooting into a stuck resting state. Every combination that sustained a multi-second `BUF_TENSION` dwell also crossed 32mm within roughly 1–1.5s once the relief branch engaged (confirmed the same is true of 13-01's own unmodified `sem_psf_relief_bound`); every combination gentle enough to avoid the relief zone recovered to NEUTRAL/COMPRESSION within about 1s and never sustained a dwell at all. Root cause (read from `sync_type_p_relief_bound_sps` and `sync_type_p_in_relief_zone`): this project's default config synthesizes a single-point flow schedule, so the relief bound's `baseline_sps` floor (10912 sps ≈ 26.7mm/s) is constant regardless of demand, and relief-zone entry is true for nearly the entire duration of any continuing-or-worsening excursion (a non-strict `<=` comparison against a continuously-updating tracked extreme). Resolved via `tension_stop_mm_disabled` (Rule 3 infrastructure, above) rather than by weakening the ms trip's own gating — the ms path itself is proven correct (fires `FAULT_HOLD`, never emits `TENSION_STOP:MM`) with mm isolated; it is genuinely not reachable ahead of mm at any organically-constructible print-flow demand under this project's current tuning defaults.

---

**Total deviations:** 3 auto-fixed (1 Rule 1 bug in the arm gate, 1 Rule 3 test infrastructure gap, 1 Rule 1 bug in a pre-existing scenario's new interaction with the trip). **Impact on plan:** The arm-gate fix was necessary for correctness — the plan's literal instruction would have silently defanged the pre-existing ms trip across nearly the whole scenario suite. The infrastructure addition and the `sem_psf_ms_fallback` isolation choice reflect a genuine empirical property of this project's current tuning (documented above and in source comments), not scope creep.

## Issues Encountered

None beyond the documented deviations above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 13 Success Criterion 2 is met: `SYNC_TENSION_STOP_MM` exists across `config.ini`, `SET:`/`GET:`, `--dump`, and `MANUAL.md`; accumulates relief motion while pegged; trips `FAULT_HOLD` alongside the existing ms dwell trip; both arm only after the first observed buffer-state transition in the active-sync window.
- Distance is proven primary and time is proven a working (if organically unreachable-ahead-of-distance, at this tuning) fallback — two scenarios trip on different thresholds, never both in the same episode.
- No compression-side distance trip and no second accumulator were introduced (D-13, "Don't Hand-Roll" — confirmed via acceptance-criteria greps).
- Both threshold-interaction gotchas (`cannot_refill` dormancy, the 0.99 saturation guard) are recorded in source and `MANUAL.md`.
- `scripts.test_sync_sim` baseline for 13-03's own no-regression comparison: **61 tests, all passing** (`python3 scripts/validate_regression.py`, exit 0).
- `SYNC_TENSION_STOP_MM` consumed `TAG_66` — 13-03's own new tag(s) must re-grep the enum before appending (current max is 66).
- `REQ-type-p-sync-relief-rail-relative-triggers` and `REQ-type-p-sync-relief-sim-coverage` remain Pending (shared with 13-01/13-03) until 13-03 finishes — this plan's own share of both is done and green.
- No blockers for 13-03 (feed probe, wave 2 continued).

---
*Phase: 13-type-p-sync-relief-fault-trip*
*Completed: 2026-09-13*

## Self-Check: PASSED
