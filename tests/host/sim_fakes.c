/// @file sim_fakes.c
/// @brief Fakes for every symbol the linked firmware sources (sync*.c, motion.c,
///        toolchange.c, cutter.c, settings_store.c) reference but main.c/
///        protocol.c/tmc2209.c would otherwise supply. Two kinds:
///
///        1. Hardware register fakes (GPIO/PWM/ADC/flash/clock) — record-only,
///           no logic to drift. motion.c and cutter.c run their real bodies on
///           top of these; the sim reads results back through real struct
///           fields (lane_t.current_sps, lane_t.task_forward), not by decoding
///           fake PWM registers.
///        2. main.c/protocol.c callback fakes (lane_ptr, set_active_lane,
///           set_toolhead_filament, other_lane, cmd_event, cmd_event_critical,
///           manual_unload_active) — main.c and protocol.c are not linked into
///           the sim, so these are reimplemented. lane_ptr/set_active_lane/
///           other_lane/cmd_event/cmd_event_critical are pure accessors, copied
///           verbatim. set_toolhead_filament carries real sync-enable/disable
///           policy (main.c:319) that scenarios depend on (e.g. runout ->
///           sync disable), so its body is also copied verbatim rather than
///           stubbed — keep it in sync with main.c by hand if that function
///           changes; nothing automated guards this one.
///
/// Undefined-symbol surface is authoritative — regenerate with
/// `nm -u build_sim/CMakeFiles/flare_sim.dir/**/*.o | sort -u` rather than
/// trusting this comment if the linked source set ever changes.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "controller_shared.h"
#include "protocol.h"
#include "sync.h"

#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "tmc2209.h"

#include "sim_fakes.h"

// ===================== Clock =====================
// No wall-clock read anywhere in tests/host/ — time is g_now_ms, advanced only
// by sim_main.c's tick loop.
absolute_time_t get_absolute_time(void) {
    return (absolute_time_t)g_now_ms * 1000u;
}

uint32_t to_ms_since_boot(absolute_time_t t) {
    return (uint32_t)(t / 1000u);
}

void sleep_ms(uint32_t ms) {
    (void)ms; // never called by the linked sources; present for header completeness
}

unsigned int clock_get_hz(clock_index_t clk) {
    (void)clk;
    return 125000000u; // RP2040 default sys clock; only affects motion.c's internal
                        // PWM wrap/clkdiv math, which the sim never reads back
}

// ===================== GPIO / PWM =====================
// Record-only. The sim reads commanded motion back through lane_t fields
// (current_sps, task_forward) that motion.c itself maintains, not through
// these registers, so exact PWM fidelity is unnecessary — see file header.
#define SIM_NUM_PINS 30
#define SIM_NUM_PWM_SLICES 8

static bool s_gpio_out[SIM_NUM_PINS];
static uint16_t s_pwm_level[SIM_NUM_PWM_SLICES];
static bool s_pwm_enabled[SIM_NUM_PWM_SLICES];
static uint16_t s_pwm_wrap[SIM_NUM_PWM_SLICES];
static float s_pwm_clkdiv[SIM_NUM_PWM_SLICES];

void gpio_init(uint pin) {
    (void)pin;
}

void gpio_set_dir(uint pin, bool out) {
    (void)pin;
    (void)out;
}

void gpio_put(uint pin, bool value) {
    if (pin < SIM_NUM_PINS)
        s_gpio_out[pin] = value;
}

bool gpio_get(uint pin) {
    return (pin < SIM_NUM_PINS) ? s_gpio_out[pin] : false;
}

void gpio_pull_up(uint pin) {
    (void)pin;
}

void gpio_pull_down(uint pin) {
    (void)pin;
}

void gpio_disable_pulls(uint pin) {
    (void)pin;
}

void gpio_set_function(uint pin, uint fn) {
    (void)pin;
    (void)fn;
}

pwm_config pwm_get_default_config(void) {
    pwm_config c = {0};
    return c;
}

void pwm_config_set_clkdiv(pwm_config *c, float div) {
    c->clkdiv = div;
}

void pwm_config_set_wrap(pwm_config *c, uint16_t wrap) {
    c->wrap = wrap;
}

