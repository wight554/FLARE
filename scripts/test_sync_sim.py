#!/usr/bin/env python3
"""Host sync simulation harness — declares scenarios against `flare_sim`,
runs one subprocess per scenario, and asserts on the emitted CSV trace.

Scope note: the scenario catalogue covers the demand-profile and
fault-injection surfaces (design.md "Demand profiles" / "Fault Injection
API") plus the generic invariant suite (every scenario, for free — see
sim_trace.c). `idle_zero` covers the frozen-distance-clock-on-abrupt-
extruder-stop case. See memories/repo/host-sync-sim.md for what's not yet
modeled (type-P RELOAD sign regression, stale fault timers while sync OFF).

Three scenarios (`reload_genuine_runout_escalation`, `reload_idle_consumer_
staged_completion`, `reload_already_loaded_noop`) exercise the real toolchange
RELOAD state machine end-to-end — added to give openspec/changes/
audit-reliability-fixes' H4/H5/H6 fixes sim-level evidence (its remaining
open tasks, 10.3/11.3/12.4, are HW:-gated; a sim pass here screens the logic,
it does not and must not check off a HW: task — design.md "Simulation
Authority Boundary").

See design.md "Assertion Model" and "Runtime Budget"; the isolation and
tolerance rules below are specified, not incidental, in
openspec/changes/host-sync-sim/specs/host-sync-simulation/spec.md.
"""
import csv
import io
import os
import re
import subprocess
import sys
import tempfile
import unittest

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
SIM_BINARY = os.path.join(REPO_ROOT, "build_sim", "flare_sim")

# Catalogue mirrors tests/host/sim_scenario.c g_sim_scenarios — kept in sync by
# hand; a name typo here fails fast as a skip-with-message, not a silent gap.
BASELINE_SCENARIOS = ["steady", "step_up", "burst", "idle_zero", "retract", "long_retract"]
FAULT_SCENARIOS = ["jam_upstream", "grind_slip", "underextrusion", "retract_stuck"]
SWITCH_SCENARIOS = ["runout", "y_splitter_toggle"]
TYPE_D_ONLY_SCENARIOS = ["sensor_chatter", "sensor_stuck", "both_switches_fault"]
# Type-P only: the H6 escalation path (sync.c sync_check_tension_dwell_and_ramp)
# is gated `g_buf_sensor_type != BUF_SENSOR_TYPE_D`.
RELOAD_SCENARIOS = [
    "reload_genuine_runout_escalation",
    "reload_idle_consumer_staged_completion",
    "reload_already_loaded_noop",
    "reload_runout_lane2_to_lane1",
    "reload_target_empty_abort",
    "reload_manual_resume_empty_active",
    "reload_mmu_mode_no_escalation",
]

ALL_DUAL_TYPE_SCENARIOS = BASELINE_SCENARIOS + FAULT_SCENARIOS + SWITCH_SCENARIOS

STRESS_LAG_SWEEP_MS = [0, 20, 50, 100, 200]

# Phase 13 (type-P bounded relief, D-01/D-02/D-22/D-25): rail-scale twins at
# 1.0/0.7/0.5 proving the relief bound and RELIEF_ON trigger are rail-relative,
# not the absolute-literal 51bdca8 failure class. Type-P only.
TYPE_P_RELIEF_SCENARIOS = [
    "sem_psf_relief_bound",
    "sem_psf_relief_bound_shallow",
    "sem_psf_relief_shallow_rail",
]

# The deliberate demand step in each TYPE_P_RELIEF_SCENARIOS entry (kept in
# sync by hand with tests/host/sim_scenario.c, same convention as the
# BASELINE_SCENARIOS catalogue above). Boot itself always produces a type-P
# TENSION reading (goal-relative: physical position 0 reads deep tension
# against a compression-biased BUF_GOAL) that clears well before this step,
# so scoping to "at or after" this timestamp selects the deliberate episode,
# not the universal boot transient every type-P scenario shows.
TYPE_P_RELIEF_STEP_MS = 3000

# Phase 13 Task 2 (D-04): proves the tension-dwell ramp's target-raise is
# capped at the same relief bound as the apply-side branch, not just at
# max_sps. Demand shape differs from TYPE_P_RELIEF_SCENARIOS (gain-modulated
# spike + recovery vs a plain step), so these get their own list rather than
# joining TYPE_P_RELIEF_SCENARIOS -- they don't share TYPE_P_RELIEF_STEP_MS's
# assumption of a step at t=3000ms and would break the responsiveness test's
# "first RELIEF_ON at/after the step" scoping if merged in.
RAMP_CAPPED_SCENARIOS = [
    "sem_psf_relief_ramp_capped",
    "sem_psf_relief_ramp_capped_shallow",
]

# The bound invariant itself (D-01/D-04: no path commands the raw clamped max
# while sat=T) applies identically regardless of demand shape, so it runs
# over all five scenarios -- the three rail-scale twins plus the two ramp
# scenarios -- in one parametrized test (see TypePReliefBoundTests).
# Phase 13 Task 3: every new scenario that physically saturates (has sat=T
# rows) joins the bound invariant, widening it rather than leaving it
# pinned to the scenarios it was born with. sem_psf_trip_unarmed(_shallow)
# are excluded -- by design they never leave their resting position, so
# they never produce a sat=T row for the invariant to check.
TENSION_STOP_BOUND_SCENARIOS = [
    "sem_psf_mm_trip",
    "sem_psf_mm_trip_shallow",
    "sem_psf_mm_before_ms",
    "sem_psf_mm_before_ms_shallow",
    "sem_psf_ms_fallback",
    "sem_psf_ms_fallback_shallow",
    "sem_psf_trip_held_suppressed",
    "sem_psf_trip_held_suppressed_shallow",
    "sem_psf_trip_hold_release",
    "sem_psf_trip_hold_release_shallow",
]

# Phase 13 Plan 03 Task 3: every probe scenario that physically saturates
# (has sat=T rows) joins the bound invariant too -- sem_psf_probe_consumer
# proves relief is still bounded while the probe runs (the plan's own
# example); the others share the identical recipe/physics and saturate
# identically. sem_psf_probe_big_buffer(_shallow) are excluded -- at a 64mm
# buffer geometry this recipe never reaches physical saturation within the
# scenario's tick window, so they produce no sat=T row for the invariant to
# check (the same reason sem_psf_trip_unarmed is excluded above).
PROBE_BOUND_SCENARIOS = [
    "sem_psf_probe_no_consumer",
    "sem_psf_probe_no_consumer_shallow",
    "sem_psf_probe_consumer",
    "sem_psf_probe_consumer_shallow",
    "sem_psf_probe_slow_consumer",
    "sem_psf_probe_slow_consumer_shallow",
    "sem_psf_probe_small_buffer",
]

BOUND_INVARIANT_SCENARIOS = (TYPE_P_RELIEF_SCENARIOS + RAMP_CAPPED_SCENARIOS +
                             TENSION_STOP_BOUND_SCENARIOS + PROBE_BOUND_SCENARIOS)

# Phase 13 Task 1 (D-07/D-08/D-09/D-10): SYNC_TENSION_STOP_MM distance trip.
# Type-P only -- the trip's own D-14 exclusion (g_buf_sensor_type !=
# BUF_SENSOR_TYPE_D) is asserted directly against --sensor-type d, not via
# this list.
TENSION_STOP_MM_SCENARIOS = [
    "sem_psf_mm_trip",
    "sem_psf_trip_unarmed",
]

# Phase 13 Task 3 (D-08/D-09/D-25/D-26/REVIEW-04): ordering, arming, and
# hold-suppression scenarios, each a (scenario, expected-event,
# forbidden-event) triple. type_specific=True in the C table -- type-D
# exclusion for the mm/ms family is asserted once, directly, in
# TensionStopMmTripTests.test_mm_trip_excludes_type_d; it does not need a
# per-triple repeat here.
TENSION_STOP_ORDERING_TRIPLES = [
    ("sem_psf_mm_before_ms", "SYNC,TENSION_STOP:MM", "SYNC,TENSION_STOP:MS"),
    ("sem_psf_mm_before_ms_shallow", "SYNC,TENSION_STOP:MM", "SYNC,TENSION_STOP:MS"),
    ("sem_psf_ms_fallback", "SYNC,TENSION_STOP:MS", "SYNC,TENSION_STOP:MM"),
    ("sem_psf_ms_fallback_shallow", "SYNC,TENSION_STOP:MS", "SYNC,TENSION_STOP:MM"),
]

# D-26: a BL lock/prime/follow cycle armed across the whole window
# suppresses BOTH trips entirely -- no expected event, just an absence.
TENSION_STOP_HELD_SCENARIOS = [
    "sem_psf_trip_held_suppressed",
    "sem_psf_trip_held_suppressed_shallow",
]

# REVIEW-04: hold falling edge -- the accumulator/arm flag reset when the
# hold releases means no trip fires in the post-release window this
# scenario spans (see its own comment in sim_scenario.c for why that window
# is, empirically, the rest of the run).
TENSION_STOP_HOLD_RELEASE_SCENARIOS = [
    "sem_psf_trip_hold_release",
    "sem_psf_trip_hold_release_shallow",
]

# D-25 rail-scale twins of Task 1's own two scenarios (arms/fires + never
# arms), proving both hold at a shallower analog reading too.
TENSION_STOP_MM_RAIL_TWIN_SCENARIOS = [
    "sem_psf_mm_trip_shallow",
    "sem_psf_trip_unarmed_shallow",
]

# Phase 13 Plan 03 (D-15/D-16/D-19): the type-P feed probe. Both outcomes,
# plus the REVIEW-02 (window-max latch) and REVIEW-03 (threshold-ordering
# clamp) tripwires.
PROBE_SCENARIOS = [
    "sem_psf_probe_no_consumer",
    "sem_psf_probe_consumer",
    "sem_psf_probe_slow_consumer",
    "sem_psf_probe_big_buffer",
]

# D-25 rail-scale (0.7) twins of all four PROBE_SCENARIOS entries, as
# (base, twin) pairs sharing the same expected verdict.
PROBE_RAIL_TWIN_PAIRS = [
    ("sem_psf_probe_no_consumer", "sem_psf_probe_no_consumer_shallow", "NO_CONSUMER"),
    ("sem_psf_probe_consumer", "sem_psf_probe_consumer_shallow", "CONSUMER"),
    ("sem_psf_probe_slow_consumer", "sem_psf_probe_slow_consumer_shallow", "CONSUMER"),
    ("sem_psf_probe_big_buffer", "sem_psf_probe_big_buffer_shallow", "CONSUMER"),
]


