/// @file sim_main.c
/// @brief Entry point for flare_sim. Replaces firmware/src/main.c: owns
///        argument parsing, the boot sequence, and the tick loop, replicating
///        main.c:569's tick order over the real, linked control sources.
///        See design.md "Main loop order (must be replicated)".

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "controller_shared.h"
#include "settings_store.h"
#include "sync.h"
#include "toolchange.h"
#include "cutter.h"
#include "motion.h"

#include "hardware/flash.h"
#include "pico/flash.h"

#include "sim_fakes.h"
#include "sim_plant.h"
#include "sim_scenario.h"
#include "sim_trace.h"

// 3000 ticks at the default 20 ms tick = 60 s simulated — see design.md
// "Runtime Budget". A scenario needing more must set tick_ceiling and state
// why in tick_ceiling_reason (tasks.md section 5 accept criterion).
#define SIM_DEFAULT_TICK_CEILING 3000u

static void apply_switch_script(const switch_script_t *script, uint32_t t_ms) {
    for (int i = 0; i < script->count; i++) {
        const switch_event_t *ev = &script->ev[i];
        if (ev->t_ms != t_ms)
            continue;
        switch (ev->target) {
        case SWITCH_L1_IN:
            g_lane_l1.in_sw.stable = ev->value;
            break;
        case SWITCH_L1_OUT:
            g_lane_l1.out_sw.stable = ev->value;
            break;
        case SWITCH_L2_IN:
            g_lane_l2.in_sw.stable = ev->value;
            break;
        case SWITCH_L2_OUT:
            g_lane_l2.out_sw.stable = ev->value;
            break;
        case SWITCH_Y:
            g_y_split.stable = ev->value;
            break;
        }
    }
}

// autopreload_tick lives in firmware/src/main.c (static, not linkable — main.c
// owns GPIO/PIO init and cannot be compiled into the sim). Stubbed per
// tasks.md section 5: autopreload (idle-lane auto-load-on-insert) is not
// exercised by the simulation. See design.md Known Limitations.
static void autopreload_tick_stub(uint32_t now_ms) {
    (void)now_ms;
}

