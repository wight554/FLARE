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
        // psf-runout-escalation-race-fix: fast/complete runout (high 20mm/s
        // demand, jam+sensor-clear at t=5000, same as
        // reload_genuine_runout_escalation but WITHOUT the low-demand
        // workaround) reproduces the real-rig race: the buffer saturates
        // the tension rail well inside CONF_PSF_WALL_SAT_MS (1000ms),
        // trips sync_tick_type_p_rail_guard's fault-hold before H6's 6s
        // dwell-based escalation (sync_check_tension_dwell_and_ramp) ever
        // gets a chance to run (sync_tick returns early once gated_checks
        // fires). Before the fix: loops FAULT_HOLD/FAULT_HOLD_RECOVERY
        // forever, RUNOUT/RELOAD:SWITCHING never fire. Confirmed against
        // real firmware, matching a 2026-07-27 real-rig capture exactly.
        .name = "reload_fast_runout_rail_guard_race", // psf-runout-escalation-race-fix
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 20.0f},
        .feed_gain = {.bp = {{0, 1.0f}, {5000, 0.0f}}, .count = 2},
        .switch_script = {.ev = {{5000, SWITCH_L1_IN, false}, {5000, SWITCH_L1_OUT, false}},
                          .count = 2},
        .active_lane = 1, .start_sync_active = true, .reload_mode = true,
        .force_no_consumer = true,
        .tick_ceiling = 1150,
        .tick_ceiling_reason = "before the fix the buffer stays continuously saturated across "
                               "the whole FAULT_HOLD/RECOVERY loop (only the sync_state label "
                               "changes, not the position) -- invariant 6's 20s saturation "
                               "bound trips at t=25800 (saturation onset ~t=5800); 1150 ticks "
                               "(23s) stays under that while still covering 3 full loop cycles "
                               "(~6.4s period), plenty to prove the infinite-loop pattern",
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
    {
        .name = "reload_runout_lane2_to_lane1", // multi-lane symmetry (2->1)
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 1.0f},
        .feed_gain = {.bp = {{0, 1.0f}, {5000, 0.0f}}, .count = 2},
        .switch_script = {.ev = {{5000, SWITCH_L2_IN, false}, {5000, SWITCH_L2_OUT, false}},
                          .count = 2},
        .active_lane = 2, .start_sync_active = true, .reload_mode = true,
        .force_no_consumer = true,
        .tick_ceiling = 4000,
        .tick_ceiling_reason = "dwell(6s) + join-delay(10s) + approach/follow for lane 2->1 symmetry",
    },
    {
        .name = "reload_target_empty_abort", // runout when target lane has no filament
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 1.0f},
        .feed_gain = {.bp = {{0, 1.0f}, {5000, 0.0f}}, .count = 2},
        .switch_script = {.ev = {{0, SWITCH_L2_IN, false},
                                 {5000, SWITCH_L1_IN, false},
                                 {5000, SWITCH_L1_OUT, false}},
                          .count = 3},
        .active_lane = 1, .start_sync_active = true, .reload_mode = true,
        .force_no_consumer = true,
        .tick_ceiling = 1000,
        .tick_ceiling_reason = "dwell(6s) triggers H6 runout escalation, which aborts on empty target lane",
    },
    {
        .name = "reload_manual_resume_empty_active", // H4 missed-swap resume via swap
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 1.0f},
        .switch_script = {.ev = {{0, SWITCH_L1_IN, false}, {0, SWITCH_L1_OUT, false}},
                          .count = 2},
        .active_lane = 1, .start_sync_active = false, .reload_mode = true,
        .manual_reload_at_ms = 1000,
        .force_no_consumer = true,
        .tick_ceiling = 4000,
        .tick_ceiling_reason = "manual RL resumes reload via swap to loaded lane 2",
    },
    {
        .name = "reload_mmu_mode_no_escalation", // reload_mode=0 suppresses auto-switch
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 1.0f},
        .feed_gain = {.bp = {{0, 1.0f}, {5000, 0.0f}}, .count = 2},
        .switch_script = {.ev = {{5000, SWITCH_L1_IN, false}, {5000, SWITCH_L1_OUT, false}},
                          .count = 2},
        .active_lane = 1, .start_sync_active = true, .reload_mode = false,
        .force_no_consumer = true,
        .tick_ceiling = 1000,
        .tick_ceiling_reason = "tension dwell under reload_mode=0 enters FAULT_HOLD, never RELOAD",
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
        // buffer-state-lock D5 / 12-SPEC §3: bare BL:T (no follow_mm/rate) is a
        // passive lock. An external retract while locked must not trigger
        // BL:BREAK/BL:FOLLOW or any motor motion — the lock just holds and the
        // watchdog/BS release it. Same demand as sem_bl_lock_catch, no follow.
        .name = "sem_bl_bare_passive",
        .demand = {.kind = DEMAND_RETRACT, .level_mm_s = 15.0f, .t1_ms = 6000, .t2_ms = 9000},
        .active_lane = 1, .start_sync_active = false,
        .bl_arm_at_ms = 1000, .bl_arm_target = 1 /* BUF_TENSION */,
        .bl_clear_at_ms = 10000,
        .tick_ceiling = 1100,
        .tick_ceiling_reason = "lock by ~1.5s, retract 6-9s, BS at 10s",
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

    // Rig-shaped BL:T -> extruder retract chain (klipper _FLARE_BL_RETRACT on the
    // 16 mm type-P rig). The extruder retract starts a short host round-trip
    // after EV:BL:LOCKED (92e39e7 removed the 1 s G4 dwell that used to sit
    // between them). rail_scale models a hard end that reads shallower than
    // the calibrated rail. Success criterion: BL:BREAK + BL:FOLLOW fire so the
    // MMU actually follows a retract longer than the buffer; without them the
    // buffer pins at the compression rail and the extruder skips (tip never
    // parks -> TC unload UNLOAD_TIMEOUT).
    {
        .name = "bl_retract_immediate",
        .demand = {.kind = DEMAND_RETRACT, .level_mm_s = 50.0f, .t1_ms = 1560, .t2_ms = 2420},
        .active_lane = 1, .start_sync_active = false,
        .bl_arm_at_ms = 1000, .bl_arm_target = 1 /* BUF_TENSION */,
        .bl_arm_follow_mm = 43.0f, .bl_arm_follow_rate_mmpm = 5000.0f,
        .buf_max_travel_override = 16, .type_p_rail_scale = 0.7f,
        .tick_ceiling = 300, .type_specific = true,
    },
    {
        .name = "bl_retract_paused",
        .demand = {.kind = DEMAND_RETRACT, .level_mm_s = 50.0f, .t1_ms = 2300, .t2_ms = 3160},
        .active_lane = 1, .start_sync_active = false,
        .bl_arm_at_ms = 1000, .bl_arm_target = 1 /* BUF_TENSION */,
        .bl_arm_follow_mm = 43.0f, .bl_arm_follow_rate_mmpm = 5000.0f,
        .buf_max_travel_override = 16, .type_p_rail_scale = 0.7f,
        .tick_ceiling = 350, .type_specific = true,
    },

    // Phase 13 (type-P bounded relief, D-01/D-02/D-22): a step_up-shaped
    // demand strong enough to hold the type-P buffer pinned at the tension
    // rail for hundreds of ticks, at the rig's real 16 mm buffer travel
    // (buf_max_travel_override) rather than this config's 25 mm dev default.
    // sem_psf_relief_bound/_shallow/_shallow_rail are rail-scale twins
    // (1.0/0.7/0.5, D-25) proving the relief bound + RELIEF_ON trigger are
    // rail-relative, not the absolute-literal 51bdca8 failure class.
    {
        .name = "sem_psf_relief_bound",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f, .t1_ms = 3000},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_specific = true,
    },
    {
        .name = "sem_psf_relief_bound_shallow",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f, .t1_ms = 3000},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_p_rail_scale = 0.7f, .type_specific = true,
    },
    {
        .name = "sem_psf_relief_shallow_rail",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f, .t1_ms = 3000},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_p_rail_scale = 0.5f, .type_specific = true,
    },
    // REVIEW-07 tripwire: an early transient deflection excursion deeper
    // than the rail the run subsequently settles at (demand_gain spike to
    // 4x), a full recovery well off it (demand_gain back to 1x), then a
    // second, shallower starvation later in the same sync window (demand_gain
    // to 2.5x, no AUTO_START in between). Without the extreme relaxation the
    // second episode never re-enters the relief zone (it's shallower than the
    // stale extreme from the first spike).
    {
        .name = "sem_psf_relief_extreme_stale",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 20.0f},
        .demand_gain = {.bp = {{.t_ms = 0, .value = 1.0f},
                               {.t_ms = 4000, .value = 2.5f},
                               {.t_ms = 4400, .value = 1.0f},
                               {.t_ms = 12000, .value = 1.8f},
                               {.t_ms = 12400, .value = 1.0f}},
                        .count = 5},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_specific = true,
        // Phase 13 Plan 02: this scenario's sustained tension-pin demand
        // shape (by design, to stress extreme-tracking) legitimately crosses
        // the new SYNC_TENSION_STOP_MM=32mm default as an unrelated side
        // effect, firing FAULT_HOLD/AUTO_START and confounding the REVIEW-07
        // assertion below (which specifically checks that no AUTO_START
        // occurs). Disabled here, not by weakening the trip: this scenario
        // isolates extreme-relaxation, not the distance trip -- Task 3 adds
        // purpose-built scenarios for the trip itself.
        .tension_stop_mm_disabled = true,
    },
    // Phase 13 Task 2 (D-04): FLARE_INT_SYNC_TENSION_RAMP_DELAY_MS defaults to
    // 0 (disabled) so tension_ramp_delay_ms_override shortens it to 500ms --
    // long enough to distinguish "ramp fired" from "ramp never fires" but
    // short enough the run doesn't need extending past the shared demand
    // step's saturation window. Proves the ramp's target-raise (D-04) is
    // capped at the same relief bound as the apply-side branch, not just at
    // max_sps, even once the ramp's own escalation has kicked in.
    {
        .name = "sem_psf_relief_ramp_capped",
        // Brief huge spike (13.3x -> 200mm/s for 300ms) drives the buffer
        // deep into tension near the physical rail, then demand drops to a
        // clearly-achievable 15mm/s: the buffer recovers GRADUALLY (several
        // seconds), passing through the band between (tracked extreme +
        // SOFT_WALL_MARGIN_NORM) and wherever the debounced BUF_TENSION
        // classification actually ends. g_sync_tension_pin_since_ms keeps
        // counting the whole time (state never leaves BUF_TENSION), so once
        // the shortened 500ms ramp delay elapses the ramp forces
        // target_sps=max_sps on every tick for as long as s stays TENSION --
        // including ticks where in_relief_zone has already gone false. At
        // 15mm/s the relief bound is far below max_sps, so an uncapped ramp
        // and a capped one produce clearly different feed values here.
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 15.0f},
        .demand_gain = {.bp = {{.t_ms = 0, .value = 1.0f},
                               {.t_ms = 2000, .value = 13.3333f},
                               {.t_ms = 2300, .value = 1.0f}},
                        .count = 3},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .tension_ramp_delay_ms_override = 500,
        .tick_ceiling = 2000,
        .tick_ceiling_reason = "spike at 2000ms + gradual multi-second recovery through the "
                               "extreme-relative/debounce gap + shortened 500ms ramp delay",
        .type_specific = true,
    },
    {
        .name = "sem_psf_relief_ramp_capped_shallow",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f, .t1_ms = 3000},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .tension_ramp_delay_ms_override = 500,
        .type_p_rail_scale = 0.7f,
        .tick_ceiling = 1500,
        .tick_ceiling_reason = "demand step at 3000ms + shortened 500ms ramp delay + hold "
                               "long enough to observe the capped ramp",
        .type_specific = true,
    },

    // Phase 13 Task 1 (D-07/D-08/D-09/D-10): SYNC_TENSION_STOP_MM distance
    // trip. feed_gain=0.3 (chronic underfeed) rather than jam_upstream's
    // drop-to-zero: at 0.3 the commanded feed still reaches the plant at 30%
    // strength, so the buffer keeps creeping (never fully stalls) while
    // staying pinned inside BUF_TENSION long enough for the commanded-feed
    // accumulator (g_sync_refill_effort_mm, sync_buf.c) to pass the 32mm
    // default. A drop-to-zero feed_gain would ALSO trip this, but 13-03's
    // ~16mm probe evaluates the same accumulator and short-circuits on a
    // buffer that never moves at all -- a chronic-creep shape stays valid
    // sim coverage once that probe lands, a hard-stall shape would not.
    {
        .name = "sem_psf_mm_trip",
        // Same demand shape as 13-01's proven sem_psf_relief_bound (D-08's
        // read_first flags this file's own bl_retract twin idiom, but the
        // relevant precedent here is that THIS exact profile is already
        // confirmed, by running it, to settle NEUTRAL/slight-COMPRESSION by
        // t~2000-3000ms before the t1=3000 demand step drives it back into
        // TENSION -- a genuine observed edge, which is what arms the trip
        // (sync_on_transition() is the only place that does, and a boot-time
        // buf_force_stable_state() call already classifies the goal-relative
        // centre as TENSION for this rig's compression-biased goal, so a
        // demand profile that is TENSION from tick 0 never produces an
        // observed edge at all and stays permanently unarmed). At t1=3000,
        // demand steps to 36mm/s AND feed_gain drops to 0.7 in the same
        // tick (print resumes with a partial jam already present) -- 0.7,
        // not jam_upstream's drop-to-zero and not the plan-suggested 0.3,
        // empirically chosen (by reading the trace) as the largest
        // reduction whose physical position still creeps rather than
        // slamming to the rail fast enough to let the pre-existing 1s
        // CONF_PSF_WALL_SAT_MS absolute-saturation guard win the race
        // against the mm accumulator (whose commanded-feed clock, not the
        // physical slack, is what this trip actually measures). The buffer
        // must creep measurably off its deepest reading while staying
        // inside BUF_TENSION long enough for the accumulator to pass 32mm;
        // this is also chronic-creep, not a hard stall, so 13-03's ~16mm
        // probe (which short-circuits on a buffer that never moves at all)
        // stays exercised by this scenario once it lands.
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.7f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_specific = true,
        .tick_ceiling = 400,
        .tick_ceiling_reason = "3s settle + chronic 0.7x underfeed at 36mm/s demand crosses "
                               "the 32mm trip well inside 400 ticks (8s)",
    },
    // D-10 arming: a scenario that starts pinned at tension and never
    // produces a single buffer-state transition must never trip, regardless
    // of how long the (would-be) accumulator would otherwise run -- the
    // steady demand keeps commanded feed flat with no jam/gain schedule at
    // all, and the buffer settles into TENSION on tick 1 and never leaves.
    {
        .name = "sem_psf_trip_unarmed",
        // Zero demand, no gain schedule, no switch script: the buffer boots
        // directly into the goal-relative TENSION classification (this
        // rig's compression-biased goal reads the centred rest position as
        // TENSION -- see sem_psf_mm_trip's comment) and, with nothing to
        // disturb it, never leaves that single zone for the whole run --
        // sync_on_transition() never fires, so g_sync_trip_armed never
        // becomes true, regardless of how long this scenario would
        // otherwise run.
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .active_lane = 1, .start_sync_active = true, .type_specific = true,
        .tick_ceiling = 400,
        .tick_ceiling_reason = "same window as sem_psf_mm_trip for an easy side-by-side "
                               "comparison; this scenario must show zero zone transitions",
    },
    // D-08/D-28: SET:SYNC_TENSION_STOP_MM:0 disables the trip. The sim
    // harness never processes SET: commands (sim_scenario.h's own note on
    // tension_ramp_delay_ms_override), so tension_stop_mm_disabled forces
    // g_sync_tension_stop_mm to 0 directly -- identical demand/feed_gain
    // shape to sem_psf_mm_trip, proving the same dynamics that trip above
    // emit nothing with the knob at its documented disable value.
    {
        .name = "sem_psf_mm_trip_disabled",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.7f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_specific = true,
        .tension_stop_mm_disabled = true,
        .tick_ceiling = 400,
        .tick_ceiling_reason = "identical to sem_psf_mm_trip so the only variable is the "
                               "disabled knob",
    },

    // Phase 13 Task 3 (D-25): rail-scale (0.7) twins of Task 1's two
    // scenarios. Rail scale attenuates the ANALOG SENSOR reading the
    // firmware sees (sim_plant.c's emit_type_p), not the physical slack or
    // the commanded-feed accumulator the trip actually reads -- so the
    // qualitative outcome (fires / never arms) is expected to hold
    // identically, only the exact tick a transition lands on can shift.
    {
        .name = "sem_psf_mm_trip_shallow",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.7f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_p_rail_scale = 0.7f, .type_specific = true,
        .tick_ceiling = 400,
        .tick_ceiling_reason = "identical to sem_psf_mm_trip, rail scale 0.7",
    },
    {
        .name = "sem_psf_trip_unarmed_shallow",
        .demand = {.kind = DEMAND_STEADY, .level_mm_s = 0.0f},
        .active_lane = 1, .start_sync_active = true,
        .type_p_rail_scale = 0.7f, .type_specific = true,
        .tick_ceiling = 400,
        .tick_ceiling_reason = "identical to sem_psf_trip_unarmed, rail scale 0.7",
    },

    // D-08/D-09: distance is the PRIMARY trip at print flow, time is the
    // slow-flow FALLBACK -- proven by two scenarios that trip on DIFFERENT
    // thresholds, not by one that trips on both. sem_psf_mm_before_ms
    // reuses sem_psf_mm_trip's exact profile: the mm accumulator crosses
    // 32mm around t~1000ms after the t=3000 step (see sem_psf_mm_trip's
    // comment), thousands of ms before FLARE_INT_SYNC_TENSION_DWELL_STOP_MS
    // (6000ms, checked in tune.h before picking this) could ever elapse.
    {
        .name = "sem_psf_mm_before_ms",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.7f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_specific = true,
        .tick_ceiling = 400,
        .tick_ceiling_reason = "mm trip fires well under 400 ticks (8s), long before the "
                               "6s ms dwell timer could complete",
    },
    {
        .name = "sem_psf_mm_before_ms_shallow",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.7f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_p_rail_scale = 0.7f, .type_specific = true,
        .tick_ceiling = 400,
        .tick_ceiling_reason = "identical to sem_psf_mm_before_ms, rail scale 0.7",
    },
    // sem_psf_ms_fallback: empirically, distance ALWAYS beats the 6000ms ms
    // dwell timer once the type-P relief branch engages, at ANY demand this
    // config's flow schedule can reach -- sync_type_p_relief_bound_sps
    // floors every relief-zone target at flow_param(...).baseline_sps
    // (10912 sps ~= 26.7mm/s with this project's default single-point
    // schedule), and relief-zone entry (sync_type_p_in_relief_zone) is true
    // for nearly the entire duration of any continuing/worsening tension
    // excursion (g_buf_pos <= tracked extreme + SOFT_WALL_MARGIN_NORM,
    // sync_internal.h). That floor alone crosses 32mm in ~1.2s regardless
    // of the scripted demand or feed_gain reduction -- confirmed by running
    // even 13-01's OWN sem_psf_relief_bound (no artificial feed_gain at
    // all) through this build: it also trips TENSION_STOP:MM within ~1.1s
    // of entering deep tension. Tried demand steps from 4mm/s to 15mm/s,
    // permanent full jams, and default vs 16mm buf_max_travel -- every
    // combination that sustains a multi-second dwell also blows past 32mm
    // in under 1.5s once relief engages; every combination gentle enough to
    // avoid relief recovers to NEUTRAL/COMPRESSION within ~1s and never
    // dwells 6s at all (see 13-02-SUMMARY.md Deviations for the full
    // investigation). tension_stop_mm_disabled isolates the ms path itself
    // rather than proving an organic race the current baseline_sps/
    // SYNC_PSF_RELIEF_MULT tuning cannot produce: 0.95 feed_gain leaves
    // just enough residual deficit (~1.15mm/s once feed maxes at 15004sps)
    // to sustain continuous TENSION for the full 6s window while staying
    // well off the physical rail (no sat=T anywhere in this trace), so the
    // ms trip -- not the pre-existing CONF_PSF_WALL_SAT_MS rail guard --
    // is demonstrably what fires.
    {
        .name = "sem_psf_ms_fallback",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.95f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true, .type_specific = true,
        .tension_stop_mm_disabled = true,
        .tick_ceiling = 550,
        .tick_ceiling_reason = "3s settle + 6s ms dwell fallback + margin",
    },
    {
        .name = "sem_psf_ms_fallback_shallow",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.95f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
        .type_p_rail_scale = 0.7f, .type_specific = true,
        .tension_stop_mm_disabled = true,
        .tick_ceiling = 550,
        .tick_ceiling_reason = "identical to sem_psf_ms_fallback, rail scale 0.7",
    },

    // D-26: a BL lock armed across the whole window suppresses both trips
    // entirely -- same t=3000 step-into-tension + chronic-underfeed shape
    // as sem_psf_mm_trip (which would otherwise fire the mm trip around
    // t~4100ms), but bl_arm_at_ms fires just before that, at t=3500, and is
    // never cleared for the rest of the run.
    {
        .name = "sem_psf_trip_held_suppressed",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.7f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_specific = true,
        .bl_arm_at_ms = 3500, .bl_arm_target = 1 /* BUF_TENSION */,
        .tick_ceiling = 400,
        .tick_ceiling_reason = "same window as sem_psf_mm_trip; the lock arms before the "
                               "mm trip would otherwise fire and is held for the rest of it",
    },
    {
        .name = "sem_psf_trip_held_suppressed_shallow",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.7f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_p_rail_scale = 0.7f, .type_specific = true,
        .bl_arm_at_ms = 3500, .bl_arm_target = 1 /* BUF_TENSION */,
        .tick_ceiling = 400,
        .tick_ceiling_reason = "identical to sem_psf_trip_held_suppressed, rail scale 0.7",
    },

    // REVIEW-04: a BL lock armed while the buffer is pinned, then CLEARED
    // mid-run (bl_clear_at_ms) with the pin continuing afterward, must not
    // trip within the tick window immediately following the clear -- the
    // hold's falling edge zeroed the accumulator and disarmed the trip, so
    // the loop must observe a fresh transition and re-accumulate the full
    // 32mm before it can fire again. bl_clear_at_ms=5000 sits comfortably
    // after the lock engages (~3500ms + prime time) and well before this
    // scenario's own tick_ceiling, leaving room for the python test to
    // assert the no-trip window in ticks derived from bl_clear_at_ms.
    // Observed: sync_retract_assist_set(false) (the BS path bl_clear_at_ms
    // calls) drops sync to SYNC_OFF and this scenario has no auto_mode, so
    // it never organically re-engages afterward -- the no-trip window this
    // asserts is therefore also permanent for the rest of the run, which is
    // a stronger (not weaker) proof that the reset held: nothing re-arms it
    // by accident either.
    {
        .name = "sem_psf_trip_hold_release",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.7f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_specific = true,
        .bl_arm_at_ms = 3500, .bl_arm_target = 1 /* BUF_TENSION */,
        .bl_clear_at_ms = 5000,
        .tick_ceiling = 500,
        .tick_ceiling_reason = "lock engages ~3.5s, releases at 5s, margin past the "
                               "post-release re-accumulation window",
    },
    {
        .name = "sem_psf_trip_hold_release_shallow",
        .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f,
                  .t1_ms = 3000},
        .feed_gain = {.bp = {{3000, 0.7f}}, .count = 1},
        .active_lane = 1, .start_sync_active = true,
        .buf_max_travel_override = 16, .type_p_rail_scale = 0.7f, .type_specific = true,
        .bl_arm_at_ms = 3500, .bl_arm_target = 1 /* BUF_TENSION */,
        .bl_clear_at_ms = 5000,
        .tick_ceiling = 500,
        .tick_ceiling_reason = "identical to sem_psf_trip_hold_release, rail scale 0.7",
    },
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
