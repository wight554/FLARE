#pragma once
/* Host shim for pico/flash.h. settings_store.c reads flash via
 * XIP_BASE + offset and computes an erase offset from PICO_FLASH_SIZE_BYTES.
 * flash_safe_execute is declared for header completeness; nothing in the
 * linked sources calls it (verified: settings_store.c only reads/erases/
 * programs directly). */

#include <stdint.h>

#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
extern uint8_t g_sim_flash[PICO_FLASH_SIZE_BYTES];
#define XIP_BASE ((uintptr_t)g_sim_flash)

typedef void (*flash_safe_execute_func)(void *param);
int flash_safe_execute(flash_safe_execute_func func, void *param, uint32_t timeout_ms);