int main(int argc, char **argv) {
    const char *scenario_name = NULL;
    bool stress = false;
    float stress_lag_ms = 0.0f;
    uint32_t ticks_override = 0;
    int sensor_type = BUF_SENSOR_TYPE_D;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario_name = argv[++i];
        } else if (strcmp(argv[i], "--stress") == 0) {
            stress = true;
        } else if (strcmp(argv[i], "--stress-lag-ms") == 0 && i + 1 < argc) {
            stress_lag_ms = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            ticks_override = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--sensor-type") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            sensor_type = (v[0] == 'p' || v[0] == 'P') ? BUF_SENSOR_TYPE_P : BUF_SENSOR_TYPE_D;
        } else {
            fprintf(stderr, "flare_sim: unrecognized argument '%s'\n", argv[i]);
            return 2;
        }
    }

    if (!scenario_name) {
        fprintf(stderr, "usage: flare_sim --scenario NAME [--stress] [--stress-lag-ms N] "
                        "[--ticks N] [--sensor-type {d,p}]\n");
        return 2;
    }

    const sim_scenario_t *scn = sim_scenario_find(scenario_name);
    if (!scn) {
        fprintf(stderr, "flare_sim: unknown scenario '%s'\n", scenario_name);
        return 2;
    }

    // ---- Boot sequence: zero-init (fresh process) -> settings_defaults() ->
    // scenario overrides -> sync_init(0) -> tick loop. design.md "Scenario
    // Isolation By Process". ----
    if (scn->test_settings_load_fresh_board) {
        // persistence-contract "Fresh Board" scenario: g_sim_flash starts
        // zeroed every process (design.md "One scenario per process"), so
        // its magic/version never match SETTINGS_MAGIC/SETTINGS_VERSION —
        // settings_load() must take the same fallback path as a genuinely
        // erased board would.
        uint32_t settings_offset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
        bool flash_was_pristine = true;
        for (size_t i = 0; i < FLASH_SECTOR_SIZE; i++) {
            if (g_sim_flash[settings_offset + i] != 0) {
                flash_was_pristine = false;
                break;
            }
        }
        settings_load();
        bool flash_still_pristine = true;
        for (size_t i = 0; i < FLASH_SECTOR_SIZE; i++) {
            if (g_sim_flash[settings_offset + i] != 0) {
                flash_still_pristine = false;
                break;
            }
        }
        fprintf(stderr, "PERSIST fresh_board_was_pristine=%d written_back=%d\n",
               flash_was_pristine, !flash_still_pristine);
    } else {
        settings_defaults();
    }

    g_buf_sensor_type = sensor_type;
    g_active_lane = scn->active_lane;
    g_auto_mode = scn->auto_mode ? 1 : 0;
    g_reload_mode = scn->reload_mode ? 1 : 0;

    if (scn->cut_feed_mm_override)
        g_cut_feed_mm = scn->cut_feed_mm_override;
    if (scn->cut_timeout_feed_ms_override)
        g_cut_timeout_feed_ms = scn->cut_timeout_feed_ms_override;
    if (scn->servo_settle_ms_override)
        g_servo_settle_ms = scn->servo_settle_ms_override;
    if (scn->cut_timeout_settle_ms_override)
        g_cut_timeout_settle_ms = scn->cut_timeout_settle_ms_override;

    // lane_id is set by lane_setup() in real firmware boot (main.c), which we
    // never call (motor/GPIO init is meaningless over the fakes) — but
    // lane_id itself is real state other logic depends on (lane_to_idx,
    // lane_id_str, reload_trigger's other-lane lookup), so it must still be
    // seeded. Found via reload_trigger emitting "RUNOUT,0" instead of
    // "RUNOUT,1" and failing its other-lane lookup.
    g_lane_l1.lane_id = 1;
    g_lane_l2.lane_id = 2;
    // Likewise lane_t.m (motor_t) is set by motor_init(), never called here.
    // sync.c's BL prime/lock/catch drives motor_set_rate_sps()/motor_set_dir()
    // directly against lane->m, bypassing lane_t.current_sps/task_forward
    // entirely — sim_plant.c falls back to decoding the commanded rate from
    // the PWM fake (sim_motor_rate_sps) for exactly this path, keyed by
    // lane->m.slice, so distinct real step-pin wiring per lane matters here
    // (motor_init() would be safe to call — all its gpio_*/pwm_* calls are
    // no-op fakes — this just avoids the debounced_input_init side call in
    // the lane_setup() it's normally wrapped in).
    motor_init(&g_lane_l1.m, PIN_L1_EN, PIN_L1_DIR, PIN_L1_STEP, false);
    motor_init(&g_lane_l2.m, PIN_L2_EN, PIN_L2_DIR, PIN_L2_STEP, false);
    g_lane_l1.in_sw.stable = true;
    g_lane_l1.out_sw.stable = true;
    g_lane_l2.in_sw.stable = true;
    g_lane_l2.out_sw.stable = true;
    g_y_split.stable = false;
    g_buf_tension_din.stable = false;
    g_buf_compression_din.stable = false;

    sync_init(0);

    if (scn->start_sync_active) {
        set_toolhead_filament(true);
    }

    sim_plant_t plant;
    sim_plant_init(&plant, stress, stress_lag_ms);

    uint32_t ceiling = ticks_override ? ticks_override
                                      : (scn->tick_ceiling ? scn->tick_ceiling : SIM_DEFAULT_TICK_CEILING);

    sim_trace_header();

    for (uint32_t tick = 0; tick < ceiling; tick++) {
        uint32_t dt_ms = (uint32_t)g_sync_tick_ms;
        if (dt_ms == 0)
            dt_ms = 20;

        apply_switch_script(&scn->switch_script, g_now_ms);
        sim_plant_tick(&plant, scn, g_now_ms, dt_ms, sensor_type);

        if (scn->manual_reload_at_ms != 0 && g_now_ms == scn->manual_reload_at_ms) {
            tc_manual_reload(g_now_ms);
        }
        if (scn->bl_arm_at_ms != 0 && g_now_ms == scn->bl_arm_at_ms) {
            sync_buffer_lock_arm((buf_state_t)scn->bl_arm_target, scn->bl_arm_follow_mm,
                                 scn->bl_arm_follow_rate_mmpm, g_now_ms);
        }
        if (scn->bl_clear_at_ms != 0 && g_now_ms == scn->bl_clear_at_ms) {
            sync_retract_assist_set(false);
        }
        if (scn->cutter_start_at_ms != 0 && g_now_ms == scn->cutter_start_at_ms) {
            cutter_start(lane_ptr(scn->active_lane), scn->cutter_enable_feed, g_now_ms);
        }
        if (scn->tc_start_at_ms != 0 && g_now_ms == scn->tc_start_at_ms) {
            tc_start(scn->tc_target_lane, g_now_ms);
        }
        if (scn->bs_request_at_ms != 0 && g_now_ms == scn->bs_request_at_ms) {
            buffer_stabilize_request(g_now_ms);
        }
        if (scn->ul_start_at_ms != 0 && g_now_ms == scn->ul_start_at_ms) {
            lane_t *ul_lane = lane_ptr(scn->ul_target_lane);
            lane_start(ul_lane, TASK_UNLOAD, g_rev_sps, false, g_now_ms, (float)g_unload_max_mm);
        }

        buffer_stabilize_tick(g_now_ms);
        cutter_tick(g_now_ms);
        tc_tick(g_now_ms);
        autopreload_tick_stub(g_now_ms);
        lane_tick(&g_lane_l1, g_now_ms);
        lane_tick(&g_lane_l2, g_now_ms);
        buf_sensor_tick(g_now_ms);
        sync_tick(g_now_ms);

        // Documented override, not modeled physics — see sim_scenario.h.
        // Applied after the real tick chain so it reflects this tick's true
        // RELOAD-active state without racing tc_tick's own transitions.
        if (scn->force_no_consumer && tc_state() != TC_IDLE) {
            g_extruder_est_sps = 0.0f;
        }

        const char *violation = sim_trace_tick(g_now_ms, &plant, scn->active_lane);
        if (violation) {
            fprintf(stderr, "flare_sim: %s\n", violation);
            return 1;
        }

        g_now_ms += dt_ms;
    }

    return 0;
}
