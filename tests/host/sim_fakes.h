#pragma once
/// @brief Shared state between sim_fakes.c and the rest of tests/host/: the
///        captured-event buffer (drained by sim_trace.c each tick) and the
///        type-P ADC counts sim_plant.c writes.

#include <stdbool.h>
#include <stdint.h>

#define SIM_EVENT_TEXT_LEN 96

typedef struct {
    char text[SIM_EVENT_TEXT_LEN]; // "TYPE,DATA"
    bool critical;
} sim_event_t;

#define SIM_EVENT_MAX 64
extern sim_event_t g_sim_events[SIM_EVENT_MAX];
extern int g_sim_event_count;

extern uint16_t g_sim_adc_counts;
extern uint32_t g_sim_sleep_ms_total; // accumulated blocking sleep_ms() from linked sources
extern uint8_t g_sim_flash[];

// Motor-level commanded rate, decoded from the PWM fake's clkdiv/wrap — see
// sim_fakes.c. Needed for BL prime/lock/catch, which drives motor_set_rate_sps()
// directly and never touches lane_t.current_sps.
float sim_motor_rate_sps(unsigned int slice);
// Raw PWM slice state (servo tests): enabled flag and last channel level.
bool sim_pwm_enabled(unsigned int slice);
uint16_t sim_pwm_level(unsigned int slice);

void sim_tmc_inject_brownout(int lane);
void sim_tmc_set_comm_fail(int lane, bool fail);
int sim_tmc_get_read_count(int lane);
int sim_tmc_get_write_count(int lane);
int sim_tmc_get_run_current_ma(int lane);
void sim_tmc_reset_counts(void);
