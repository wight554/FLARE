#pragma once
/* Host shim for hardware/gpio.h. motion.c and cutter.c drive real motor/servo
 * logic against these calls; tests/host/sim_fakes.c records pin state into a
 * small pin table that sim_plant.c reads back (e.g. motor direction via
 * motor_t.dir_pin) since motor_t itself stores no direction field. */

#include <stdbool.h>
#include "pico/stdlib.h"

enum {
    GPIO_IN = 0,
    GPIO_OUT = 1,
    GPIO_FUNC_PWM = 4,
};

void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool out);
void gpio_put(uint pin, bool value);
bool gpio_get(uint pin);
void gpio_pull_up(uint pin);
void gpio_pull_down(uint pin);
void gpio_disable_pulls(uint pin);
void gpio_set_function(uint pin, uint fn);
