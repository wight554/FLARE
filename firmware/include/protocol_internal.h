#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "controller_shared.h"

extern char g_marker_tag[MARKER_TAG_LEN];
extern uint16_t g_marker_seq;
extern bool g_live_tune_lock;

void cmd_handle_status_dump(void);
bool cmd_handle_tmc_advanced(const char *cmd, const char *p, uint32_t now_ms);