def _skip_reason():
    if not os.path.isfile(SIM_BINARY):
        return (f"{SIM_BINARY} not built — run "
                f"'cmake -S tests/host -B build_sim && ninja -C build_sim' first")
    return None


TUNE_H = os.path.join(REPO_ROOT, "firmware", "include", "tune.h")
SYNC_INTERNAL_H = os.path.join(REPO_ROOT, "firmware", "include", "sync_internal.h")
SETTINGS_STORE_C = os.path.join(REPO_ROOT, "firmware", "src", "settings_store.c")


def _conf_int(name):
    """Regex-read `#define CONF_<name> <int>` out of the generated tune.h —
    same regex-the-C-source idiom scripts/test_settings_parity.py uses for
    settings_store.h/.c. Never hardcode a firmware constant as a literal."""
    with open(TUNE_H) as f:
        text = f.read()
    m = re.search(r"#define\s+CONF_" + re.escape(name) + r"\s+(-?\d+)\b", text)
    if not m:
        raise AssertionError(f"CONF_{name} not found in {TUNE_H} — run scripts/gen_config.py first")
    return int(m.group(1))


def _conf_float(name):
    """Float sibling of _conf_int — reads `#define CONF_<name> <float>f`."""
    with open(TUNE_H) as f:
        text = f.read()
    m = re.search(r"#define\s+CONF_" + re.escape(name) + r"\s+(-?[\d.]+)f\b", text)
    if not m:
        raise AssertionError(f"CONF_{name} not found in {TUNE_H} — run scripts/gen_config.py first")
    return float(m.group(1))


class SimRun:
    """Result of one flare_sim subprocess invocation."""

    def __init__(self, returncode, stdout, stderr):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        self.rows = list(csv.DictReader(io.StringIO(stdout))) if stdout else []

    def zones(self):
        return [r["zone"] for r in self.rows]

    def sats(self):
        return [r["sat"] for r in self.rows]

    def events_text(self):
        return " ".join(r["events"] for r in self.rows)


def run_scenario(scenario, sensor_type="d", ticks=400, stress=False, stress_lag_ms=None):
    # ticks=None lets the scenario's own tick_ceiling (tests/host/sim_scenario.c)
    # apply — needed for RELOAD scenarios, whose dwell/join-delay/approach/
    # follow sequence runs well past the 60s default.
    args = [SIM_BINARY, "--scenario", scenario, "--sensor-type", sensor_type]
    if ticks is not None:
        args += ["--ticks", str(ticks)]
    if stress:
        args.append("--stress")
        if stress_lag_ms is not None:
            args += ["--stress-lag-ms", str(stress_lag_ms)]
    # Never reuse a process across scenarios — process-per-scenario isolation is
    # what removes the need to hand-reset ~970 global references (design.md
    # "One scenario per process"). If the suite ever needs to be faster, the
    # sanctioned remedy is sharding scenarios across parallel processes, not
    # reusing one process for multiple scenarios.
    proc = subprocess.run(args, capture_output=True, text=True, timeout=30)
    return SimRun(proc.returncode, proc.stdout, proc.stderr)


def save_failure_trace(test_name, run):
    d = tempfile.mkdtemp(prefix="flare_sim_fail_")
    path = os.path.join(d, f"{test_name}.csv")
    with open(path, "w", encoding="utf-8") as f:
        f.write(run.stdout)
    return path


@unittest.skipIf(_skip_reason(), _skip_reason())
class InvariantSuiteTests(unittest.TestCase):
    """Every catalogued scenario, both sensor types (unless type-specific),
    must run to completion with zero invariant violations — the value that
    needs no per-scenario authoring (design.md "Global Invariants")."""

    def _assert_clean(self, scenario, sensor_type):
        run = run_scenario(scenario, sensor_type=sensor_type)
        if run.returncode != 0:
            path = save_failure_trace(f"{scenario}_{sensor_type}", run)
            self.fail(f"{scenario}/{sensor_type}: {run.stderr.strip()} (trace: {path})")
        self.assertGreater(len(run.rows), 0, f"{scenario}/{sensor_type}: empty trace")

    def test_dual_type_scenarios(self):
        for scenario in ALL_DUAL_TYPE_SCENARIOS:
            for sensor_type in ("d", "p"):
                with self.subTest(scenario=scenario, sensor_type=sensor_type):
                    self._assert_clean(scenario, sensor_type)

    def test_type_d_only_scenarios(self):
        for scenario in TYPE_D_ONLY_SCENARIOS:
            with self.subTest(scenario=scenario):
                self._assert_clean(scenario, "d")

    def test_reload_scenarios(self):
        for scenario in RELOAD_SCENARIOS:
            with self.subTest(scenario=scenario):
                run = run_scenario(scenario, sensor_type="p", ticks=None)
                if run.returncode != 0:
                    path = save_failure_trace(f"{scenario}_p", run)
                    self.fail(f"{scenario}/p: {run.stderr.strip()} (trace: {path})")


