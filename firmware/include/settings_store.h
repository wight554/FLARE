#pragma once

#include <stdint.h>

extern int g_active_sector;
extern uint32_t g_seq;

void settings_defaults(void);
void settings_save(void);
void settings_load(void);
void sync_tmc_settings(int lane);
