#pragma once
/* Host shim for hardware/flash.h. Backs the RAM-backed flash fake in
 * tests/host/sim_fakes.c so settings_store.c's save/load round trip runs as
 * real firmware code. */

#include <stddef.h>
#include <stdint.h>

#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u

void flash_range_erase(uint32_t flash_offs, size_t count);
void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count);
