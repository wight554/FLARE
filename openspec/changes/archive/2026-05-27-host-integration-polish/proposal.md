## Why

FLARE's Klipper integration is functional but suffers from severe console spam, lack of complete command parity with the standard Happy Hare Mainsail/Fluidd MMU widget, and dynamic color syncing gaps.

1. **Console Spam**: The daemon polls state at 4Hz and pushes 13 `SET_GCODE_VARIABLE` updates to Klipper for `_FLARE_STATE` plus 1 `SET_MMU` update on every tick. High-frequency floats (`g_buf_pos`, `sps`) constantly change due to minor ADC noise, triggering G-code console history prints continually.
2. **Missing Commands**: Fluidd's MMU widget buttons and standard Klipper macros call commands like `MMU_STATUS`, `MMU_HOME`, `MMU_UNLOCK`, `MMU_PAUSE`, and `MMU_RESET`. These are missing in `mmu.py`, causing "Unknown command" crashes or errors.
3. **RGB Sync Gap**: `SET_MMU` updates the string representation of `GATE_COLOR`, but fails to recompute `gate_color_rgb` / `tool_color_rgb`. As a result, dashboard gate and spool colors do not update dynamically on changes.

## What Changes

- **Spam Elimination**:
  - Remove the unused `_FLARE_STATE` macro from `klipper/flare_mmu.cfg`.
  - Remove all 13 `SET_GCODE_VARIABLE` calls from `scripts/flare_daemon.py`.
  - Remove float variables `g_buf_pos` and `sps` from the daemon's change-detection keys. Round their values to 1 decimal place before sync so minor ADC drift is ignored.
- **Parity Commands**:
  - Register `MMU_STATUS` in `klipper/mmu.py` to dump the current status to the console.
  - Register stubs for `MMU_HOME`, `MMU_UNLOCK`, `MMU_PAUSE`, and `MMU_RESET` to satisfy Fluidd buttons without causing errors.
- **Dynamic RGB Sync**:
  - Update `cmd_SET_MMU` in `klipper/mmu.py` to automatically parse hexadecimal values from `GATE_COLOR` updates and re-calculate `self.gate_color_rgb` so frontend colors sync immediately.

## Capabilities

### Modified Capabilities
- `klipper-integration`: `mmu.py` gets complete command parity with Happy Hare widget stubs (`MMU_STATUS`, `MMU_HOME`, etc.) and handles dynamic RGB conversions on `SET_MMU`. `flare_mmu.cfg` retires the unused `_FLARE_STATE` variable mirror.
- `daemon-sync`: `flare_daemon.py` klipper syncer is optimized to exclude float jitter from change-detection and stop G-code variable mirror spam.

## Impact

- `klipper/mmu.py`: register missing stubs + `MMU_STATUS` handler; implement hex-to-RGB conversion inside `cmd_SET_MMU`.
- `scripts/flare_daemon.py`: strip `_FLARE_STATE` updates; filter float noise from change-detection.
- `klipper/flare_mmu.cfg`: delete `_FLARE_STATE` macro.
- No firmware changes required.
