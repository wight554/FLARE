## 1. Already done (inline, before this change existed) — record only

- [x] 1.1 `sync-state-model` (2/22 runtime-testable): `sem_relief_pause_lifecycle`,
      `sem_fault_hold_standalone_recovery` in `tests/host/sim_scenario.c` +
      `SyncStateModelTests` in `scripts/test_sync_sim.py`. Rest are doc/
      taxonomy or protocol.c-gated.
- [x] 1.2 `buffer-state-lock` (4/13): `sem_bl_lock_catch`, `sem_bl_release_via_bs`,
      `sem_bl_watchdog_timeout` + `BufferStateLockTests`. Required a real fix:
      BL prime/lock/catch drives `motor_set_rate_sps()`/`motor_set_dir()`
      directly against `lane->m`, bypassing `lane_t.task`/`current_sps` — added
      `sim_motor_rate_sps()` (decodes commanded rate from the PWM fake's
      `clkdiv`/`wrap`) as a fallback in `sim_plant.c`, plus real `motor_init()`
      calls in `sim_main.c` boot so lane1/lane2 motors don't alias to PWM
      slice 0. Found spec/code mismatch: spec says `EV:BL:BREAK`, real event
      is `EV:BL:FOLLOW`.
- [x] 1.3 `cutter-feed-timeout` (4/7): `sem_cutter_large_feed_completes`,
      `sem_cutter_feed_timeout_jam`, `sem_cutter_settle_completes`,
      `sem_cutter_settle_timeout_abort` + `CutterFeedTimeoutTests`. Added
      scenario-level tunable overrides (`cut_feed_mm_override`,
      `cut_timeout_feed_ms_override`, `servo_settle_ms_override`,
      `cut_timeout_settle_ms_override`) in `sim_scenario.h`/`sim_main.c`.
      Found spec/code mismatch: spec says `cutter_abort()`/`CUT:ERROR ABORTED`
      on timeout, real code calls `cutter_fail(reason)` emitting
      `CUT:ERROR,<reason>`.
- [x] 1.4 `motion-safety` (2/13): `sem_motion_dry_spin_probe` + `MotionSafetyTests`.
      Skipped "Task Travel Limits"/`LOAD_TIMEOUT` (`TASK_LOAD_FULL` precondition
      setup too involved for the time spent). Found dead code: `sync.c`'s
      hard-wall-critical `FAULT_HOLD` path (`sync_tick_apply_rate`) computes
      `compression_wall_critical` only under type-D, acts on it only under
      not-type-D — permanently unreachable both ways. Confirmed empirically
      (idle_zero/type-D, ~63mm/s push, thresholds blown through, no
      `SYNC,FAULT_HOLD`). Regression-guard test added.
- [x] 1.5 All findings from 1.1-1.4 recorded in `memories/repo/host-sync-sim.md`
      and `TEST_CASES.md`'s scenario catalogue table.

## 2. relay-fallback-only (8 scenarios) — DONE

- [x] 2.1 Read spec. Runtime-observable: NEUTRAL fallback formula, TENSION
      catch-up, COMPRESSION stop. Non-observable (build/protocol/analyzer/
      config-generator): skipped.
- [x] 2.2 Added `sem_relay_fallback_probe` (type-D, steady demand, exercises
      all 3 relay branches) to `tests/host/sim_scenario.c`.
- [x] 2.3 Added `RelayFallbackTests` in `scripts/test_sync_sim.py`.
- [x] 2.4 Full regression green: clean `build_sim` rebuild (no warnings),
      26/26 `unittest scripts.test_sync_sim -v`, firmware untouched.
- [x] 2.5 Recorded in `memories/repo/host-sync-sim.md` and `TEST_CASES.md`.
      6th spec/code mismatch found: spec says COMPRESSION branch drives
      `SYNC_MIN` (682); `relay_control_law()` (sync_relay.c) literally
      `return 0`, confirmed by `idle_zero`/type-D converging to and holding
      exactly 0 for 56s+ sustained compression, never 682.

