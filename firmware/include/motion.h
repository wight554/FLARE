#pragma once

#include "controller_shared.h"

void debounced_input_init(debounced_input_t *d, uint pin);
void debounced_input_update(debounced_input_t *d);

void motor_init(motor_t *motor, uint en_pin, uint dir_pin, uint step_pin, bool dir_invert);
void motor_enable(motor_t *motor, bool on);
void motor_set_dir(motor_t *motor, bool forward);
void motor_set_rate_sps(motor_t *motor, int sps);
void motor_stop(motor_t *motor);
int motion_clamp_rate_sps(int sps);
void motion_limit_runtime_rates(bool refresh_active_motors);

void lane_setup(lane_t *lane, uint pin_in, uint pin_out, motor_t m, int lane_id, tmc_t *tmc);
void lane_start(lane_t *lane, task_t t, int sps, bool forward, uint32_t now_ms, float limit_mm);
void lane_stop(lane_t *lane);
void lane_tick(lane_t *lane, uint32_t now_ms);
void lane_fault(lane_t *lane, fault_t f);
void stop_all(void);