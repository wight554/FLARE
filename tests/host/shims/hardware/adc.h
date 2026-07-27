#pragma once
/* Host shim for hardware/adc.h. Only sync_buf.c calls these two, at
 * sync_buf.c:381/384. Real behavior (mapping simulated buffer slack to ADC
 * counts) lives in tests/host/sim_fakes.c. */

#include <stdint.h>

void adc_select_input(uint32_t input);
uint16_t adc_read(void);