uint pwm_gpio_to_slice_num(uint gpio) {
    return gpio % SIM_NUM_PWM_SLICES;
}

uint pwm_gpio_to_channel(uint gpio) {
    return gpio % 2u;
}

void pwm_init(uint slice, pwm_config *c, bool start) {
    (void)slice;
    (void)c;
    (void)start;
}

void pwm_set_enabled(uint slice, bool enabled) {
    if (slice < SIM_NUM_PWM_SLICES)
        s_pwm_enabled[slice] = enabled;
}

void pwm_set_clkdiv(uint slice, float div) {
    if (slice < SIM_NUM_PWM_SLICES)
        s_pwm_clkdiv[slice] = div;
}

void pwm_set_wrap(uint slice, uint16_t wrap) {
    if (slice < SIM_NUM_PWM_SLICES)
        s_pwm_wrap[slice] = wrap;
}

// Some code paths (sync.c's BL prime/lock/catch) drive the motor directly via
// motor_set_rate_sps()/motor_set_dir(), bypassing lane_t.current_sps/
// task_forward entirely (those fields are motion.c's own bookkeeping, not
// touched by direct PWM calls). sim_plant.c needs this to see BL-driven
// motion — see memories/repo/host-sync-sim.md. Inverts motion.c's own
// sps -> (clkdiv, wrap) conversion: freq = sys_clk / (clkdiv * (wrap+1)),
// and one PWM period = one step pulse, so freq == sps.
float sim_motor_rate_sps(uint slice) {
    if (slice >= SIM_NUM_PWM_SLICES || !s_pwm_enabled[slice])
        return 0.0f;
    float clkdiv = s_pwm_clkdiv[slice];
    if (clkdiv < 1.0f)
        return 0.0f;
    float wrap_plus_one = (float)s_pwm_wrap[slice] + 1.0f;
    return 125000000.0f / (clkdiv * wrap_plus_one); // clock_get_hz(clk_sys)'s fixed value
}

void pwm_set_chan_level(uint slice, uint chan, uint16_t level) {
    (void)chan;
    if (slice < SIM_NUM_PWM_SLICES)
        s_pwm_level[slice] = level;
}

void pwm_set_gpio_level(uint gpio, uint16_t level) {
    uint slice = pwm_gpio_to_slice_num(gpio);
    if (slice < SIM_NUM_PWM_SLICES)
        s_pwm_level[slice] = level;
}

// ===================== ADC =====================
// adc_read is the type-P sensor injection point; sim_plant.c owns the mapping
// from simulated buffer slack to counts and writes it here each tick.
uint16_t g_sim_adc_counts = 2048;

void adc_select_input(uint32_t input) {
    (void)input;
}

uint16_t adc_read(void) {
    return g_sim_adc_counts;
}

// ===================== Flash =====================
// RAM-backed flash so settings_store.c's save/load round trip runs as real
// firmware code (design.md "Runtime Globals Generated From Firmware
// Definitions").
uint8_t g_sim_flash[PICO_FLASH_SIZE_BYTES];

void flash_range_erase(uint32_t flash_offs, size_t count) {
    memset(g_sim_flash + flash_offs, 0xFF, count);
}

void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count) {
    memcpy(g_sim_flash + flash_offs, data, count);
}

int flash_safe_execute(flash_safe_execute_func func, void *param, uint32_t timeout_ms) {
    (void)timeout_ms;
    func(param);
    return 0;
}

// ===================== TMC2209 =====================
// tmc2209.c is out of scope (needs generated PIO headers, see proposal.md
// Scope). Only the four entry points settings_store.c/motion.c actually call
// are faked; nothing dereferences the tmc_t pointer, so a NULL lane->tmc
// (lane_setup() is never called — main.c owns it) is safe.
bool tmc_set_run_current_ma(tmc_t *tmc, int run_ma, int hold_ma) {
    (void)tmc;
    (void)run_ma;
    (void)hold_ma;
    return true;
}

bool tmc_setup_chopconf(tmc_t *tmc, int microsteps, int toff, int tbl, int hstrt, int hend,
                        bool intpol) {
    (void)tmc;
    (void)microsteps;
    (void)toff;
    (void)tbl;
    (void)hstrt;
    (void)hend;
    (void)intpol;
    return true;
}

