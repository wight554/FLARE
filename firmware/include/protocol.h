#pragma once

#include <stdint.h>

#define CMD_LINE_MAX 768

void cmd_reply(const char *status, const char *data);
void cmd_event(const char *type, const char *data);
void cmd_event_critical(const char *type, const char *data);
void cmd_poll(uint32_t now_ms);
bool manual_unload_active(void);