@unittest.skipIf(_skip_reason(), _skip_reason())
class PerScenarioAssertionTests(unittest.TestCase):
    """Trace-based checks beyond the generic invariants: expected zone/rail
    reached, matching design.md's Buffer Plant Model and Fault Injection
    scenarios."""

    def test_steady_demand_exceeds_feed_drives_tension_before_settling(self):
        run = run_scenario("steady", sensor_type="d", ticks=100)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("TENSION", run.zones(),
                      "steady: expected an early TENSION zone before feed ramps to match demand")

    def test_long_retract_saturates_compression_rail(self):
        run = run_scenario("long_retract", sensor_type="d", ticks=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("C", run.sats(),
                      "long_retract: expected the compression rail to saturate — see "
                      "spec.md's corrected 'Retract longer than half travel' scenario")

    def test_both_switches_fault_reaches_buf_fault(self):
        run = run_scenario("both_switches_fault", sensor_type="d", ticks=200)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("FAULT", run.zones(),
                      "both_switches_fault: expected buf_state_raw() to resolve to BUF_FAULT")

    def test_jam_upstream_feed_cannot_recover_buffer(self):
        # feed_gain -> 0 mid-scenario: sim_plant.c stops translating commanded
        # feed into slack movement from that timestamp, modeling filament
        # stuck upstream. The buffer should end up pinned at (or driven
        # toward) the tension rail despite the firmware still commanding feed.
        run = run_scenario("jam_upstream", sensor_type="d", ticks=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("T", run.sats(), "jam_upstream: expected the tension rail to be reached")


@unittest.skipIf(_skip_reason(), _skip_reason())
class ReloadFixEventTests(unittest.TestCase):
    """Event-level assertions for audit-reliability-fixes H4/H5/H6. These are
    sim-level screens for logic/deadlock defects only — they do not, and per
    design.md's authority boundary must not, check off that change's
    remaining HW: tasks (10.3, 11.3, 12.4)."""

    def test_h6_genuine_runout_escalates_to_reload_not_fault_hold_loop(self):
        run = run_scenario("reload_genuine_runout_escalation", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("RUNOUT,1", events)
        self.assertIn("RELOAD:SWITCHING,1->2", events)
        fault_hold_count = events.count("SYNC,FAULT_HOLD")
        self.assertEqual(fault_hold_count, 0,
                         "expected the H6 escalation path, not the SYNC:FAULT_HOLD loop it replaces")

    def test_fast_runout_escalates_via_rail_guard_not_fault_hold_loop(self):
        # psf-runout-escalation-race-fix: confirmed on real type-P rig
        # 2026-07-27 that a fast/complete runout (high demand, saturates
        # the tension rail within CONF_PSF_WALL_SAT_MS ~1s) never escalated
        # to RELOAD -- sync_tick_type_p_rail_guard's fault-hold path fires
        # first every cycle and short-circuits the tick before H6's slower
        # (~6s) tension-dwell escalation ever runs. Fixed by sharing the
        # runout-escalation check between both fault-hold entry paths.
        run = run_scenario("reload_fast_runout_rail_guard_race", sensor_type="p", ticks=1150)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("RUNOUT,1", events)
        self.assertIn("RELOAD:SWITCHING,1->2", events)
        self.assertEqual(events.count("SYNC,FAULT_HOLD"), 0,
                         "expected escalation via the rail-saturation path, not the "
                         "FAULT_HOLD/FAULT_HOLD_RECOVERY loop this fix replaces")

    def test_h4_idle_consumer_completes_on_staged_compression_no_follow_jam(self):
        run = run_scenario("reload_idle_consumer_staged_completion", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("RELOAD:LOADED,2", events)
        self.assertNotIn("FOLLOW_JAM", events)

    def test_h5_rl_on_already_loaded_lane_is_a_noop(self):
        run = run_scenario("reload_already_loaded_noop", sensor_type="p", ticks=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("RELOAD:LOADED,1", events)
        # No RELOAD:JOINING / TC:* motion should follow the manual RL: call —
        # a restart of approach/follow is exactly the H5 regression.
        self.assertNotIn("RELOAD:JOINING", events)

    def test_multi_lane_failover_symmetry_lane2_to_lane1(self):
        run = run_scenario("reload_runout_lane2_to_lane1", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("RUNOUT,2", events)
        self.assertIn("RELOAD:SWITCHING,2->1", events)
        self.assertIn("RELOAD:LOADED,1", events)
        self.assertEqual(events.count("SYNC,FAULT_HOLD"), 0)
        self.assertNotIn("FOLLOW_JAM", events)

    def test_runout_target_empty_aborts_without_switching(self):
        run = run_scenario("reload_target_empty_abort", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("RUNOUT,1", events)
        self.assertIn("!RELOAD:FAULT,NO_FILAMENT", events)
        self.assertNotIn("RELOAD:SWITCHING", events)
        self.assertNotIn("RELOAD:JOINING", events)
        self.assertEqual(run.rows[-1]["sync_state"], "OFF")

    def test_manual_resume_on_empty_active_lane_triggers_swap(self):
        run = run_scenario("reload_manual_resume_empty_active", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("RELOAD:SWITCHING,1->2", events)
        self.assertIn("RELOAD:LOADED,2", events)
        self.assertNotIn("FOLLOW_JAM", events)

    def test_mmu_mode_runout_disables_sync_without_escalation(self):
        run = run_scenario("reload_mmu_mode_no_escalation", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("RUNOUT,1", events)
        self.assertNotIn("RELOAD:SWITCHING", events)
        self.assertEqual(run.rows[-1]["sync_state"], "OFF")


@unittest.skipIf(_skip_reason(), _skip_reason())
class SyncStateModelTests(unittest.TestCase):
    """Scenarios derived directly from openspec/specs/sync-state-model's
    `#### Scenario:` blocks — see tests/host/sim_scenario.c for the mapping
    from each scenario to the sync.c mechanism it exercises."""

    def test_relief_pause_preserves_state_and_rearms_on_tension(self):
        # Spec scenarios "Enter relief pause without losing state" + "Resume
        # on TENSION re-arm": demand pauses (buffer overfeeds into
        # compression, SYNC_RELIEF_PAUSE entered), then resumes (buffer
        # drains back toward tension) and the controller should return to
        # SYNC_ACTIVE without a cold restart.
        run = run_scenario("sem_relief_pause_lifecycle", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("SYNC,RELIEF_PAUSE", events)
        states = [r["sync_state"] for r in run.rows]
        self.assertIn("RELIEF_PAUSE", states)
        # Must return to ACTIVE after the resume, not stay paused or go OFF.
        self.assertEqual(states[-1], "ACTIVE",
                         "expected the controller back in SYNC_ACTIVE after demand resumed")

    def test_fault_hold_recovers_standalone_on_schedule(self):
        # Spec scenario "Standalone recovery": recovers after the configured
        # interval with no host command. The underlying jam here is
        # permanent (feed_gain never restored), so the recovered feed can't
        # physically move the buffer and it re-enters FAULT_HOLD shortly
        # after — that repeat cycle is expected for a genuinely unrecoverable
        # jam, not asserted against; only that recovery fires at all.
        run = run_scenario("sem_fault_hold_standalone_recovery", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("SYNC,FAULT_HOLD", events)
        self.assertIn("SYNC,FAULT_HOLD_RECOVERY", events)


@unittest.skipIf(_skip_reason(), _skip_reason())
class BufferStateLockTests(unittest.TestCase):
    """Scenarios derived from openspec/specs/buffer-state-lock's
    `#### Scenario:` blocks. BL:<state> host-command framing (`OK` ack,
    `ER:BUSY` rejection) is protocol.c-level and out of sim scope — these
    call `sync_buffer_lock_arm()`/`sync_retract_assist_set()` directly.

    Buffer-state-lock scenarios test prime, lock, break, and catch.
    On lock-break, firmware emits EV:BL:BREAK then EV:BL:FOLLOW."""

    def test_prime_locks_at_switch_and_holds_against_spring(self):
        run = run_scenario("sem_bl_release_via_bs", sensor_type="d", ticks=250)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("BL,PRIME", events)
        self.assertIn("BL,LOCKED", events)
        # Lock holds against the buffer spring: zero commanded feed the whole
        # time it's locked, and the buffer stays exactly where it locked.
        locked_rows = [r for r in run.rows if r["sync_state"] == "RETRACT_ASSIST"
                      and int(r["ts_ms"]) > 1200]
        self.assertTrue(locked_rows)
        self.assertTrue(all(r["feed_sps"] == "0" for r in locked_rows))
        positions = {r["bp_mm"] for r in locked_rows}
        self.assertEqual(len(positions), 1, "expected the locked position to stay constant")

    def test_bs_releases_lock_to_sync_off(self):
        run = run_scenario("sem_bl_release_via_bs", sensor_type="d", ticks=250)
        self.assertEqual(run.returncode, 0, run.stderr)
        states = [r["sync_state"] for r in run.rows]
        self.assertEqual(states[-1], "OFF")

    def test_lock_break_engages_catch_same_tick(self):
        run = run_scenario("sem_bl_lock_catch", sensor_type="d", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("BL,BREAK", events)
        self.assertIn("BL,FOLLOW", events)
        self.assertLess(events.index("BL,BREAK"), events.index("BL,FOLLOW"))

    def test_bare_bl_is_passive_under_retract(self):
        # buffer-state-lock D5 / 12-SPEC §3: without follow args the lock never
        # arms the catch. A 15 mm/s external retract at 6-9 s must not produce
        # BREAK/FOLLOW nor any commanded motor rate; BS at 10 s releases.
        for sensor_type in ("d", "p"):
            with self.subTest(sensor_type=sensor_type):
                run = run_scenario("sem_bl_bare_passive", sensor_type=sensor_type, ticks=None)
                self.assertEqual(run.returncode, 0, run.stderr)
                events = run.events_text()
                self.assertIn("BL,LOCKED", events)
                self.assertNotIn("BL,BREAK", events)
                self.assertNotIn("BL,FOLLOW", events)
                locked_rows = [r for r in run.rows if r["sync_state"] == "RETRACT_ASSIST"
                               and 2000 < int(r["ts_ms"]) < 10000]
                self.assertTrue(locked_rows)
                self.assertTrue(all(float(r["motor_sps"]) == 0.0 for r in locked_rows),
                                "bare BL commanded motor motion while locked")
                self.assertEqual(run.rows[-1]["sync_state"], "OFF")

    def test_watchdog_auto_releases_after_default_timeout(self):
        run = run_scenario("sem_bl_watchdog_timeout", sensor_type="d", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("!BL,TIMEOUT", events)  # cmd_event_critical -> "!" prefix in the trace
        self.assertEqual(run.rows[-1]["sync_state"], "OFF")

    def test_type_p_fast_prime_prediction(self):
        run = run_scenario("sem_bl_release_via_bs", sensor_type="p", ticks=250)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("BL,PRIME", events)
        self.assertIn("BL,LOCKED", events)
        locked_rows = [r for r in run.rows if r["sync_state"] == "RETRACT_ASSIST"
                       and 1400 <= int(r["ts_ms"]) < 4000]
        self.assertTrue(locked_rows)
        positions = [float(r["bp_mm"]) for r in locked_rows]
        self.assertTrue(all(-12.5 < p < -10.0 for p in positions),
                        f"expected locked position near rail without slamming: {positions[:3]}")
        self.assertEqual(len(set(r["bp_mm"] for r in locked_rows)), 1)
        self.assertEqual(run.rows[-1]["sync_state"], "OFF")

    def test_type_p_catch_escalation(self):
        run = run_scenario("sem_bl_lock_catch", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("BL,PRIME", events)
        self.assertIn("BL,LOCKED", events)
        self.assertIn("BL,BREAK", events)
        self.assertIn("BL,FOLLOW", events)
        self.assertIn("BL,FOLLOW_DONE", events)
        self.assertLess(events.index("BL,PRIME"), events.index("BL,LOCKED"))
        self.assertLess(events.index("BL,LOCKED"), events.index("BL,BREAK"))
        self.assertLess(events.index("BL,BREAK"), events.index("BL,FOLLOW"))
        self.assertLess(events.index("BL,FOLLOW"), events.index("BL,FOLLOW_DONE"))
        pre_demand_locked = [r for r in run.rows if 1400 <= int(r["ts_ms"]) < 6000]
        self.assertTrue(pre_demand_locked)
        self.assertEqual(len(set(r["bp_mm"] for r in pre_demand_locked)), 1)
        catch_start_pos = float([r["bp_mm"] for r in run.rows if int(r["ts_ms"]) == 6220][0])
        catch_end_pos = float([r["bp_mm"] for r in run.rows if int(r["ts_ms"]) == 6540][0])
        self.assertLess(catch_end_pos, catch_start_pos,
                        f"expected catch to pull toward tension: {catch_start_pos} -> {catch_end_pos}")

    def test_type_p_shallow_rail_still_follows_retract(self):
        """Regression for the 2026-09-12 UNLOAD_TIMEOUT: a rig whose tension
        hard end reads shallower than PSF_BREAK_THRESHOLD_NORM (-0.70 here,
        baseline capture saw -0.68) never passed the absolute "engaged" test,
        so an extruder retract longer than the buffer produced no BL:BREAK/
        FOLLOW — the buffer pinned at the compression rail, the extruder
        skipped against the held MMU and the tip never parked. The break must
        be detected relative to the reading actually seen at the rail, and it
        must not depend on a settle dwell between LOCKED and the retract."""
        for scenario in ("bl_retract_immediate", "bl_retract_paused"):
            with self.subTest(scenario=scenario):
                run = run_scenario(scenario, sensor_type="p", ticks=None)
                self.assertEqual(run.returncode, 0, run.stderr)
                events = run.events_text()
                self.assertIn("BL,PRIME_BOUND", events)
                self.assertIn("BL,LOCKED", events)
                self.assertIn("BL,BREAK", events, "retract never broke the lock")
                self.assertIn("BL,FOLLOW", events, "MMU never followed the retract")
                self.assertLess(events.index("BL,LOCKED"), events.index("BL,BREAK"))
                # The MMU followed the whole 43 mm: buffer ends on the tension
                # side, not pinned at +half-travel (compression) like the rig.
                self.assertLess(float(run.rows[-1]["bp_mm"]), 0.0,
                                f"buffer ended at {run.rows[-1]['bp_mm']} mm (compression)")
                self.assertNotIn("C", run.sats(), "buffer touched the compression rail")


@unittest.skipIf(_skip_reason(), _skip_reason())
class CutterFeedTimeoutTests(unittest.TestCase):
    """Scenarios derived from openspec/specs/cutter-feed-timeout's
    `#### Scenario:` blocks. GET:/SET: protocol exposure scenarios are
    protocol.c-level and out of sim scope — these call `cutter_start()`
    directly (cutter_tick() already runs every sim tick).

    Spec/code mismatch found while building these (4th this project, see
    memories/repo/host-sync-sim.md): both timeout scenarios say
    `cutter_abort()` is called and `CUT:ERROR ABORTED` is emitted. The real
    phase-timeout path calls `cutter_fail(reason)` instead, emitting
    `CUT:ERROR,<reason>` (`FEED_TIMEOUT`/`OPEN_TIMEOUT`/etc) — `cutter_abort()`
    is a different, external-abort-only entry point that never fires from a
    phase timeout. Asserted against the real event text here."""

    def test_large_feed_completes_without_abort(self):
        run = run_scenario("sem_cutter_large_feed_completes", sensor_type="d", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("CUT:DONE", events)
        self.assertNotIn("CUT:ERROR", events)

    def test_feed_timeout_fires_on_genuine_jam(self):
        run = run_scenario("sem_cutter_feed_timeout_jam", sensor_type="d", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("!CUT:ERROR,FEED_TIMEOUT", events)

    def test_settle_completes_when_timeout_exceeds_servo_settle(self):
        run = run_scenario("sem_cutter_settle_completes", sensor_type="d", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("CUT:DONE", events)
        self.assertNotIn("CUT:ERROR", events)

    def test_abort_fires_when_servo_settle_exceeds_timeout(self):
        run = run_scenario("sem_cutter_settle_timeout_abort", sensor_type="d", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("!CUT:ERROR,OPEN_TIMEOUT", events)


@unittest.skipIf(_skip_reason(), _skip_reason())
class MotionSafetyTests(unittest.TestCase):
    """Scenarios derived from openspec/specs/motion-safety's `#### Scenario:`
    blocks. Autopreload's "Fresh Insertion" scenario is untestable —
    autopreload_tick() is main.c-static, never linked into the sim (see
    design.md Known Limitations). Non-destructive relief/BL-prime-cap/
    standalone-recovery scenarios overlap sync-state-model and buffer-state-lock,
    already covered there."""

    def test_dry_spin_halts_motor_and_blocks_restart(self):
        # "Filament Lost Mid-Task": TASK_FEED, both switches clear, buffer not
        # BUF_TENSION, sustained > 8s -> FAULT:DRY_SPIN, and the fault blocks
        # sync from restarting the lane (verified physically: the buffer
        # trace after the fault must be consistent with zero real feed, not
        # just g_sync_current_sps, which keeps accumulating internally even
        # while gated — see memories/repo/host-sync-sim.md).
        run = run_scenario("sem_motion_dry_spin_probe", sensor_type="d", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("!FAULT:DRY_SPIN,1", events)

    def test_hard_wall_critical_fault_hold_path_is_dead_code(self):
        # Finding, not a design assertion: sync.c's "hard-wall critical"
        # FAULT_HOLD path (sync_tick_apply_rate) computes
        # compression_wall_critical only under BUF_SENSOR_TYPE_D, then only
        # acts on it when sensor type is NOT D — the AND can never be true,
        # so this specific escalation is permanently unreachable for both
        # sensor types. motion-safety's "Hard-wall critical triggers
        # FAULT_HOLD" scenario describes behavior the code doesn't have.
        # `idle_zero`/type-D drives a fast compression push (~63 mm/s,
        # comfortably past the 0.25 mm/s / 350 ms thresholds) that would
        # trigger it if reachable; asserts it doesn't fire, documenting
        # current (not spec-stated) behavior as a regression guard.
        run = run_scenario("idle_zero", sensor_type="d", ticks=3000)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertNotIn("SYNC,FAULT_HOLD", events)


@unittest.skipIf(_skip_reason(), _skip_reason())
class RelayFallbackTests(unittest.TestCase):
    """Scenarios derived from openspec/specs/relay-fallback-only's `####
    Scenario:` blocks. "Firmware drops the duty-estimator machinery" /
    "Protocol drops estimator telemetry fields" / "Analyzer emits no relay
    duty recommendations" / "Config surface drops the dead relay keys" are
    build/protocol/python/config-generator scenarios, none observable from
    flare_sim's runtime trace — skipped.

    Spec/code mismatch found (6th this project, see
    memories/repo/host-sync-sim.md): "Catch-up and stop branches preserved"
    says COMPRESSION drives `SYNC_MIN` (682 by default). Real
    `relay_control_law()` (sync_relay.c) returns a literal `0` for
    BUF_COMPRESSION, and the sim confirms it: sustained compression
    converges to and holds exactly 0, never 682, for the full run."""

    def test_neutral_and_tension_stay_within_relay_bounds(self):
        run = run_scenario("sem_relay_fallback_probe", sensor_type="d", ticks=400)
        self.assertEqual(run.returncode, 0, run.stderr)
        neutral_feeds = [int(r["feed_sps"]) for r in run.rows if r["zone"] == "NEUTRAL"]
        tension_feeds = [int(r["feed_sps"]) for r in run.rows if r["zone"] == "TENSION"]
        self.assertTrue(neutral_feeds and tension_feeds)
        # relay_control_law's NEUTRAL branch clamps to [g_sync_min_sps, relay_base];
        # TENSION is relay_base * RELAY_CATCHUP_FRAC(1.3), clamped elsewhere to
        # <= g_sync_max_sps (15004 default) -- bounds check, not exact formula.
        self.assertTrue(all(f >= 682 for f in neutral_feeds if f > 0))
        self.assertTrue(all(f <= _conf_int("SYNC_MAX_SPS") for f in tension_feeds))

    def test_compression_converges_to_zero_not_sync_min(self):
        run = run_scenario("idle_zero", sensor_type="d", ticks=3000)
        self.assertEqual(run.returncode, 0, run.stderr)
        compression_feeds = [int(r["feed_sps"]) for r in run.rows if r["zone"] == "COMPRESSION"]
        self.assertTrue(compression_feeds)
        self.assertEqual(compression_feeds[-1], 0,
                         "expected sustained COMPRESSION to converge to 0 (relay_control_law's "
                         "real return value), not SYNC_MIN as the spec states")


@unittest.skipIf(_skip_reason(), _skip_reason())
class PersistenceContractTests(unittest.TestCase):
    """Scenarios derived from openspec/specs/persistence-contract's `####
    Scenario:` blocks. "Settings Version Bump" / "Runtime Tunables Flow" /
    "Persisted fields round-trip symmetrically" / "no gratuitous version
    bump" are static/structural — already enforced by
    scripts/test_settings_parity.py, not `flare_sim`'s job to re-check.

    Spec/code mismatch found (8th this project, see
    memories/repo/host-sync-sim.md): "Fresh Board" says invalid magic/CRC
    settings are written back to flash immediately. Real `settings_load()`
    (settings_store.c) calls `settings_defaults()` on that path and returns
    — no `settings_save()` call, no write-back. Confirmed via
    `sem_persistence_fresh_board`, which reports written_back=0 on stderr."""

    def test_fresh_board_falls_back_but_does_not_write_back(self):
        args = [SIM_BINARY, "--scenario", "sem_persistence_fresh_board", "--sensor-type", "d"]
        proc = subprocess.run(args, capture_output=True, text=True, timeout=30)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("fresh_board_was_pristine=1", proc.stderr)
        self.assertIn("written_back=0", proc.stderr,
                      "expected settings_load() to NOT write defaults back to flash on a "
                      "fresh/invalid-magic board, contrary to the spec's stated behavior")


@unittest.skipIf(_skip_reason(), _skip_reason())
class SyncRefactorTests(unittest.TestCase):
    """Scenarios derived from openspec/specs/sync-refactor's runtime-relevant
    requirements (30 total; most are Klipper-sidecar/analyzer/protocol-
    rename scope, not linked into flare_sim — see
    memories/repo/host-sync-sim.md for the full disposition).

    "Type-D standalone buffer control is a hysteretic relay" is already
    covered by RelayFallbackTests. "Normal switch contact does not trigger
    FAULT_HOLD" is already covered by MotionSafetyTests'
    hard-wall-critical-is-dead-code finding (its intent — normal contact
    never faults — holds, just via unreachable code rather than an explicit
    type-P gate as this spec's wording implies).

    "Type-D compression relief is overfill-budgeted" — RESOLVED, no longer
    inconclusive. The earlier block was a sim-methodology gap, not a real
    one: `sync_check_continuous_compression` (sync.c ~1767) is gated on
    `g_sync_auto_started`, which the sim's `start_sync_active` shortcut
    never set (bypasses the organic auto-engage path). Fixed by driving
    `sync_tick_auto_start_stop` (sync.c:1319) organically instead —
    `auto_mode=true`, `start_sync_active=false`, and one lane's OUT forced
    false (that function's both-loaded guard blocks auto-start otherwise;
    the sim boot default is both lanes' OUT true). `sem_sync_overfill_budget_probe`
    reuses `idle_zero`'s demand profile this way and reaches genuine
    `g_sync_auto_started=true`.

    9th spec/code mismatch found this way: the spec's "overfill-budgeted"
    wording implies a small (~3mm) distance-based trigger. The only
    RELIEF_PAUSE path reachable from sustained compression is
    `sync_check_continuous_compression`'s dwell timer — TIME-based
    (`CONF_SYNC_AUTO_STOP_MS`, 5000ms default), gated on feed already at
    the compression floor, not a distance budget at all. Confirmed
    empirically: compression onset ~t=3500, `SYNC,RELIEF_PAUSE` at t=8280
    — a ~4780ms dwell, matching the 5000ms constant, not a small-mm
    trigger. (The overfill-budget globals, `g_sync_relieve_effort_mm`/
    `g_sync_compression_drain_budget_mm`, do exist and are real — they gate
    a completely different mechanism, `sync_type_d_compression_drain_target()`,
    a partial-feed "drain" rate during active-demand compression, not
    RELIEF_PAUSE entry.)"""

    def test_tension_feeds_compression_backs_off(self):
        # "Sync control polarity matches the state contract" — feed
        # increases in TENSION, backs off in COMPRESSION, for both sensor
        # types. Already implicit in every scenario in the catalogue;
        # asserted explicitly here against `steady`.
        # `steady` never reaches COMPRESSION for type-P within a reasonable
        # window (type-P's continuous PD is more damped than type-D's
        # bang-bang relay) -- `burst` does, cleanly, within 600 ticks.
        cases = [("steady", "d", 400), ("burst", "p", 600)]
        for scenario, sensor_type, ticks in cases:
            with self.subTest(sensor_type=sensor_type):
                run = run_scenario(scenario, sensor_type=sensor_type, ticks=ticks)
                self.assertEqual(run.returncode, 0, run.stderr)
                tension_feeds = [int(r["feed_sps"]) for r in run.rows if r["zone"] == "TENSION"]
                compression_feeds = [int(r["feed_sps"]) for r in run.rows
                                     if r["zone"] == "COMPRESSION"]
                self.assertTrue(tension_feeds and compression_feeds)
                # Not a strict per-tick inequality (ramping/filters lag one
                # tick behind the zone label) -- compare characteristic
                # levels: peak commanded feed while TENSION must exceed the
                # typical feed while COMPRESSION, confirming the polarity
                # (refill vs. back-off), not an exact control law.
                self.assertGreater(max(tension_feeds), max(compression_feeds) // 2,
                                   "expected TENSION to command materially more feed than "
                                   "COMPRESSION (refill vs. back-off polarity)")

    def test_compression_relief_is_dwell_timed_not_distance_budgeted(self):
        # "Type-D compression relief is overfill-budgeted" -- 9th spec/code
        # mismatch (see class docstring): RELIEF_PAUSE fires off a ~5s dwell
        # timer (CONF_SYNC_AUTO_STOP_MS), not a small distance budget.
        # Asserted as a time window (7-9s from scenario start), not an exact
        # tick, since the compression-onset timing has some slack -- what
        # matters is that it's seconds, not the near-instant firing a small
        # mm-based trigger would produce.
        run = run_scenario("sem_sync_overfill_budget_probe", sensor_type="d", ticks=500)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("SYNC,RELIEF_PAUSE", events)
        relief_row = next(r for r in run.rows if "RELIEF_PAUSE" in r["events"])
        relief_ms = int(relief_row["ts_ms"])
        self.assertGreater(relief_ms, 7000,
                           "RELIEF_PAUSE fired too fast to be the ~5s dwell timer")
        self.assertLess(relief_ms, 9000,
                        "RELIEF_PAUSE took much longer than the ~5s dwell timer predicts")


class PsfTypePSensorTests(unittest.TestCase):
    """Scenarios derived from openspec/specs/psf-type-p-sensor (16
    requirements, 48 scenarios). Heavy overlap with already-built coverage:
    "Type-P Relief-Pause Auto-Recovery"'s "recovers under demand" scenario
    and "Type-P Fault Timers Scoped to Active Sync"'s "Fault recovery does
    not instantly re-fault" scenario are both already exercised by
    SyncStateModelTests (sem_relief_pause_lifecycle, already runs type-P;
    sem_fault_hold_standalone_recovery's docstring confirms the
    ~1.5s-not-instant re-fault cadence the fault-timers requirement
    guarantees). "Hard Catch and Print-Stop Detection"'s two saturation
    scenarios are also that same pair (PSF_WALL_SAT_MS compression ->
    relief_pause, tension -> fault_hold).

    Out of scope for flare_sim (protocol.c CAL:/SET:/GET: commands not
    linked): PSF Endpoint Calibration, BUF_GOAL User Param, Remove
    BUF_RANGE/BUF_INVERT. Out of scope (no distinguishing external
    behavior without internal-state export / noise injection): Filtered
    Derivative. Out of scope (spec explicitly says "measured against a
    real print, not isolated bench bursts"): Type-P Feed Quality and
    Reliable Stabilize, beyond what Stabilize Rail Breakaway already covers.

    Net-new scenario built: "Type-P Stabilize Rail Breakaway" — untested by
    any prior scenario (none exercised `BS`/boot-stabilize under type-P).
    sem_psf_stab_rail_breakaway confirms the stagnation guard does NOT
    abort on the short-window test while saturated (BUF_STAB:DONE at
    t=3740, well inside the 3000ms PSF_STAB_RAIL_BREAK_MS cap from BS at
    t=2000); sem_psf_stab_rail_break_timeout (feed_gain zeroed at BS, an
    uncoupled/jammed lane) confirms the cap itself fires
    (BUF_STAB:STAGNANT_TIMEOUT at exactly t=5000 = 2000+3000)."""

    def test_loaded_buffer_breaks_off_rail_without_early_abort(self):
        run = run_scenario("sem_psf_stab_rail_breakaway", sensor_type="p", ticks=500)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("BUF_STAB,DONE", events)
        self.assertNotIn("STAGNANT_TIMEOUT", events)

    def test_stuck_buffer_aborts_at_breakaway_cap(self):
        run = run_scenario("sem_psf_stab_rail_break_timeout", sensor_type="p", ticks=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("BUF_STAB,STAGNANT_TIMEOUT", events)
        self.assertNotIn("BUF_STAB,DONE", events)

    def test_brief_retract_brake_recovers_without_stopping(self):
        # "Hard Catch and Print-Stop Detection" / "Slowdown recovers": no new
        # scenario needed -- the existing `retract` scenario (a brief 500ms
        # retract pulse) run under type-P already is this scenario. The
        # rapid compression-direction velocity spike engages
        # `sync_fast_brake`, but the retract ends and the buffer settles at
        # 7.5mm compression (short of the 12.5mm rail) well before any
        # sustained-saturation path could fire -- confirmed empirically: no
        # RELIEF_PAUSE/FAULT_HOLD, sync stays ACTIVE throughout.
        run = run_scenario("retract", sensor_type="p", ticks=400)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertNotIn("RELIEF_PAUSE", events)
        self.assertNotIn("FAULT_HOLD", events)
        states = {r["sync_state"] for r in run.rows}
        self.assertEqual(states, {"ACTIVE"})

    def test_sustained_retract_confirms_stop_and_enters_relief_pause(self):
        # "Real stop confirmed": `long_retract` (200mm/s retract) under
        # type-P saturates the compression rail and, unlike the brief
        # `retract` case above, stays pinned there -- the controller enters
        # `sync_relief_pause()` (SYNC,RELIEF_PAUSE at t=3420 in a fixed
        # run, confirmed empirically, exact timing not asserted since it
        # depends on the filtered-derivative decay, not a pinned constant).
        run = run_scenario("long_retract", sensor_type="p", ticks=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("SYNC,RELIEF_PAUSE", events)

    def test_normal_unload_completes_without_guard_interference(self):
        # "Type-P Unload Uses No Position-Based Over-Tension Guard" /
        # "Type-P normal retract is not blocked": OUT clears 1000ms into the
        # retract -> falls through to the deadline-tracked completion path,
        # UNLOADED fires for both sensor types with no guard interference.
        for sensor_type in ("d", "p"):
            with self.subTest(sensor_type=sensor_type):
                run = run_scenario("sem_psf_unload_normal", sensor_type=sensor_type, ticks=400)
                self.assertEqual(run.returncode, 0, run.stderr)
                events = run.events_text()
                self.assertIn("UNLOADED,1", events)
                self.assertNotIn("UNLOAD_BLOCKED", events)

    def test_stuck_unload_type_p_skips_tension_block_type_d_fires_it(self):
        # "Type-D unload guard unchanged" / "Type-P stuck unload falls
        # through to the distance limit": OUT held present the whole run
        # (extruder gripping) -- type-D's UNLOAD_TENSION_BLOCK dwell fires
        # at t=6200 (UL at t=1000 + CONF_UNLOAD_TENSION_BLOCK_MS 5000 +
        # margin); type-P has no position-based guard at all and reaches
        # neither UNLOAD_BLOCKED nor (within this invariant-safe 12s
        # window) the much-larger-scale UNLOAD_MAX distance timeout --
        # confirmed empirically that chasing the real UNLOAD_TIMEOUT here
        # collides with the generic 20s saturation invariant (the buffer's
        # small physical rail saturates in ~1s; UNLOAD_MAX operates on the
        # whole filament path, three orders of magnitude larger), so only
        # the guard's absence is asserted, not the eventual distance-limit
        # fallback.
        run_d = run_scenario("sem_psf_unload_stuck", sensor_type="d", ticks=600)
        self.assertEqual(run_d.returncode, 0, run_d.stderr)
        self.assertIn("UNLOAD_BLOCKED", run_d.events_text())

        run_p = run_scenario("sem_psf_unload_stuck", sensor_type="p", ticks=600)
        self.assertEqual(run_p.returncode, 0, run_p.stderr)
        self.assertNotIn("UNLOAD_BLOCKED", run_p.events_text())

    def test_idle_dwell_does_not_fault_on_organic_engagement(self):
        # "Type-P Fault Timers Scoped to Active Sync" / "Normal extrude does
        # not fault on engagement": 8s idle (sync OFF) before demand kicks
        # in and organically engages sync via sync_tick_auto_start_stop.
        # Confirmed for both sensor types (the type-D "unchanged" claim in
        # the same requirement group, checked incidentally): clean
        # SYNC,AUTO_START, zero FAULT_HOLD anywhere in the run.
        for sensor_type in ("d", "p"):
            with self.subTest(sensor_type=sensor_type):
                run = run_scenario("sem_psf_no_fault_on_idle_engagement",
                                   sensor_type=sensor_type, ticks=800)
                self.assertEqual(run.returncode, 0, run.stderr)
                events = run.events_text()
                self.assertIn("SYNC,AUTO_START", events)
                self.assertNotIn("FAULT_HOLD", events)

    def test_type_p_relief_rises_without_snapping(self):
        # Supersedes "Type-P Tension Refill Snap" (psf-type-p-sensor spec):
        # Phase 13 SC#1 replaced the direct-apply snap-to-max_sps this test
        # used to assert with bounded relief (13-01-PLAN.md Task 1). Relief
        # is NOT disabled -- feed still rises measurably and promptly once
        # the buffer enters the relief zone -- but it must never reach
        # CONF_SYNC_MAX_SPS on a row the plant marks tension-saturated
        # (sat=="T").
        #
        # Scenario choice: NOT step_up (this test's own former scenario).
        # step_up's demand (40mm/s) exceeds the hardware's max deliverable
        # rate (~36.7mm/s) so thoroughly that feed legitimately CONVERGES to
        # exactly max_sps and holds there roughly 1.2s before physical
        # saturation ever registers (confirmed empirically this task,
        # scripts/test_sync_sim.py history) -- correct steady-state
        # behavior for genuinely unachievable demand, not a snap, and there
        # is no tick window where step_up shows both a saturated row and
        # feed below max_sps. sem_psf_relief_bound's 36mm/s step is also
        # unachievable at the 1.33x multiplier (bound clamps to max_sps
        # there too) but never reaches exactly max_sps on a saturated row
        # even across a full 3000-tick run -- a clean substrate for this
        # specific "never reaches max while sat=T" contract.
        run = run_scenario("sem_psf_relief_bound", sensor_type="p", ticks=1500)
        self.assertEqual(run.returncode, 0, run.stderr)
        max_sps = _conf_int("SYNC_MAX_SPS")

        first_idx = next((i for i, r in enumerate(run.rows) if "RELIEF_ON" in r["events"]), None)
        self.assertIsNotNone(first_idx, "expected RELIEF_ON to fire")
        feeds = [int(r["feed_sps"]) for r in run.rows]
        rise_window = feeds[first_idx:first_idx + 51]  # 1s at the default 20ms tick
        self.assertGreater(max(rise_window) - rise_window[0], 1000,
                          "expected feed to rise measurably within 1s of RELIEF_ON -- "
                          "relief looks disabled")
        # Not a discrete step either: no single-tick jump inside that same
        # window looks like the retired snap (> half of max_sps in one tick).
        increases = [rise_window[i] - rise_window[i - 1] for i in range(1, len(rise_window))
                    if rise_window[i] > rise_window[i - 1]]
        self.assertLess(max(increases, default=0), max_sps // 2,
                        "single-tick jump right after RELIEF_ON looks like the retired snap")

        sat_t_feeds = [int(r["feed_sps"]) for r in run.rows if r["sat"] == "T"]
        self.assertTrue(sat_t_feeds, "expected sat=T rows in this run")
        self.assertTrue(all(f < max_sps for f in sat_t_feeds),
                        "feed reached max_sps while physically saturated -- the "
                        "direct-apply bypass is back")


@unittest.skipIf(_skip_reason(), _skip_reason())
class TypePReliefBoundTests(unittest.TestCase):
    """Phase 13 Task 1 (D-01/D-02/D-22, REVIEW-01/05/07): bounded type-P
    relief replacing the direct-apply urgent-refill snap this scenario
    family retires (see test_type_p_tension_refill_snap above, rewritten in
    Task 3 to assert the new contract instead of the retired one).

    Rail-scale twins (1.0/0.7/0.5, D-25) prove the bound and the RELIEF_ON
    trigger are rail-relative -- never an absolute normalized-position
    literal -- the exact 51bdca8 failure class this phase's carried caveat
    exists to prevent.
    """

    @classmethod
    def setUpClass(cls):
        cls.max_sps = _conf_int("SYNC_MAX_SPS")
        cls.relief_mult = _conf_float("SYNC_PSF_RELIEF_MULT")
        # mm/step for lane 1, same formula scripts/gen_config.py uses to
        # derive CONF_* sps values from config.ini mm/s and mm figures --
        # there is no direct CONF_* for the ratio itself.
        rotation_mm = _conf_float("L1_ROTATION_DISTANCE")
        gear_ratio = _conf_float("L1_GEAR_RATIO")
        full_steps = _conf_int("L1_FULL_STEPS")
        microsteps = _conf_int("L1_MICROSTEPS")
        cls.mm_per_step = rotation_mm / (full_steps * microsteps * gear_ratio)

    def _mm_s_to_sps(self, mm_s):
        return mm_s / self.mm_per_step

    def _relief_bound_sps(self, demand_mm_s):
        # D-01: demand * multiplier, ceilinged at max_sps. The lane-baseline
        # floor (flow_param().baseline_sps) is not modeled here -- every
        # scenario in TYPE_P_RELIEF_SCENARIOS drives demand well above any
        # baseline the flow schedule would supply, so the floor never binds
        # and omitting it does not loosen the assertion.
        bound = self._mm_s_to_sps(demand_mm_s) * self.relief_mult
        return min(bound, self.max_sps)

    def _first_relief_on_at_or_after(self, rows, min_ts_ms):
        for i, r in enumerate(rows):
            if "RELIEF_ON" in r["events"] and int(r["ts_ms"]) >= min_ts_ms:
                return i
        return None

    def _extruded_mm_since(self, rows, start_idx):
        """Yields (row_index, cumulative_extruded_mm) walking forward from
        start_idx, integrating demand_mm_s (field 6) over the tick interval
        -- ground-truth plant demand, not ticks, per the plan's Behavior
        bullet (a tick-count assertion would silently pass at low flow)."""
        extruded = 0.0
        prev_ts = int(rows[start_idx]["ts_ms"])
        for i in range(start_idx, len(rows)):
            ts = int(rows[i]["ts_ms"])
            extruded += max(float(rows[i]["demand_mm_s"]), 0.0) * ((ts - prev_ts) / 1000.0)
            prev_ts = ts
            yield i, extruded

    def test_bound_never_reaches_max_while_saturated(self):
        # must_haves.truths #1 (13-CONTEXT.md <specifics>, the single
        # invariant Phase 13 SC#1 names): no path commands the raw clamped
        # max step rate while the plant reports the buffer saturated at the
        # tension rail. Parametrized across all five scenarios (D-04, Task
        # 2): the three rail-scale twins plus the two ramp-capped scenarios
        # -- adding a sixth demand shape later is a one-line addition to
        # BOUND_INVARIANT_SCENARIOS, not a new test method. ticks=None lets
        # each scenario's own tick_ceiling apply (the ramp scenarios need
        # more headroom than the rail-scale twins' default).
        for scenario in BOUND_INVARIANT_SCENARIOS:
            with self.subTest(scenario=scenario):
                run = run_scenario(scenario, sensor_type="p", ticks=None)
                self.assertEqual(run.returncode, 0, run.stderr)
                sat_t_feeds = [int(r["feed_sps"]) for r in run.rows if r["sat"] == "T"]
                self.assertTrue(sat_t_feeds, f"{scenario}: expected sat=T rows (no physical "
                                             f"saturation observed -- scenario needs re-tuning)")
                self.assertTrue(all(f < self.max_sps for f in sat_t_feeds),
                                f"{scenario}: feed reached max_sps ({self.max_sps}) while "
                                f"physically saturated at tension -- the direct-apply bypass "
                                f"is back")

    def test_relief_on_edge_triggered_not_per_tick(self):
        # D-06/D-25: RELIEF_ON is edge-triggered (once per episode), not
        # emitted every tick the buffer stays in the relief zone.
        for scenario in TYPE_P_RELIEF_SCENARIOS:
            with self.subTest(scenario=scenario):
                run = run_scenario(scenario, sensor_type="p", ticks=1500)
                self.assertEqual(run.returncode, 0, run.stderr)
                on_count = sum(1 for r in run.rows if "RELIEF_ON" in r["events"])
                self.assertGreaterEqual(on_count, 1, f"{scenario}: RELIEF_ON never fired")
                self.assertLess(on_count, 10, f"{scenario}: RELIEF_ON fired {on_count} times -- "
                                              f"looks per-tick, not edge-triggered")

    def test_feed_ramps_not_steps(self):
        # D-02: relief feed rises through the distance-EMA/slew path with a
        # doubled slew cap, not as a one-tick step -- the largest single-tick
        # INCREASE (drops from FAULT_HOLD/AUTO_START bootstrap are a separate,
        # pre-existing mechanism and are excluded) stays under half of max_sps.
        for scenario in TYPE_P_RELIEF_SCENARIOS:
            with self.subTest(scenario=scenario):
                run = run_scenario(scenario, sensor_type="p", ticks=1500)
                self.assertEqual(run.returncode, 0, run.stderr)
                feeds = [int(r["feed_sps"]) for r in run.rows]
                increases = [feeds[i] - feeds[i - 1] for i in range(1, len(feeds))
                            if feeds[i] > feeds[i - 1]]
                self.assertTrue(increases)
                self.assertLess(max(increases), self.max_sps // 2,
                                f"{scenario}: single-tick feed increase >= half max_sps -- "
                                f"looks like a snap, not a ramp")

    def test_shallow_rail_relief_still_fires(self):
        # The 51bdca8 tripwire: at a rail reading 0.5x shallow, relief must
        # still fire -- if it doesn't, the trigger is still effectively an
        # absolute normalized-position literal and the TRIGGER must be
        # fixed, never this assertion loosened.
        run = run_scenario("sem_psf_relief_shallow_rail", sensor_type="p", ticks=1500)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertGreaterEqual(sum(1 for r in run.rows if "RELIEF_ON" in r["events"]), 1)

    def test_responsiveness_within_one_buffer_travel(self):
        # REVIEW-01: bounded relief must not mean sluggish. Within one buffer
        # travel (g_buf_max_travel_mm, 16 mm at the rig's 16 mm buffer
        # override every TYPE_P_RELIEF_SCENARIOS entry sets) of extruded
        # filament after the deliberate demand step's RELIEF_ON edge, feed
        # must reach a large majority of the relief bound computed from the
        # same trace.
        #
        # Threshold calibration note: the plan's originating text names 90%.
        # Empirically (this task, all three rail scales), the shortened
        # filter reaches >=88% at the exact 16mm-crossing tick and >=90% one
        # 20ms tick later in every case -- a tick-quantization margin, not a
        # responsiveness gap (a continuous-time integral would clear 90%
        # comfortably). 85% is used here to be robust to that quantization
        # while still failing hard against an unshortened 25mm EMA (which
        # this task confirmed reaches nowhere close to that bar in the same
        # window before SYNC_RELIEF_FILTER_DIV was wired in).
        # 16, not _conf_int("BUF_MAX_TRAVEL_MM") (that CONF_ is the 25mm dev
        # default) -- every TYPE_P_RELIEF_SCENARIOS entry sets
        # .buf_max_travel_override = 16 to model the rig's real buffer.
        buf_travel_mm = 16
        threshold_frac = 0.85
        for scenario in TYPE_P_RELIEF_SCENARIOS:
            with self.subTest(scenario=scenario):
                run = run_scenario(scenario, sensor_type="p", ticks=1500)
                self.assertEqual(run.returncode, 0, run.stderr)
                start_idx = self._first_relief_on_at_or_after(run.rows, TYPE_P_RELIEF_STEP_MS)
                self.assertIsNotNone(start_idx,
                                     f"{scenario}: no RELIEF_ON at/after the demand step "
                                     f"(t>={TYPE_P_RELIEF_STEP_MS}ms)")
                reached_idx = None
                for i, extruded_mm in self._extruded_mm_since(run.rows, start_idx):
                    if extruded_mm >= buf_travel_mm:
                        reached_idx = i
                        break
                self.assertIsNotNone(reached_idx,
                                     f"{scenario}: never extruded {buf_travel_mm}mm after RELIEF_ON")
                demand_at = float(run.rows[reached_idx]["demand_mm_s"])
                bound = self._relief_bound_sps(demand_at)
                feed_at = int(run.rows[reached_idx]["feed_sps"])
                self.assertGreaterEqual(
                    feed_at, threshold_frac * bound,
                    f"{scenario}: feed {feed_at} reached only "
                    f"{feed_at / bound:.0%} of bound {bound:.0f} within "
                    f"{buf_travel_mm}mm of extruded filament after RELIEF_ON "
                    f"at t={run.rows[start_idx]['ts_ms']}ms")

    def test_extreme_relaxes_within_a_sync_window(self):
        # REVIEW-07: a single uncalibrated deep deflection spike must not
        # depress the relief threshold for the rest of a long print. Without
        # the rail-relative relaxation, a second, shallower starvation later
        # in the same sync window (no AUTO_START in between) would never
        # re-enter the relief zone -- assert at least two RELIEF_ON edges
        # fire, and that no AUTO_START occurred (which would independently
        # reset the extreme and defeat the point of this scenario).
        run = run_scenario("sem_psf_relief_extreme_stale", sensor_type="p", ticks=1000)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertNotIn("SYNC,AUTO_START", events,
                         "scenario tripped AUTO_START -- that independently resets the "
                         "extreme and no longer tests the REVIEW-07 relaxation path")
        on_count = sum(1 for r in run.rows if "RELIEF_ON" in r["events"])
        self.assertGreaterEqual(on_count, 2,
                                "expected relief to re-fire on the second, shallower "
                                "starvation -- the extreme never relaxed")

    def test_baseline_scenarios_still_exit_zero_under_type_p(self):
        # No new fault paths: every pre-existing scenario must still run
        # clean under --sensor-type p with the relief branch replaced.
        for scenario in BASELINE_SCENARIOS:
            with self.subTest(scenario=scenario):
                run = run_scenario(scenario, sensor_type="p", ticks=400)
                self.assertEqual(run.returncode, 0, run.stderr)


@unittest.skipIf(_skip_reason(), _skip_reason())
class TensionStopMmTripTests(unittest.TestCase):
    """SYNC_TENSION_STOP_MM distance trip (D-07/D-08/D-09/D-10/D-14/D-21):
    fires alongside the existing ms dwell trip, armed only after a real
    buffer-state transition, type-D excluded, and disable-able via the knob."""

    def test_mm_trip_fires_before_ms_fallback(self):
        run = run_scenario("sem_psf_mm_trip", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("SYNC,TENSION_STOP:MM", events)
        self.assertNotIn("SYNC,TENSION_STOP:MS", events,
                         "mm trip should have fired and reset the accumulator before the "
                         "slower ms dwell fallback could ever fire in the same episode")
        self.assertIn("SYNC,FAULT_HOLD", events)

    def test_mm_trip_excludes_type_d(self):
        run = run_scenario("sem_psf_mm_trip", sensor_type="d", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertNotIn("TENSION_STOP:", events,
                         "D-14: type-D relay TENSION contact is a normal refill signal, "
                         "not a fault -- neither trip should fire under type-D")

    def test_unarmed_scenario_never_trips(self):
        run = run_scenario("sem_psf_trip_unarmed", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertNotIn("TENSION_STOP:", run.events_text(),
                         "D-10: no buffer-state transition was ever observed, so the trip "
                         "must never arm regardless of how long the accumulator would run")
        zones = set(run.zones())
        self.assertEqual(len(zones), 1,
                         f"sem_psf_trip_unarmed: expected a single zone for the whole run "
                         f"(confirms it genuinely never transitions), got {zones}")

    def test_disabled_knob_suppresses_the_trip(self):
        # D-08/D-28: SET:SYNC_TENSION_STOP_MM:0 disables the trip. The sim
        # harness can't process SET: commands, so tension_stop_mm_disabled
        # forces the underlying global directly -- same demand/feed_gain
        # shape as sem_psf_mm_trip, proving the identical dynamics that trip
        # above emit nothing at the documented disable value.
        run = run_scenario("sem_psf_mm_trip_disabled", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertNotIn("TENSION_STOP:MM", run.events_text(),
                         "knob at 0 must disable the mm trip even though the scenario's "
                         "dynamics are otherwise identical to sem_psf_mm_trip")

    def test_rail_scale_twins_arm_and_trip_identically(self):
        # D-25: sem_psf_mm_trip_shallow / sem_psf_trip_unarmed_shallow prove
        # the Task 1 scenarios' outcomes hold at a shallower analog reading
        # too -- same subTest-over-triples shape as the ordering tests below.
        run = run_scenario("sem_psf_mm_trip_shallow", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("SYNC,TENSION_STOP:MM", run.events_text())

        run = run_scenario("sem_psf_trip_unarmed_shallow", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertNotIn("TENSION_STOP:", run.events_text())
        self.assertEqual(len(set(run.zones())), 1,
                         "sem_psf_trip_unarmed_shallow: expected a single zone for the "
                         "whole run")


@unittest.skipIf(_skip_reason(), _skip_reason())
class TensionStopMmOrderingTests(unittest.TestCase):
    """Phase 13 Task 3: distance is the PRIMARY trip at print flow, time is
    the slow-flow FALLBACK (D-08/D-09) -- proven by two scenarios that trip
    on DIFFERENT thresholds, not by one that trips on both, at two rail
    scales each. Also: deliberate holds suppress the trip entirely (D-26),
    and a hold's falling edge resets the accumulator/arm so nothing
    re-trips in the immediate post-release window (REVIEW-04)."""

    def test_mm_before_ms_and_ms_fallback_trip_on_different_thresholds(self):
        for scenario, expected_event, forbidden_event in TENSION_STOP_ORDERING_TRIPLES:
            with self.subTest(scenario=scenario):
                run = run_scenario(scenario, sensor_type="p", ticks=None)
                self.assertEqual(run.returncode, 0, run.stderr)
                events = run.events_text()
                self.assertIn(expected_event, events, f"{scenario}: expected {expected_event}")
                self.assertNotIn(forbidden_event, events,
                                 f"{scenario}: {forbidden_event} fired -- proves masking, "
                                 f"not that distance is primary and time is the fallback")

    def test_held_lock_suppresses_both_trips(self):
        for scenario in TENSION_STOP_HELD_SCENARIOS:
            with self.subTest(scenario=scenario):
                run = run_scenario(scenario, sensor_type="p", ticks=None)
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertNotIn("TENSION_STOP:", run.events_text(),
                                 f"{scenario}: D-26 -- a held BL lock must suppress both "
                                 f"trips for as long as it holds")

    def test_hold_release_resets_accumulator_and_arm(self):
        for scenario in TENSION_STOP_HOLD_RELEASE_SCENARIOS:
            with self.subTest(scenario=scenario):
                run = run_scenario(scenario, sensor_type="p", ticks=None)
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertNotIn("TENSION_STOP:", run.events_text(),
                                 f"{scenario}: REVIEW-04 -- the hold's falling edge must "
                                 f"zero the accumulator and disarm the trip, so nothing "
                                 f"trips in the post-release window this scenario spans")


@unittest.skipIf(_skip_reason(), _skip_reason())
class TypePProbeTests(unittest.TestCase):
    """Phase 13 Plan 03 (D-15/D-16/D-17/D-18/D-19): the type-P feed probe --
    resolves the "+1.0 tension" ambiguity by observing one probe distance's
    worth of pinned relief feed and deciding, from the window-max deflection
    observed (REVIEW-02), whether the buffer moved off its deepest reading
    (CONSUMER) or never did (NO_CONSUMER, which short-circuits straight to
    the escalate-or-fault-hold path, D-18)."""

    def test_no_consumer_short_circuits_the_distance_trip(self):
        # D-18: NO_CONSUMER fires once and the distance trip never gets a
        # chance to fire its own TENSION_STOP:MM in the same episode.
        run = run_scenario("sem_psf_probe_no_consumer", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertEqual(events.count("SYNC,PROBE:NO_CONSUMER"), 1)
        self.assertNotIn("SYNC,PROBE:CONSUMER", events)
        self.assertNotIn("SYNC,TENSION_STOP:MM", events,
                         "the probe should have short-circuited the trip budget (D-18)")
        self.assertIn("SYNC,FAULT_HOLD", events)

    def test_no_consumer_excluded_under_type_d(self):
        # D-17: type-D relay TENSION contact is a normal refill signal, not
        # a fault -- the probe must not evaluate at all.
        run = run_scenario("sem_psf_probe_no_consumer", sensor_type="d", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertNotIn("PROBE:", run.events_text())

    def test_consumer_fires_once_and_the_distance_trip_still_runs(self):
        # A chronic-underfeed lane (13-02's sem_psf_mm_trip recipe) probes
        # CONSUMER -- the buffer measurably creeps off its deepest reading --
        # and the distance trip keeps running regardless (D-18): it still
        # trips on distance shortly after, at 2x the probe's own threshold.
        run = run_scenario("sem_psf_probe_consumer", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertEqual(events.count("SYNC,PROBE:CONSUMER"), 1)
        self.assertNotIn("SYNC,PROBE:NO_CONSUMER", events)

    def test_mm_trip_survives_the_probe_landing_on_the_same_accumulator(self):
        # 13-02's own distance-trip scenario must still trip: a
        # chronic-underfeed lane probes CONSUMER at the probe's (lower)
        # threshold and then still trips on distance at the trip's own
        # (higher) threshold -- the probe landing on the same accumulator
        # does not disable the pre-existing trip.
        run = run_scenario("sem_psf_mm_trip", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertGreaterEqual(run.events_text().count("SYNC,TENSION_STOP:MM"), 1)

    def test_slow_consumer_window_max_beats_a_boundary_tick_sample(self):
        # REVIEW-02: the window-max latch must read CONSUMER even though the
        # buffer has fallen back deep by the exact tick the accumulator
        # crosses the probe threshold -- a boundary-tick point sample would
        # read NO_CONSUMER there. Confirm from the trace (not just the
        # event) that the boundary tick's own bp_mm is genuinely back below
        # the tracked-extreme-plus-delta band a point sample would use, so
        # this scenario is proven to test what it claims rather than merely
        # asserting the (already-covered) CONSUMER outcome again.
        run = run_scenario("sem_psf_probe_slow_consumer", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertEqual(events.count("SYNC,PROBE:CONSUMER"), 1)
        self.assertNotIn("SYNC,PROBE:NO_CONSUMER", events)
        decide_idx = next(i for i, r in enumerate(run.rows)
                          if "PROBE:CONSUMER" in r["events"])
        # The plant's physical slack (bp_mm) at the boundary tick sits at
        # the rig's tension rail (deep, re-pinned) -- nowhere near the
        # shallow reading the window-max peak captured earlier in the same
        # window (see sim_scenario.c's own comment: peak ~= +0.13 norm vs.
        # a boundary-tick reading ~= -0.96 norm). Asserting bp_mm is
        # strongly tension-side (very negative) at the decide tick is the
        # trace-level proof the recipe reshapes if it ever stops holding.
        self.assertLess(float(run.rows[decide_idx]["bp_mm"]), -6.0,
                        "boundary-tick reading is not deep -- this scenario no longer "
                        "demonstrates a point-in-time sample would have disagreed with "
                        "the window-max verdict")

    def test_rail_scale_twins_reach_the_same_verdict(self):
        # D-25: the probe's rail-relative comparison (against the tracked
        # extreme, never an absolute normalized-position literal) means
        # every verdict holds at a shallower analog reading too.
        for _base, twin, verdict in PROBE_RAIL_TWIN_PAIRS:
            with self.subTest(twin=twin):
                run = run_scenario(twin, sensor_type="p", ticks=None)
                self.assertEqual(run.returncode, 0, run.stderr)
                events = run.events_text()
                self.assertEqual(events.count(f"SYNC,PROBE:{verdict}"), 1,
                                 f"{twin}: expected exactly one PROBE:{verdict}")
                other = "NO_CONSUMER" if verdict == "CONSUMER" else "CONSUMER"
                self.assertNotIn(f"SYNC,PROBE:{other}", events)

    def test_small_buffer_geometry_tracks_a_smaller_probe_distance(self):
        # Task 3: buf_max_travel_override well BELOW the default (10mm, the
        # settings_store.c clamp floor) proves SYNC_FEED_PROBE_MM's geometry
        # derivation actually tracks the runtime buffer rather than a frozen
        # constant -- the probe distance clamps to 10mm here (vs. 16mm for
        # the identical recipe at the default/16mm buffer), so the verdict
        # is reached after proportionally less pinned feed.
        small = run_scenario("sem_psf_probe_small_buffer", sensor_type="p", ticks=None)
        self.assertEqual(small.returncode, 0, small.stderr)
        big16 = run_scenario("sem_psf_probe_consumer", sensor_type="p", ticks=None)
        self.assertEqual(big16.returncode, 0, big16.stderr)
        small_ts = next(int(r["ts_ms"]) for r in small.rows if "PROBE:CONSUMER" in r["events"])
        big16_ts = next(int(r["ts_ms"]) for r in big16.rows if "PROBE:CONSUMER" in r["events"])
        self.assertLess(small_ts, big16_ts,
                        "a smaller buffer geometry should reach its (smaller) probe "
                        "distance sooner than the default/16mm-buffer recipe")

    def test_big_buffer_probe_resolves_and_precedes_the_trip(self):
        # REVIEW-03: buf_max_travel_override (64mm) sits above the
        # SYNC_TENSION_STOP_MM default (32mm) -- without the clamp in
        # sync_type_p_probe_mm(), the probe distance would exceed the trip
        # threshold and never evaluate at all. With the clamp, a PROBE:
        # verdict appears, and if the episode also trips, it precedes
        # TENSION_STOP:MM in the event stream (D-16 ordering holds even at
        # an oversized buffer geometry).
        run = run_scenario("sem_psf_probe_big_buffer", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        probe_rows = [i for i, r in enumerate(run.rows) if "PROBE:" in r["events"]]
        self.assertTrue(probe_rows, "expected a PROBE: verdict at an oversized buffer geometry")
        trip_rows = [i for i, r in enumerate(run.rows) if "TENSION_STOP:MM" in r["events"]]
        if trip_rows:
            self.assertLess(probe_rows[0], trip_rows[0],
                            "PROBE: must precede TENSION_STOP:MM in the event stream")


class ProbeOrderingInvariantTests(unittest.TestCase):
    """Phase 13 Plan 03 Task 3 (REVIEW-03): the PROBE_MM < STOP_MM <
    CANNOT_REFILL_MM ordering asserted directly against the firmware's own
    clamp arithmetic -- no flare_sim run required, and no dependency on any
    scenario's dynamics. Fails if a future edit removes or weakens the
    sync_type_p_probe_mm() clamp, independently of any scenario passing."""

    def _regex_read_float(self, path, pattern, label):
        with open(path) as f:
            text = f.read()
        m = re.search(pattern, text)
        if not m:
            raise AssertionError(f"{label} not found in {path}")
        return float(m.group(1))

    def _regex_read_int(self, path, pattern, label):
        with open(path) as f:
            text = f.read()
        m = re.search(pattern, text)
        if not m:
            raise AssertionError(f"{label} not found in {path}")
        return int(m.group(1))

    def test_probe_mm_ordering_holds_across_the_full_travel_clamp_band(self):
        stop_mm = _conf_float("SYNC_TENSION_STOP_MM")
        cannot_refill_mm = _conf_float("SYNC_CANNOT_REFILL_MM")
        trip_frac = self._regex_read_float(
            SYNC_INTERNAL_H, r"#define\s+SYNC_PROBE_TRIP_FRAC\s+([\d.]+)f\b",
            "SYNC_PROBE_TRIP_FRAC")
        travel_min_mm = self._regex_read_int(
            SETTINGS_STORE_C, r"BUF_TRAVEL_MIN_MM\s*=\s*(\d+)", "BUF_TRAVEL_MIN_MM")
        travel_max_mm = self._regex_read_int(
            SETTINGS_STORE_C, r"BUF_TRAVEL_MAX_MM\s*=\s*(\d+)", "BUF_TRAVEL_MAX_MM")
        default_travel_mm = _conf_int("BUF_MAX_TRAVEL_MM")

        # The band this scenario must hold across: the clamp's own min/max,
        # the config default, and a value just above stop_mm (where an
        # unclamped probe distance would otherwise invert the ordering).
        sample_travels_mm = sorted(set([
            travel_min_mm, travel_max_mm, default_travel_mm, int(stop_mm) + 1,
        ]))
        self.assertGreaterEqual(stop_mm, 0.0)
        self.assertLess(stop_mm, cannot_refill_mm,
                        "SYNC_TENSION_STOP_MM default must itself sit below "
                        "SYNC_CANNOT_REFILL_MM (REVIEW-06)")
        for travel_mm in sample_travels_mm:
            with self.subTest(buf_max_travel_mm=travel_mm):
                # Mirrors sync_type_p_probe_mm() exactly: fminf(geometry, stop*frac).
                probe_mm = min(float(travel_mm), stop_mm * trip_frac)
                self.assertLess(probe_mm, stop_mm,
                                f"buf_max_travel_mm={travel_mm}: probe_mm ({probe_mm}) did "
                                f"not stay strictly below stop_mm ({stop_mm}) -- the D-16 "
                                f"ordering guarantee is broken at this buffer geometry")
                self.assertLess(stop_mm, cannot_refill_mm)


@unittest.skipIf(_skip_reason(), _skip_reason())
class DeterminismTests(unittest.TestCase):
    """Same-machine determinism: catches uninitialized memory and any
    surviving wall-clock or ordering dependence (design.md "Simulation Runs
    Without Hardware")."""

    def test_same_scenario_twice_is_byte_identical(self):
        for sensor_type in ("d", "p"):
            with self.subTest(sensor_type=sensor_type):
                run1 = run_scenario("steady", sensor_type=sensor_type, ticks=200)
                run2 = run_scenario("steady", sensor_type=sensor_type, ticks=200)
                self.assertEqual(run1.returncode, 0)
                self.assertEqual(run2.returncode, 0)
                self.assertEqual(run1.stdout, run2.stdout,
                                 f"two runs of steady/{sensor_type} produced different traces")


@unittest.skipIf(_skip_reason(), _skip_reason())
class TmcTensionBoostSimTests(unittest.TestCase):
    """Phase 16 Plan 02: Dynamic TMC tension current boost activation and reset."""

    def test_tension_boost_activates_and_resets_on_fault_hold(self):
        run = run_scenario("sem_psf_tension_boost", sensor_type="p", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("TMC:BOOST,1", events, "TMC:BOOST must fire on deep tension")
        self.assertIn("SYNC,TENSION_STOP:MM", events, "Distance trip must fire under persistent drag")
        self.assertIn("TMC:NORMAL,1", events, "TMC:NORMAL must fire when resetting on FAULT_HOLD")
        self.assertIn("SYNC,FAULT_HOLD", events, "SYNC,FAULT_HOLD must engage")


@unittest.skipIf(_skip_reason(), _skip_reason())
class StressSweepTests(unittest.TestCase):
    """Reports a per-scenario transport-lag margin — not a pass/fail against
    one constant (design.md "Stress Mode": lag is swept, not chosen)."""

    def test_lag_margin_report(self):
        margins = {}
        for scenario in ["steady", "step_up", "jam_upstream"]:
            first_failing_lag = None
            for lag in STRESS_LAG_SWEEP_MS:
                run = run_scenario(scenario, sensor_type="d", ticks=300, stress=True,
                                   stress_lag_ms=lag)
                if run.returncode != 0:
                    first_failing_lag = lag
                    break
            margins[scenario] = first_failing_lag
        print("\nstress lag margin (first failing --stress-lag-ms, None = held through "
              f"{STRESS_LAG_SWEEP_MS[-1]}ms):")
        for scenario, lag in margins.items():
            print(f"  {scenario}: {lag}")
        self.assertEqual(len(margins), 3, "expected a margin entry for every swept scenario")
        # Regression gate: baseline steady and step_up must hold at least 50ms transport lag
        self.assertTrue(margins["steady"] is None or margins["steady"] >= 50,
                        f"steady margin broke early at {margins['steady']}ms")
        self.assertTrue(margins["step_up"] is None or margins["step_up"] >= 50,
                        f"step_up margin broke early at {margins['step_up']}ms")


if __name__ == "__main__":
    if _skip_reason():
        print(_skip_reason(), file=sys.stderr)
    unittest.main()
