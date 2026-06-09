#pragma once

#include "controller_shared.h"
#include <stdbool.h>
#include <stdint.h>

void cutter_init(void);
bool cutter_busy(void);
bool cutter_failed(void);
uint32_t cutter_expected_ms(lane_t *lane, bool enable_feed);
void cutter_start(lane_t *lane, bool enable_feed, uint32_t now_ms);
void cutter_abort(void);
void cutter_tick(uint32_t now_ms);
bool cutter_test_us(uint32_t us);