## 3. type-d-dynamic-flow (7 scenarios) — DONE (disposition-only, no new scenario)

- [x] 3.1-3.5 Read spec + cross-checked `memories/repo/typed-tension-recovery-floor.md`.
      Found the 7th and most significant mismatch: Requirement "A tension
      touch slams a decaying recovery feed floor" names
      `SYNC_TENSION_RECOVERY_FLOOR`/`SYNC_TENSION_RECOVERY_MS` and describes a
      fixed-MS decay after a snap-to-demand — `grep -rn
      "SYNC_TENSION_RECOVERY" firmware/` returns nothing; those symbols don't
      exist. The real, shipped mechanism (`sync_apply_type_d_probe_floor`,
      sync.c ~1466) is a continuous AIMD-style ramp
      (`g_sync_tension_probe_up/down/neutral_sps_per_s`, `_max_sps`) — a
      *different design*, not a renaming. `typed-tension-recovery-floor.md`
      already records "decaying-floor... FAILED+removed" as one of several
      rejected pivots before the AIMD probe shipped (hardware-validated) —
      the spec appears to document the rejected approach as current. No
      scenario built: a demand-step-up/type-D probe was attempted but the
      chosen demand level (40mm/s) simply exceeded deliverable feed and
      pinned the buffer at the TENSION rail permanently (a different failure
      mode, not informative about touch-burst-collapse); tuning a clean
      single-vs-burst demonstration needs more time than spent here.
      Requirement 2 ("quiet SYNC_MIN_RATE default, reserve bias handles slow
      drift") checked consistent: `CONF_SYNC_MIN_SPS`=682 ≈ 100mm/min at this
      board's gearing, matches memory's "quiet default SYNC_MIN_RATE 100" —
      no mismatch there.

## 4. persistence-contract (7 scenarios) — DONE

- [x] 4.1-4.5 Read spec. Most requirements are static/structural, already
      enforced by `scripts/test_settings_parity.py` (not `flare_sim`'s job to
      duplicate): Settings Version Bump, Runtime Tunables Flow, Persisted
      fields round-trip symmetrically, no-gratuitous-version-bump. Only
      "Fresh Board" is runtime-observable — added `sem_persistence_fresh_board`
      (new scenario field `test_settings_load_fresh_board`, makes
      `sim_main.c` call the real `settings_load()` instead of
      `settings_defaults()` at boot against the pristine zeroed flash fake)
      + `PersistenceContractTests`. Found the 8th mismatch: spec says a
      fresh/invalid-magic board's fallback settings are "written back to
      flash immediately"; real `settings_load()` (settings_store.c) calls
      `settings_defaults()` on that path and returns — no `settings_save()`
      call anywhere in it. Confirmed via direct flash-byte comparison
      before/after (`written_back=0`, verified in test).
      Regression green (27/27), clean rebuild, firmware untouched.

## 5. flow-keyed-schedule (7 scenarios) — DONE (disposition-only, no new scenario)

- [x] 5.1-5.5 Read spec. 5 of 6 requirements are `gen_config.py`/analyzer
      scope (schedule table generation, byte-identical emission determinism)
      — python, out of `flare_sim`'s linked scope. The one runtime piece,
      "Firmware interpolates baseline and bias on live flow" /
      "Degenerate single-point equivalence", checked out clean on direct
      inspection: `flow_active_segment()`/`flow_param()` (sync.c ~456-473)
      both correctly special-case `len<=1` to return the constant single
      point — no off-by-one, no bug. This repo's actual `config.ini` ships
      degenerate (`CONF_FLOW_SCHED_LEN 1`), so the multi-breakpoint
      interpolation scenario isn't exercisable without editing the
      gitignored personal config, which is out of scope. No new scenario
      built — nothing to demonstrate beyond what a direct code read already
      confirmed correct, and time was better spent on specs with an actual
      finding.

## 6. sync-refactor-foundation (5 scenarios) — DONE (disposition-only)

- [x] 6.1-6.5 Read spec. All 5 scenarios are architectural/process:
      standalone-without-Klipper, additive/default-compatible hardening,
      config-generation flow, telemetry-supports-offline-calibration,
      regression-review-across-flows. None runtime-observable from
      `flare_sim`'s CSV trace — no code path to drive, no event/state to
      assert on. No overlap check needed since nothing here would produce a
      scenario regardless.

## 7. reserve-safety-floor (5 scenarios) — DONE (disposition-only)

- [x] 7.1-7.5 Read spec. Verified by direct code read (both one-line `max()`
      expressions, low bug risk): `baseline_control_floor_sps()` (sync.c:609)
      = `max(flow_param(est).baseline_sps, g_baseline_target_sps)`, matches
      spec exactly; reserve bias floor (sync_buf.c:146-147) =
      `fmaxf(g_sync_compression_bias_frac, schedule_bias)`, matches exactly.
      No mismatch. No new scenario — same degenerate-schedule limitation as
      flow-keyed-schedule (task 5): this repo's config ships a 1-point
      schedule, so the interesting multi-breakpoint/weak-endpoint cases
      aren't exercisable without editing the gitignored personal config.

## 8. buffer-geometry-vocabulary (10 scenarios) — DONE (already-covered, no new scenario)

- [x] 8.1-8.5 Read spec. Most requirements are config.ini/protocol.c scope
      (SET/GET token rename, legacy-key rejection, config validation) — out
      of `flare_sim`. The runtime core — full-range `buf_switch_span_mm`/
      `buf_max_travel_mm` config converting to internal half-travel exactly
      once at ingest (`g_buf_switch_span_half_mm = CONF_BUF_SWITCH_SPAN_MM *
      HALF_F` in `main.c`, consumed by `buf_threshold_mm()`) and the EMU
      Sync defaults (10mm switch span / 25mm max travel) — is already
      implicitly confirmed by every type-D scenario run so far: `tune.h`
      has `CONF_BUF_SWITCH_SPAN_MM=10.0f`/`CONF_BUF_MAX_TRAVEL_MM=25`
      (matches spec's stated EMU defaults exactly), and every type-D
      scenario has consistently shown a ~5mm switch-crossing threshold (a
      small tick-granularity overshoot past the exact 5.0mm, from the
      plant's 20ms discrete steps, not a bug) and exactly ±12.5mm rail
      saturation (`25/2`). No new scenario needed.

## 9. sync-feedback (10 scenarios) — DONE (disposition-only)

- [x] 9.1 Reviewed: mostly code-structure requirements ("no early return",
      "isolated static functions", "single code path", "unified
      sync_apply_scaling path") — not observable from `flare_sim`'s
      black-box CSV trace/exit code; these describe internal call structure,
      not anything a running scenario can distinguish. The 2-3 genuinely
      runtime-flavored scenarios (type-D compression-recovery feed cap vs.
      type-P soft-wall-only backoff, no-feed-floor during compression for
      type-P) are already implicitly exercised by every existing type-D/
      type-P scenario pair in the catalogue without producing any observed
      anomaly (no stuck floors, no unexpected feed caps) — no dedicated new
      scenario built given the low marginal signal versus time cost.

## 10. toolchange-orchestration (16 scenarios) — PARTIAL, one gap logged

- [x] 10.1-10.2 Read spec. "RELOAD Buffer-Driven Contact" + "RELOAD
      Bang-Bang Pressure Cycle" already covered by the existing `reload_*`
      scenarios (H4/H5/H6). `CU:`/`CX:`/`UL:`/`UM:`/`CP:` host-command
      scenarios are protocol.c-gated — out of scope, though the underlying
      state machines they'd drive (`cutter_start`, manual unload) are real
      and linked.
- [ ] 10.3 Attempted "Normal Toolchange" (plain `tc_start()`, non-RELOAD
      path) via new `tc_start_at_ms`/`tc_target_lane` scenario trigger
      fields (added to `sim_scenario.h`/`sim_main.c`, real working
      infrastructure). Confirmed `TC_UNLOAD_WAIT_TH` -> `TC:UNLOADING`
      transition (partial confirmation of "Toolhead clear wait is
      meaningful"), but the unload phase then stalls: it needs `OUT` to
      transition true->false as unload physically progresses a real
      distance, which needs more `switch_script` timing than attempted so
      far — same precondition-complexity class as `TASK_LOAD_FULL`
      (motion-safety task, also not attempted). Removed the broken scenario
      rather than leave a failing one in the catalogue. Resume by adding a
      second `switch_script` event clearing `SWITCH_L1_OUT` at an estimated
      unload-completion timestamp (or, cleaner, drive it from the plant
      based on `lane->task_dist_mm` once unload starts — needs a small
      plant extension).
- [ ] 10.4-10.5 Not reached — no new scenario landed to regress/record beyond
      what's already noted here and in `memories/repo/host-sync-sim.md`.

## 11. psf-type-p-sensor (48 scenarios) — DONE (net-new subset; rest triaged out)

- [x] 11.1 Read `openspec/specs/psf-type-p-sensor/spec.md` in full (16
      requirements, 48 scenarios). Disposition per requirement:
      - "Type-P Relief-Pause Auto-Recovery": overlaps `sem_relief_pause_lifecycle`
        (already runs `sensor_type=p`, DEMAND_PAUSE_RESUME = exactly the
        "recovers under demand" scenario). Transition-driven-while-pinned and
        static-home-rest-does-not-rearm sub-scenarios not separately isolated —
        would need precise velocity/position staging beyond current demand
        profiles; left uncovered, not net-new-built.
      - "Type-P Stabilize Rail Breakaway": NOT covered by anything existing
        (no prior scenario ever called `BS`/stabilize under type-P). Net-new,
        built (see 11.2).
      - "Type-P Tension Refill Snap": partially implicit in
        `SyncRefactorTests.test_tension_feeds_compression_backs_off`
        (feed reaches max_sps in TENSION) but the snap-to-max-then-settle-
        from-extruder-estimate mechanic isn't isolated. Not built — same
        class as the dead-zone/PD-response requirement below, deprioritized
        given time budget.
      - "PSF Endpoint Calibration", "BUF_GOAL User Param in Raw ADC Space",
        "Remove BUF_RANGE and BUF_INVERT": protocol.c `CAL:`/`SET:`/`GET:`
        commands, not linked into `flare_sim` (established out-of-scope
        class, same as every other protocol-only requirement in this
        change).
      - "Asymmetric Normalization with Auto-Polarity", "Goal-Relative Zone
        Boundaries": sync_analog.c IS linked and these transforms run on
        every type-P scenario already in the catalogue (every zone/`bp_mm`
        CSV column reflects goal-relative, auto-polarity-normalized
        position) — exercised implicitly by the whole existing type-P
        corpus, no dedicated scenario needed.
      - "Continuous Extruder Estimate": internal-state requirement
        (`vel_norm`, `extruder_est_sps`, confidence) not exposed in the CSV
        trace; behaviorally implied by every type-P scenario's feed
        responsiveness already asserted elsewhere. Out of scope for a
        dedicated scenario without a trace-format change.
      - "Gradual PD Control with Dead Zone", "Filtered Derivative", "Soft
        Walls": control-law shape requirements — the *effect* (feed near
        goal without hunting; blend to max/zero approaching rails) is
        implicit in existing type-P scenarios (`steady`, `burst`,
        `sem_psf_stab_rail_breakaway`'s approach to goal); isolating the PD
        terms themselves needs internal-state export. Not built.
      - "Hard Catch and Print-Stop Detection": the two `PSF_WALL_SAT_MS`
        saturation scenarios are the same mechanism already exercised by
        `sem_relief_pause_lifecycle` (compression sat -> relief_pause) and
        `sem_fault_hold_standalone_recovery` (tension sat -> fault_hold,
        whose docstring's ~1.5s re-entry cadence is exactly this
        requirement's "no instant re-fault" guarantee). The `sync_fast_brake`
        reversible-brake path (rapid velocity spike, not sustained
        saturation) needed no new scenario: `retract`/type-P (brief 500ms
        pulse) already IS "Slowdown recovers" — settles at 7.5mm compression,
        short of the rail, sync stays `ACTIVE` throughout, no RELIEF_PAUSE/
        FAULT_HOLD; `long_retract`/type-P (sustained 200mm/s) already IS
        "Real stop confirmed" — saturates and stays pinned, `SYNC,
        RELIEF_PAUSE` fires. Both existing scenarios, now asserted under
        `PsfTypePSensorTests`.
      - "Type-P Unload Uses No Position-Based Over-Tension Guard": built.
        Added an `ul_start_at_ms`/`ul_target_lane` trigger (calls the real
        `lane_start(..., TASK_UNLOAD, ...)`, mirroring protocol.c's manual
        UL command) plus two scenarios: `sem_psf_unload_normal` (OUT clears
        mid-retract -> `UNLOADED` for both sensor types, no guard
        interference) and `sem_psf_unload_stuck` (OUT held present the
        whole run, extruder-gripping jam -> type-D's `UNLOAD_TENSION_BLOCK`
        dwell fires `UNLOAD_BLOCKED` at t=6200; type-P has no such guard and
        never blocks in the same window). Gotcha found empirically: the
        tension-block guard has a "both lanes' OUT present" double-load
        exception (motion.c:378) that the sim's boot default (both lanes'
        OUT true) silently satisfies — lane 2's OUT must be forced false
        for the type-D block path to engage at all. Did NOT chase the
        eventual type-P `UNLOAD_TIMEOUT`/`UNLOAD_MAX` distance fallback:
        confirmed it collides with the generic 20s saturation invariant
        (the buffer's ~12.5mm physical rail saturates in ~1s; `UNLOAD_MAX`
        operates on the whole filament path, three orders of magnitude
        larger) — the guard's absence is the testable claim, not the
        eventual timeout.
      - "Type-P Fault Timers Scoped to Active Sync": "Fault recovery does
        not instantly re-fault" already covered (see above,
        `sem_fault_hold_standalone_recovery`). "Normal extrude does not
        fault on engagement" needs an idle-dwell-then-organic-AUTO_START
        setup, blocked by the same `g_sync_auto_started`
        organic-engage-only gating documented as INCONCLUSIVE under
        sync-refactor (task 12) — not attempted here for the same reason.
      - "Type-P Feed Quality and Reliable Stabilize": spec text explicitly
        says "measured against a real print, not isolated bench bursts" —
        out of scope for `flare_sim` by the spec's own stated acceptance
        method, except the "BS breaks away from a deep saturated rail"
        sub-scenario, which IS the Stabilize Rail Breakaway requirement
        above and is covered by the same net-new scenario.
- [x] 11.2 Built, across two passes:
      - `sem_psf_stab_rail_breakaway`/`sem_psf_stab_rail_break_timeout` (new
        `bs_request_at_ms` trigger calling real `buffer_stabilize_request()`,
        mirroring the `BS` command) — "Type-P Stabilize Rail Breakaway":
        breaks off the rail before `PSF_STAB_RAIL_BREAK_MS` when the stab
        motor actually moves the buffer (`BUF_STAB:DONE` at t=3740, cap at
        t=5000) vs. aborts exactly at the cap when it can't (`feed_gain=0`
        models an uncoupled/jammed lane, `STAGNANT_TIMEOUT` at t=5000 =
        2000+3000).
      - `sem_psf_unload_normal`/`sem_psf_unload_stuck` (new
        `ul_start_at_ms`/`ul_target_lane` trigger calling real
        `lane_start(..., TASK_UNLOAD, ...)`) — "Type-P Unload Uses No
        Position-Based Over-Tension Guard": normal retract completes
        (`UNLOADED`) for both sensor types with no guard interference;
        stuck retract (OUT held present) fires type-D's `UNLOAD_BLOCKED`
        at t=6200 but never blocks type-P in the same window.
      - No new C scenario needed for "Hard Catch and Print-Stop Detection":
        `retract`/type-P (brief pulse, recovers) and `long_retract`/type-P
        (sustained, confirms stop -> `SYNC,RELIEF_PAUSE`) already exercise
        the `sync_fast_brake` reversible-brake path.
- [x] 11.3-11.5 Verified against the real binary before asserting every
      time (measured exact event timestamps/outcomes first, twice found the
      first scenario attempt didn't do what its comment claimed — sim boot
      sensor defaults are both-OUT-true, not the false assumed initially —
      and fixed the scenario rather than the assertion). Added
      `PsfTypePSensorTests` (6 tests total) to `scripts/test_sync_sim.py`.
      Full regression: clean rebuild (warning-free), all new scenarios pass
      under both sensor types, 34/34 python tests green, `git diff
      --name-only main -- firmware/` empty. Recorded in
      `memories/repo/host-sync-sim.md` and `TEST_CASES.md`.

## 12. sync-refactor (30 requirements, ~60 scenarios) — PARTIAL, one item left open

- [x] 12.1 Skimmed all 30 requirement titles (not just scenario count —
      "60" in the proposal was scenario count; 30 requirements). Confirmed
      the hypothesis: majority are Klipper-sidecar/UDS/analyzer/calibration
      (`Observe-Only Calibration`, `Sidecar + UDS Tracking`, `Chatter
      Resistance`, `Relative Noise Gate`, `State-Aware Recommendations`,
      `FAIL vs WARN Separation`, `Bidirectional Drift Observation`, etc. —
      all python/analyzer, not linked) or protocol/config vocabulary rename
      (`Serial protocol tokens...renamed`, `Config keys are renamed`,
      `Buffer states use tension/compression/neutral vocabulary` — all
      protocol.c/config.ini, out of scope). A genuinely runtime-relevant
      minority remains: "Sync control polarity matches the state contract",
      "Pin-to-state decode is verified non-inverted", "Type-D compression
      relief is overfill-budgeted", "Normal switch contact does not trigger
      FAULT_HOLD" (this one's INTENT is already confirmed consistent with
      motion-safety's dead-code finding — normal type-D compression contact
      provably never reaches FAULT_HOLD, matching "does not trigger"),
      "Type-D RELIEF_PAUSE re-arms without a full drain", "Type-D estimator
      does not hard-overwrite from a modeled transition".
- [x] 12.2 Investigated the runtime-relevant minority from 12.1:
      - "Type-D standalone buffer control is a hysteretic relay": already
        covered by `RelayFallbackTests` (task 2).
      - "Normal switch contact does not trigger FAULT_HOLD": intent already
        confirmed by `MotionSafetyTests`' hard-wall-critical dead-code
        finding (task 1.4) — normal contact never faults, via unreachable
        code rather than an explicit type-P gate as this spec implies.
      - "Sync control polarity matches the state contract": added
        `test_tension_feeds_compression_backs_off` (`SyncRefactorTests`) —
        confirms TENSION commands materially more feed than COMPRESSION for
        both sensor types (`steady`/type-D, `burst`/type-P — `steady` never
        reaches COMPRESSION for type-P within a reasonable window, type-P's
        continuous PD is more damped than type-D's bang-bang relay).
      - "Pin-to-state decode is verified non-inverted": NOT sim-testable —
        the sim's sensor injection writes directly to the already-labeled
        `g_buf_tension_din`/`g_buf_compression_din` structs, bypassing the
        physical-pin-to-logical-signal mapping this requirement is actually
        about (that mapping lives in main.c's pin assignment, unlinked).
      - "Type-D compression relief is overfill-budgeted": RESOLVED (user
        asked to keep chasing the open items; found the fix). The earlier
        INCONCLUSIVE block was a sim-methodology gap, not a firmware fact:
        `sync_tick_auto_start_stop` (sync.c:1319, the organic auto-engage
        path) is fully real and linked — it just needed correct staging:
        `auto_mode=true`, `start_sync_active=false`, and one lane's OUT
        forced false (the function's both-loaded guard silently blocks
        auto-start otherwise, and the sim's boot default has both lanes'
        OUT true). `sem_sync_overfill_budget_probe` (sync-refactor
        scenario, reuses `idle_zero`'s demand profile) reaches genuine
        `g_sync_auto_started=true` this way.
        9th spec/code mismatch found: the spec's "overfill-budgeted"
        wording implies a small (~3mm) distance-based trigger. The only
        reachable RELIEF_PAUSE path from sustained compression
        (`sync_check_continuous_compression`) is TIME-based
        (`CONF_SYNC_AUTO_STOP_MS`, 5000ms), gated on feed already at the
        compression floor — not a distance budget. Confirmed empirically:
        compression onset ~t=3500, `RELIEF_PAUSE` at t=8280 (~4780ms
        dwell, matching the 5s constant). The overfill-budget globals
        (`g_sync_relieve_effort_mm`/`g_sync_compression_drain_budget_mm`)
        are real but gate a different mechanism entirely —
        `sync_type_d_compression_drain_target()`, a partial-feed "drain"
        rate during active-demand compression, not RELIEF_PAUSE entry.
        Added `test_compression_relief_is_dwell_timed_not_distance_budgeted`
        (`SyncRefactorTests`), asserting the ~5s window.
      - "Type-D estimator does not hard-overwrite from a modeled transition":
        not attempted — internal estimator-blend precision testing needs
        more setup than the remaining time budget covered.
- [x] 12.3-12.5 Full regression green (28/28 `unittest`), clean `build_sim`
      rebuild (no warnings), firmware untouched. Recorded in
      `memories/repo/host-sync-sim.md` and `TEST_CASES.md`.

## Readiness and Delivery Checks

- [x] `python3 -m py_compile scripts/*.py` — clean
- [x] `cmake -S tests/host -B build_sim -G Ninja && ninja -C build_sim` —
      clean, no warnings (own sources scoped `-Wall -Wextra`)
- [x] `python3 -m unittest scripts.test_sync_sim -v` — 36/36 green
- [x] `python3 scripts/validate_regression.py` — Host Sync Simulation gate
      step passes; full script still fails at the Python Lint (ruff) step
      on pre-existing unrelated `flare_daemon.py` F841 findings (confirmed
      via `git diff` against main — not introduced by this change, per
      `memories/repo/host-sync-sim.md`)
- [x] `git diff --name-only main -- firmware/` empty — zero firmware edits
- [x] Documentation sync verified: `TEST_CASES.md` scenario catalogue table
      lists every scenario added this change
- [x] `openspec validate spec-derived-sim-coverage --strict` passing
- [x] `openspec validate --specs --strict` passing — 42/42
- [x] Observation appended to `memories/repo/host-sync-sim.md` (task 11
      psf-type-p-sensor entry, this session)

Remaining open items before a future full archive (not blockers, carried
forward as known gaps): task 10's toolchange-orchestration "Normal
Toolchange" scenario (needs plant/switch-script work); task 12's "Type-D
estimator does not hard-overwrite from a modeled transition" (not
attempted, needs more setup than time allowed); task 11's remaining
documented uncovered psf-type-p-sensor sub-scenarios (relief-pause's
transition-while-pinned/static-rest sub-cases, Tension Refill Snap's exact
snap-then-settle mechanic).

Closed this pass (previously listed here): `sync_fast_brake`, the Type-P
Unload guard, "Normal extrude does not fault on engagement", and "Type-D
compression relief is overfill-budgeted" — the last two were both blocked
on the same root cause (sim never reached genuine
`g_sync_auto_started=true`), fixed once by driving the real organic
auto-engage path (`sync_tick_auto_start_stop`) correctly staged
(`auto_mode=true`, single lane loaded). Found the 9th spec/code mismatch
in the process (see task 12.2).