bool tmc_set_stealthchop_sps(tmc_t *tmc, int sps, int microsteps) {
    (void)tmc;
    (void)sps;
    (void)microsteps;
    return true;
}

bool tmc_set_pwmconf(tmc_t *tmc) {
    (void)tmc;
    return true;
}

// ===================== main.c pure helpers =====================
// Copied verbatim from firmware/src/main.c. Pure math, no state, safe by
// inspection; g_shadow_ihold_irun's only consumer is protocol.c status
// reporting (out of scope), so build_ihold_irun_reg is stubbed rather than
// porting the TMC current<->CS register math — nothing in the linked control
// path reads its result.
int clamp_i(int value, int lo, int hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

float clamp_f(float value, float lo, float hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

int lane_to_idx(int lane) {
    return (lane == 1) ? 0 : 1;
}

float sps_to_mm_per_min(int sps) {
    return (float)sps * g_mm_per_step[0] * 60.0f + 0.05f;
}

int mm_per_min_to_sps(float mm_per_min) {
    return (int)(mm_per_min / 60.0f / g_mm_per_step[0] + 0.5f);
}

uint32_t build_ihold_irun_reg(int run_ma, int hold_ma, bool vsense) {
    (void)run_ma;
    (void)hold_ma;
    (void)vsense;
    return 0;
}

// ===================== main.c callbacks =====================
// Copied verbatim from firmware/src/main.c — main.c cannot be linked (owns
// main(), GPIO/PIO init). Pure accessors; safe by inspection.
lane_t *lane_ptr(int lane) {
    if (lane == 1)
        return &g_lane_l1;
    if (lane == 2)
        return &g_lane_l2;
    return NULL;
}

int other_lane(int lane) {
    return (lane == 1) ? 2 : 1;
}

void set_active_lane(int lane) {
    if (g_active_lane != lane && (g_active_lane == 1 || g_active_lane == 2) &&
        (lane == 1 || lane == 2)) {
        int old_idx = g_active_lane - 1;
        int new_idx = lane - 1;
        if (g_mm_per_step[new_idx] > 1e-6f) {
            g_extruder_est_sps = g_extruder_est_sps * (g_mm_per_step[old_idx] / g_mm_per_step[new_idx]);
        }
    }
    g_active_lane = lane;
    if (lane == 1 || lane == 2) {
        char lane_s[2];
        lane_id_str(lane_s, lane);
        cmd_event("ACTIVE", lane_s);
    } else {
        cmd_event("ACTIVE", "NONE");
    }
}

// Carries real sync enable/disable policy (main.c:319) — scenarios exercising
// runout/RELOAD depend on this, so it is not a stub. Keep in sync with main.c
// by hand; nothing automated guards this copy.
void set_toolhead_filament(bool present) {
    g_toolhead_has_filament = present;
    if (!g_auto_mode) {
        if (present)
            sync_set_state(SYNC_ACTIVE);
        else
            sync_disable(false);
        if (!present) {
            g_sync_current_sps = 0;
            g_sync_auto_started = false;
            g_sync_tail_assist_active = false;
            g_sync_idle_since_ms = 0;
        }
    }
}

// ===================== protocol.c callbacks =====================
// protocol.c is USB/host-command wire format — out of scope (proposal.md
// Scope). cmd_event/cmd_event_critical are captured into the trace instead of
// formatted onto USB; manual_unload_active always reports false (no host
// MANUAL_UNLOAD command exists in the sim).
#define SIM_EVENT_MAX 64
sim_event_t g_sim_events[SIM_EVENT_MAX];
int g_sim_event_count;

static void sim_record_event(const char *type, const char *data, bool critical) {
    if (g_sim_event_count >= SIM_EVENT_MAX)
        return;
    sim_event_t *e = &g_sim_events[g_sim_event_count++];
    snprintf(e->text, sizeof(e->text), "%s,%s", type, data ? data : "");
    e->critical = critical;
}

void cmd_event(const char *type, const char *data) {
    sim_record_event(type, data, false);
}

void cmd_event_critical(const char *type, const char *data) {
    sim_record_event(type, data, true);
}

bool manual_unload_active(void) {
    return false;
}
