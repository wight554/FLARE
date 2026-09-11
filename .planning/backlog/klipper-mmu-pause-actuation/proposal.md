# Proposal: Klipper MMU Pause Command Actuation

## Why
In `klipper/mmu.py`, `cmd_MMU_PAUSE` is currently an empty stub (`def cmd_MMU_PAUSE(self, gcmd): pass`). Clicking "Pause" in Mainsail/Fluidd MMU panels does not pause active filament feeds or abort ongoing toolchanges, relying solely on local MCU sensor interlocks.

## What Changes
- Wire `cmd_MMU_PAUSE` in `klipper/mmu.py` to dispatch an emergency pause/stop command (`PA` or `STOP`) to the FLARE controller via `flare_cmd.py` or the daemon.
- Ensure MCU acknowledges and enters a safe hold state without losing position tracking.

## Impact
- `klipper/mmu.py`
- `scripts/flare_cmd.py`
