#include "sim_scenario.h"
#include <stddef.h>

float demand_profile_eval(const demand_profile_t *p, uint32_t t_ms) {
    switch (p->kind) {
    case DEMAND_STEADY:
        return p->level_mm_s;
    case DEMAND_STEP_UP:
        return (t_ms < p->t1_ms) ? p->level_mm_s : p->level2_mm_s;
    case DEMAND_BURST:
        return (t_ms >= p->t1_ms && t_ms < p->t2_ms) ? p->level_mm_s : 0.0f;
    case DEMAND_IDLE_ZERO:
        return (t_ms < p->t1_ms) ? p->level_mm_s : 0.0f;
    case DEMAND_RETRACT:
    case DEMAND_LONG_RETRACT:
        return (t_ms >= p->t1_ms && t_ms < p->t2_ms) ? -p->level_mm_s : 0.0f;
    case DEMAND_PAUSE_RESUME:
        if (t_ms < p->t1_ms)
            return p->level_mm_s;
        if (t_ms < p->t2_ms)
            return 0.0f;
        return p->level2_mm_s;
    }
    return 0.0f;
}

float gain_schedule_eval(const gain_schedule_t *s, uint32_t t_ms) {
    float v = 1.0f; // no fault until a breakpoint fires
    for (int i = 0; i < s->count; i++) {
        if (s->bp[i].t_ms <= t_ms)
            v = s->bp[i].value;
    }
    return v;
}

