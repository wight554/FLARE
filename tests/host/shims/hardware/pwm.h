#pragma once
/* Host shim for hardware/pwm.h. motion.c derives a step rate from
 * clkdiv/wrap/chan_level; tests/host/sim_fakes.c records the last commanded
 * level per (slice, channel) so sim_plant.c can read it back through
 * motor_t.slice / motor_t.chan without duplicating motion.c's sps<->PWM math. */

#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h" /* real hardware/pwm.h pulls this in transitively;
                             * cutter.c relies on it for GPIO_FUNC_PWM/gpio_set_function
                             * without including hardware/gpio.h itself. */

typedef struct {
    float clkdiv;
    uint16_t wrap;
} pwm_config;

pwm_config pwm_get_default_config(void);
void pwm_config_set_clkdiv(pwm_config *c, float div);
void pwm_config_set_wrap(pwm_config *c, uint16_t wrap);
uint pwm_gpio_to_slice_num(uint gpio);
uint pwm_gpio_to_channel(uint gpio);
void pwm_init(uint slice, pwm_config *c, bool start);
void pwm_set_enabled(uint slice, bool enabled);
void pwm_set_clkdiv(uint slice, float div);
void pwm_set_wrap(uint slice, uint16_t wrap);
void pwm_set_chan_level(uint slice, uint chan, uint16_t level);
void pwm_set_gpio_level(uint gpio, uint16_t level);
