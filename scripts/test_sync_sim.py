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
RELOAD_SCENARIOS = ["reload_genuine_runout_escalation",
                   "reload_idle_consumer_staged_completion", "reload_already_loaded_noop"]

ALL_DUAL_TYPE_SCENARIOS = BASELINE_SCENARIOS + FAULT_SCENARIOS + SWITCH_SCENARIOS

STRESS_LAG_SWEEP_MS = [0, 20, 50, 100, 200]


def _skip_reason():
    if not os.path.isfile(SIM_BINARY):
        return (f"{SIM_BINARY} not built — run "
                f"'cmake -S tests/host -B build_sim && ninja -C build_sim' first")
    return None


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

    Spec/code mismatch found while building these (see
    memories/repo/host-sync-sim.md): the spec's "Extruder retract breaks the
    lock" scenario says `EV:BL:BREAK` is emitted; the real firmware emits
    `EV:BL:FOLLOW` (sync.c has no BL:BREAK event anywhere) — asserted against
    the real event name here, not the spec's stated one."""

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
        self.assertIn("BL,FOLLOW", events)  # the real event — see class docstring

    def test_watchdog_auto_releases_after_default_timeout(self):
        run = run_scenario("sem_bl_watchdog_timeout", sensor_type="d", ticks=None)
        self.assertEqual(run.returncode, 0, run.stderr)
        events = run.events_text()
        self.assertIn("!BL,TIMEOUT", events)  # cmd_event_critical -> "!" prefix in the trace
        self.assertEqual(run.rows[-1]["sync_state"], "OFF")


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
        self.assertTrue(all(f <= 15004 for f in tension_feeds))

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


@unittest.skipIf(_skip_reason(), _skip_reason())
class DeterminismTests(unittest.TestCase):
    """Same-machine determinism: catches uninitialized memory and any
    surviving wall-clock or ordering dependence (design.md "Simulation Runs
    Without Hardware")."""

    def test_same_scenario_twice_is_byte_identical(self):
        run1 = run_scenario("steady", sensor_type="d", ticks=200)
        run2 = run_scenario("steady", sensor_type="d", ticks=200)
        self.assertEqual(run1.returncode, 0)
        self.assertEqual(run2.returncode, 0)
        self.assertEqual(run1.stdout, run2.stdout,
                         "two runs of the same scenario produced different traces")


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


if __name__ == "__main__":
    if _skip_reason():
        print(_skip_reason(), file=sys.stderr)
    unittest.main()
