# Proposal: Flash Ping-Pong Atomic Persistence

## Why
Currently in `firmware/src/settings_store.c`, settings persist to a single fixed 4 KB flash sector (`SETTINGS_FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE`). Every `settings_save()` call erases the sector before writing. If power drops mid-erase or mid-write, the sector is corrupted/blank, and `settings_load()` resets all operator calibration back to factory defaults.

## What Changes
- Allocate two adjacent 4 KB flash sectors (`SECTOR_A` and `SECTOR_B`).
- Implement an atomic commit structure with a monotonically increasing sequence counter and magic/CRC32 verification.
- Write sequence: write new settings into the inactive sector, verify CRC32, stamp active flag, and only then obsolete the old sector.
- Power drops during erase/write will leave the previous valid sector intact, guaranteeing zero calibration loss across unexpected power outages.

## Impact
- `firmware/src/settings_store.c`
- Flash layout changes (allocates 8 KB instead of 4 KB at top of flash)
- Eliminates single-sector wear concentration by 50%