// clang-format off
const sim_scenario_t g_sim_scenarios[] = {
    {
        .name = "steady",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 20.0f},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "step_up",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 40.0f, .t1_ms = 5000},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "burst",
        .demand = {.kind = DEMAND_BURST, .level_mm_s = 35.0f, .t1_ms = 2000, .t2_ms = 4000},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "idle_zero",
        .demand = {.kind = DEMAND_IDLE_ZERO, .level_mm_s = 25.0f, .t1_ms = 3000},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "retract",
        .demand = {.kind = DEMAND_RETRACT, .level_mm_s = 15.0f, .t1_ms = 2000, .t2_ms = 2500},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "long_retract",
        .demand = {.kind = DEMAND_LONG_RETRACT, .level_mm_s = 200.0f, .t1_ms = 2000, .t2_ms = 3000},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "jam_upstream",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 20.0f},
        .feed_gain = {.bp = {{0, 1.0f}, {3000, 0.0f}}, .count = 2},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "grind_slip",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 20.0f},
        .demand_gain = {.bp = {{0, 1.0f}, {3000, 0.0f}}, .count = 2},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "underextrusion",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 20.0f},
        .demand_gain = {.bp = {{0, 1.0f}, {2000, 0.5f}}, .count = 2},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "retract_stuck",
        .demand = {.kind = DEMAND_RETRACT, .level_mm_s = 15.0f, .t1_ms = 2000, .t2_ms = 2500},
        .retract_gain = {.bp = {{0, 1.0f}, {2000, 0.0f}}, .count = 2},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "sensor_chatter",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 10.0f},
        .sensor_force = {.ev = {{2000, FORCE_CHATTER, SENSOR_TARGET_TENSION, true}}, .count = 1},
        .active_lane = 1, .start_sync_active = true, .type_specific = true,
    },
    {
        .name = "sensor_stuck",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 10.0f},
        .sensor_force = {.ev = {{2000, FORCE_STUCK, SENSOR_TARGET_TENSION, true}}, .count = 1},
        .active_lane = 1, .start_sync_active = true, .type_specific = true,
    },
    {
        .name = "both_switches_fault",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 10.0f},
        .sensor_force = {.ev = {{2000, FORCE_BOTH, SENSOR_TARGET_TENSION, true}}, .count = 1},
        .active_lane = 1, .start_sync_active = true, .type_specific = true,
    },
    {
        .name = "runout",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 20.0f},
        .switch_script = {.ev = {{3000, SWITCH_L1_OUT, false}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
    },
    {
        .name = "y_splitter_toggle",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 10.0f},
        .switch_script = {.ev = {{2000, SWITCH_Y, true}, {2500, SWITCH_Y, false}}, .count = 2},
        .active_lane = 1, .start_sync_active = true,
    },
    // audit-reliability-fixes H4/H5/H6 sim coverage. Type-P only: the H6
    // escalation path (sync.c sync_check_tension_dwell_and_ramp) is gated
    // `g_buf_sensor_type != BUF_SENSOR_TYPE_D` — type-D's relay catch-up
    // handles tension dwell differently, unchanged by H6.
    {
        .name = "reload_genuine_runout_escalation", // H6
        // Demand kept low (1 mm/s): the plant's tension-rail saturation
        // wall-timeout (CONF_PSF_WALL_SAT_MS = 1000 ms, sync.c ~line 1254) is
        // a separate, faster fault-hold path than H6's 6 s tension-DWELL
        // escalation. A high-demand full jam saturates the rail and trips
        // the wall-timeout before the dwell timer ever completes — a real
        // race, not a sim artifact — so this scenario needs enough travel
        // time from the TENSION-zone crossing to the rail for the dwell
        // clock to win. Found by running this scenario against real sync.c
        // and observing which fault-hold path actually fired.
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 1.0f},
        .feed_gain = {.bp = {{0, 1.0f}, {5000, 0.0f}}, .count = 2},
        .switch_script = {.ev = {{5000, SWITCH_L1_IN, false}, {5000, SWITCH_L1_OUT, false}},
                          .count = 2},
        .active_lane = 1, .start_sync_active = true, .reload_mode = true,
        // g_extruder_est_sps is stale from before the runout (sync_tick, the
        // only thing that updates it, is suppressed for the whole RELOAD
        // sequence) — without this, tc_reload_consumer_active() reads the
        // pre-runout estimate as "consumer active" and the follow phase
        // chases tension instead of completing on staged compression,
        // tripping an unrelated FOLLOW_JAM that has nothing to do with H6.
        // H6 is specifically about the escalation path, not follow
        // completion (that's H4) — isolate it.
        .force_no_consumer = true,
        .tick_ceiling = 4000,
        .tick_ceiling_reason = "dwell(6s) + join-delay(10s) + approach/follow need "
                               "headroom beyond the 60s default",
    },
    {
        .name = "reload_idle_consumer_staged_completion", // H4
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 1.0f},
        .feed_gain = {.bp = {{0, 1.0f}, {5000, 0.0f}}, .count = 2},
        .switch_script = {.ev = {{5000, SWITCH_L1_IN, false}, {5000, SWITCH_L1_OUT, false}},
                          .count = 2},
        .active_lane = 1, .start_sync_active = true, .reload_mode = true,
        .force_no_consumer = true,
        .tick_ceiling = 4000,
        .tick_ceiling_reason = "same headroom as reload_genuine_runout_escalation, "
                               "plus follow-phase settle time",
    },
    {
        .name = "reload_already_loaded_noop", // H5
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 10.0f},
        .active_lane = 1, .start_sync_active = true, .reload_mode = true,
        .manual_reload_at_ms = 2000,
    },
    // openspec/specs/sync-state-model scenarios. Type-P only: both mechanisms
    // below key off g_buf_pos / TYPE_P_RAIL_NORM, an analog-only concept.
    {
        // Spec scenario "Enter relief pause without losing state" + "Resume
        // on TENSION re-arm". Demand cuts to 0 (print pauses) while sync is
        // still driving feed toward the prior demand level, overfeeding the
        // buffer into the compression rail; CONF_PSF_WALL_SAT_MS (1000 ms)
        // after saturating, sync.c enters SYNC_RELIEF_PAUSE. Demand then
        // resumes (print continues), draining the buffer back off the rail;
        // once g_buf_pos drops back past TYPE_P_AUTO_START_POS_NORM, sync.c's
        // relief_rearm check should return the controller to SYNC_ACTIVE.
        .name = "sem_relief_pause_lifecycle",
        .demand = {.kind = DEMAND_PAUSE_RESUME, .level_mm_s = 20.0f, .level2_mm_s = 15.0f,
                  .t1_ms = 3000, .t2_ms = 8000},
        .active_lane = 1, .start_sync_active = true,
        .tick_ceiling = 2000,
        .tick_ceiling_reason = "needs headroom past the resume at t2=8s for re-arm to settle",
    },
    {
        // Spec scenario "Standalone recovery": "WHEN SYNC_FAULT_HOLD has been
        // stable for the configured recovery interval THEN the controller
        // recovers conservatively without any host command." Confirmed: with
        // feed_gain permanently 0 (a persistent, un-clearing jam — g_reload_mode
        // is false here, so this isn't the H6 escalation path), FAULT_HOLD is
        // entered once the tension rail has been saturated for
        // CONF_PSF_WALL_SAT_MS (1000 ms), and SYNC,FAULT_HOLD_RECOVERY fires
        // exactly CONF_SYNC_FAULT_HOLD_RECOVERY_MS (5000 ms) after entry, on
        // schedule, with no host command — spec satisfied. Because the plant's
        // feed_gain=0 fault is permanent, the recovered feed can't physically
        // move the (crudely modeled) buffer, so it re-saturates and re-enters
        // FAULT_HOLD within ~1.5 s, repeating the entry/recover/re-enter cycle
        // indefinitely — expected for a genuinely unrecoverable jam, not a
        // defect. Keep tick_ceiling short enough that invariant 6 (saturation
        // bound, 20 s) doesn't fire on this deliberately-permanent-fault
        // scenario — it isn't a generically valid check here, since the fault
        // never clears by construction.
        .name = "sem_fault_hold_standalone_recovery",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 1.0f},
        .feed_gain = {.bp = {{0, 1.0f}, {5000, 0.0f}}, .count = 2},
        .active_lane = 1, .start_sync_active = true,
        .tick_ceiling = 1500,
        .tick_ceiling_reason = "needs 1s(wall-sat) + 5s(recovery interval) + margin past "
                               "the t=5000 jam onset, short enough to stay under "
                               "invariant 6's 20s saturation-bound window",
    },
    // openspec/specs/buffer-state-lock scenarios. Type-D (the spec's stated
    // purpose: "drive type-D buffers to tension or compression"). BL prime/
    // lock/catch drives motor_set_rate_sps()/motor_set_dir() directly against
    // lane->m, never touching lane->task/current_sps/task_forward — the plant
    // picks this up via sim_motor_rate_sps()/gpio_get(dir_pin) (see
    // sim_plant.c and memories/repo/host-sync-sim.md). BL:<state> host-command
    // framing (OK ack, ER:BUSY rejection while a task is running) is
    // protocol.c-level and out of sim scope; these call sync_buffer_lock_arm()/
    // sync_retract_assist_set() directly, same pattern as manual_reload_at_ms.
    {
        // Spec scenarios "Host arms tension lock" (effect only, not the OK ack)
        // + "Prime hits target switch first" + "Lock holds against buffer
        // spring"/"Lock preserves controller learning" (implicit: zero net
        // feed while locked) + "Extruder retract breaks the lock" + "Tension-
        // armed catch slams retract". follow_mm must exceed half travel
        // (12.5 mm default) for the follow-on catch path (BL_FOLLOW) to arm at
        // all — with follow_mm=0 sync_buffer_lock_locked() never even checks
        // for a break (see sync.c:894, gated on `g_bl_follow_mm > 0.0f`).
        .name = "sem_bl_lock_catch",
        .demand = {.kind = DEMAND_RETRACT, .level_mm_s = 15.0f, .t1_ms = 6000, .t2_ms = 9000},
        .active_lane = 1, .start_sync_active = false,
        .bl_arm_at_ms = 1000, .bl_arm_target = 1 /* BUF_TENSION */,
        .bl_arm_follow_mm = 20.0f, .bl_arm_follow_rate_mmpm = 300.0f,
        .tick_ceiling = 700,
        .tick_ceiling_reason = "prime+lock+break+catch all settle well under the 60s default; "
                               "kept short so a stuck lock fails fast",
    },
    {
        // Spec scenario "Operator aborts lock": BS releases the lock/catch
        // and returns to SYNC_OFF.
        .name = "sem_bl_release_via_bs",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .active_lane = 1, .start_sync_active = false,
        .bl_arm_at_ms = 1000, .bl_arm_target = 1 /* BUF_TENSION */,
        .bl_clear_at_ms = 4000,
        .tick_ceiling = 400,
    },
    {
        // Spec scenario "Misordered macro leaves lock armed": auto-releases
        // with EV:BL:TIMEOUT after BL_WATCHDOG_DEFAULT_MS (30s) with no
        // break and no BS. No follow_mm, so no break-check runs — isolates
        // the watchdog path specifically.
        .name = "sem_bl_watchdog_timeout",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .active_lane = 1, .start_sync_active = false,
        .bl_arm_at_ms = 1000, .bl_arm_target = 1 /* BUF_TENSION */,
        .tick_ceiling = 1650,
        .tick_ceiling_reason = "lock achieved quickly, then needs the full 30s watchdog "
                               "window plus margin (below SIM_LIVENESS_BACKSTOP_MS=45s)",
    },
    // openspec/specs/cutter-feed-timeout scenarios. cutter_start()/cutter_tick()
    // are purely time-driven state machines (CUT_FEED_WAIT/CUT_OPEN_WAIT etc.
    // compare elapsed ms against configured timeouts) — no plant/buffer
    // interaction needed. Feed motion itself goes through the same motor-
    // level fallback as BL (cut_begin_feed() drives lane->m directly without
    // setting lane->task), already covered by sim_plant.c. GET:/SET: protocol
    // exposure scenarios are protocol.c-level, out of sim scope.
    {
        // "Large feed completes without abort": default CONF_CUT_FEED_MM=150,
        // CONF_CUT_FEED_SPS=10230, CONF_L1_MM_PER_STEP=0.0024437 already gives
        // feed_initial_ms ~= 150 / (10230*0.0024437) * 1000 ~= 6000ms > 5000ms,
        // comfortably under the default 30s FLARE_INT_CUT_TIMEOUT_FEED_MS —
        // no override needed.
        .name = "sem_cutter_large_feed_completes",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .active_lane = 1, .start_sync_active = false,
        .cutter_start_at_ms = 500, .cutter_enable_feed = true,
        .tick_ceiling = 600,
    },
    {
        // "Timeout still fires on genuine jam": same ~6s feed, but
        // cut_timeout_feed_ms overridden below it (3000ms) so CUT_FEED_WAIT
        // must abort rather than silently completing.
        .name = "sem_cutter_feed_timeout_jam",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .active_lane = 1, .start_sync_active = false,
        .cutter_start_at_ms = 500, .cutter_enable_feed = true,
        .cut_timeout_feed_ms_override = 3000,
        .tick_ceiling = 600,
    },
    {
        // "Settle timeout exceeds SERVO_SETTLE_MS": defaults already satisfy
        // this (500ms settle < 5000ms timeout) — full open/close/reopen/done
        // cycle should complete with no abort.
        .name = "sem_cutter_settle_completes",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .active_lane = 1, .start_sync_active = false,
        .cutter_start_at_ms = 500, .cutter_enable_feed = false,
        .tick_ceiling = 600,
    },
    {
        // "Abort fires when servo hangs": servo_settle_ms overridden above
        // the settle timeout (6000ms > default 5000ms) so the "settled"
        // success check never fires before the timeout does.
        .name = "sem_cutter_settle_timeout_abort",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .active_lane = 1, .start_sync_active = false,
        .cutter_start_at_ms = 500, .cutter_enable_feed = false,
        .servo_settle_ms_override = 6000,
        .tick_ceiling = 600,
    },
    // openspec/specs/motion-safety probe. "Filament Lost Mid-Task": TASK_FEED,
    // IN clears, buffer not BUF_TENSION, persists > 8s -> FAULT:DRY_SPIN.
    // demand=0 (idle/paused print) so the buffer doesn't drift toward TENSION
    // (which would legitimately suppress dry-spin per its own condition) and
    // sync's own commanded feed trends toward ~0, keeping lane_tail_in_transit()
    // true long enough that motion.c's OTHER runout path (lane_tick_feed_autoload,
    // ~1s debounce) doesn't win the race and stop the lane (task->IDLE) before
    // dry-spin's 8s timer can complete.
    {
        .name = "sem_motion_dry_spin_probe",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 20.0f},
        .switch_script = {.ev = {{3000, SWITCH_L1_IN, false}, {3000, SWITCH_L1_OUT, false}},
                          .count = 2},
        .active_lane = 1, .start_sync_active = true,
        .tick_ceiling = 800,
        .tick_ceiling_reason = "let steady-state settle (~3s) before clearing switches, "
                               "then 8s dry-spin timeout + margin",
    },
    // openspec/specs/relay-fallback-only probe. "NEUTRAL always uses the
    // fallback" + "Catch-up and stop branches preserved" — sync_relay.c's
    // relay_control_law(). Steady demand keeps the type-D relay oscillating
    // through NEUTRAL/TENSION/COMPRESSION, exercising all three branches.
    {
        .name = "sem_relay_fallback_probe",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 20.0f},
        .active_lane = 1, .start_sync_active = true,
        .tick_ceiling = 400,
    },
    // openspec/specs/persistence-contract "Fresh Board" scenario.
    {
        .name = "sem_persistence_fresh_board",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .active_lane = 1, .start_sync_active = false,
        .test_settings_load_fresh_board = true,
        .tick_ceiling = 10,
    },
    // openspec/specs/psf-type-p-sensor "Type-P Stabilize Rail Breakaway".
    // DEMAND_IDLE_ZERO with sync inactive pins the buffer at the tension rail
    // by t=1500 (no motor feed to counter steady consumption); BS at t=2000
    // (buffer already saturated) exercises the rail-break path: stagnation
    // guard must NOT abort on the short-window position-change test while
    // saturated, only past PSF_STAB_RAIL_BREAK_MS (3000ms) from stabilize
    // start. Here feed_gain stays 1.0 so the stab motor's own drive actually
    // moves the plant and the buffer breaks off before the cap -> BUF_STAB:DONE.
    {
        .name = "sem_psf_stab_rail_breakaway",
        .demand = {.kind = DEMAND_IDLE_ZERO, .level_mm_s = 25.0f, .t1_ms = 1500},
        .active_lane = 1, .start_sync_active = false,
        .bs_request_at_ms = 2000,
        .tick_ceiling = 500,
        .tick_ceiling_reason = "rail-break cap is 3000ms from BS at t=2000; 500 ticks "
                               "(10s) covers breakaway + settle to goal with margin",
    },
    // Same setup, but feed_gain zeroed at the BS trigger tick models an
    // uncoupled/jammed lane: the stab motor commands a rate but the plant
    // never moves, so the buffer stays pinned saturated past
    // PSF_STAB_RAIL_BREAK_MS -> BUF_STAB:STAGNANT_TIMEOUT, not the 10s boot
    // deadline.
    {
        .name = "sem_psf_stab_rail_break_timeout",
        .demand = {.kind = DEMAND_IDLE_ZERO, .level_mm_s = 25.0f, .t1_ms = 1500},
        .feed_gain = {.bp = {{0, 1.0f}, {2000, 0.0f}}, .count = 2},
        .active_lane = 1, .start_sync_active = false,
        .bs_request_at_ms = 2000,
        .tick_ceiling = 300,
        .tick_ceiling_reason = "PSF_STAB_RAIL_BREAK_MS (3000ms) from BS at t=2000 is the "
                               "abort deadline at t=5000; 300 ticks (6s) covers it with margin",
    },
    // openspec/specs/psf-type-p-sensor "Type-P Unload Uses No Position-Based
    // Over-Tension Guard". OUT starts present (sim boot default, both
    // sensors true) and clears 1000ms into the retract (mirrors the
    // `runout` scenario's proven switch_script pattern) -> falls through to
    // the deadline-tracked completion path (motion.c:356-364/391-406),
    // never engaging the sensor-latch/buf-recover-jog branches -- exercises
    // "normal retract proceeds to UNLOADED without any relief jog".
    {
        .name = "sem_psf_unload_normal",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .switch_script = {.ev = {{2000, SWITCH_L1_OUT, false}}, .count = 1},
        .active_lane = 1, .start_sync_active = false,
        .ul_start_at_ms = 1000, .ul_target_lane = 1,
        .tick_ceiling = 400,
    },
    // OUT forced present for the whole run (stuck jam): keeps the unload
    // state machine in its OUT-present loop (motion.c:345, a no-op branch)
    // long enough for the reverse-retract motor motion to pull the type-P
    // buffer to and hold at the tension rail -- the exact "extruder gripping"
    // condition the spec's type-P finding says is indistinguishable from a
    // real jam by position alone. Lane 2's OUT must be forced false: the
    // tension-block guard explicitly skips itself when BOTH lanes' OUT are
    // present (double-load recovery exception, motion.c:378), and the sim's
    // boot default is both lanes' OUT true. Compares type-P (falls through
    // to UNLOAD_TIMEOUT only) against type-D (UNLOAD_BLOCKED first, via
    // CONF_UNLOAD_TENSION_BLOCK_MS dwell) on the identical setup.
    {
        .name = "sem_psf_unload_stuck",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .switch_script = {.ev = {{0, SWITCH_L2_OUT, false}}, .count = 1},
        .active_lane = 1, .start_sync_active = false,
        .ul_start_at_ms = 1000, .ul_target_lane = 1,
        .tick_ceiling = 600,
        .tick_ceiling_reason = "UNLOAD_TENSION_BLOCK_MS (5000ms, type-D) from UL at t=1000 is "
                               "the earliest expected event, at t=6000; 600 ticks (12s) covers "
                               "it and type-P's later UNLOAD_MAX distance fallback with margin",
    },
    // openspec/specs/psf-type-p-sensor "Type-P Fault Timers Scoped to
    // Active Sync" / "Normal extrude does not fault on engagement". 8s idle
    // (demand=0, sync OFF, single lane loaded via forced L2 OUT=false to
    // clear sync_tick_auto_start_stop's both-loaded guard) before demand
    // kicks in and organically engages sync via sync_tick_auto_start_stop
    // (sync.c:1319) -- no site in the current codebase sets
    // g_sync_tension_pin_since_ms while sync is OFF (confirmed by grep: the
    // only writers are inside sync_rearm_active/the fast-brake path/this
    // same auto-start function, all sync-active-only), so the dwell timer
    // simply can't go stale during idle; every activation path resets it
    // fresh regardless (sync.c:1078/1376). Empirically: SYNC,AUTO_START
    // fires cleanly after the 8s idle for both sensor types, zero
    // FAULT_HOLD anywhere in a 16s run.
    {
        .name = "sem_psf_no_fault_on_idle_engagement",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 0.0f, .level2_mm_s = 10.0f,
                  .t1_ms = 8000},
        .switch_script = {.ev = {{0, SWITCH_L2_OUT, false}}, .count = 1},
        .active_lane = 1, .start_sync_active = false, .auto_mode = true,
        .tick_ceiling = 800,
        .tick_ceiling_reason = "8s idle + margin for AUTO_START to fire and settle",
    },
    // openspec/specs/sync-refactor "Type-D compression relief is
    // overfill-budgeted", organic-engage variant. Same idle_zero demand
    // profile (25mm/s consumption for 3s crosses into TENSION and
    // organically engages sync, then drops to 0 -> sustained overfeed into
    // COMPRESSION) as the type-D-only `idle_zero` scenario, but via
    // sync_tick_auto_start_stop (auto_mode=true, single lane loaded via
    // forced L2 OUT=false) instead of the sim's start_sync_active
    // shortcut, so g_sync_auto_started is genuinely true and
    // sync_check_continuous_compression's dwell-based RELIEF_PAUSE path
    // (sync.c:1767, gated on g_sync_auto_started) is actually reachable.
    {
        .name = "sem_sync_overfill_budget_probe",
        .demand = {.kind = DEMAND_IDLE_ZERO, .level_mm_s = 25.0f, .t1_ms = 3000},
        .switch_script = {.ev = {{0, SWITCH_L2_OUT, false}}, .count = 1},
        .active_lane = 1, .start_sync_active = false, .auto_mode = true,
        .tick_ceiling = 500,
        .tick_ceiling_reason = "RELIEF_PAUSE fires ~8280ms (compression dwell timer, "
                               "~CONF_SYNC_AUTO_STOP_MS after compression onset); demand "
                               "never resumes after t1=3000 so it stays there forever -- "
                               "500 ticks (10s) captures entry with margin, well under the "
                               "45s liveness backstop this deliberately-stuck-after run would "
                               "otherwise trip",
    },
    // openspec/specs/toolchange-orchestration "Normal Toolchange" attempted
    // and REMOVED: tc_start(2, t) reaches TC_UNLOAD_WAIT_TH -> TC:UNLOADING
    // (confirms "Toolhead clear wait is meaningful" partially), but the
    // unload phase then stalls waiting for OUT to clear — OUT needs to
    // transition true->false as unload physically progresses a real
    // distance, which needs more switch-script timing than a single t=0
    // event provides (same precondition-complexity class as
    // TASK_LOAD_FULL, motion-safety task 4 — not attempted there either).
    // Left the trigger fields (tc_start_at_ms/tc_target_lane) in
    // sim_scenario.h/sim_main.c since they're real, working infrastructure;
    // just no scenario built on them yet. See memories/repo/host-sync-sim.md.
};
// clang-format on

const int g_sim_scenario_count = (int)(sizeof(g_sim_scenarios) / sizeof(g_sim_scenarios[0]));

const sim_scenario_t *sim_scenario_find(const char *name) {
    for (int i = 0; i < g_sim_scenario_count; i++) {
        const char *a = g_sim_scenarios[i].name;
        const char *b = name;
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
            return &g_sim_scenarios[i];
    }
    return NULL;
}
