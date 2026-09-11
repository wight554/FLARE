## Why

FLARE currently lacks seamless support for a local filament bypass. While basic host-side mocking exists, slicer toolchanges (e.g. transitioning to tool `-2` for bypass) trigger G-code validation crashes, telemetry gets out of sync because the daemon syncer is bypass-unaware, and the standalone WebUI has no bypass card or controls. Furthermore, we need to completely bypass any physical MMU motor movements when in bypass mode, automate loading directly upon inserting filament into the toolhead sensor, and eliminate unnecessary "eject" procedures.

## What Changes

- **Bypass Active State**: Introduce a unified `bypass` boolean and status field across the Klipper mock, host daemon telemetry, and standalone WebUI, mapping to Happy Hare's gate/tool sentinel `-2`.
- **MMU-Free Bypass Load & Unload**: Force all `MMU_LOAD` and `MMU_UNLOAD` commands to bypass MMU lane motors entirely when bypassed, relying strictly on extruder-only logic (hotend tip-forming, gear grab/retract, and meltzone push).
- **Auto-Load on Sensor Insert**: Automate toolhead filament loading when in bypass mode by triggering `MMU_LOAD` immediately upon a `toolhead_sensor` insertion edge (TS:1 transition).
- **Suppressed Eject**: Suppress all MMU lane eject movements (`MMU_EJECT` / `FLARE_EJECT`) when in bypass mode, printing a clean notification and skipping serial execution.
- **Bypass Card in WebUI**: Add a premium "Filament Bypass" card to the standalone WebUI lane selector, allowing Klipper-free disengagement of the MMU and driving extruder-only load/unload tasks.
- **Robust Slicer Toolchange Support**: Fix `MMU_CHANGE_TOOL` to correctly recognize and allow transitioning to gate/tool index `-2` (Bypass) rather than throwing index errors.

## Capabilities

### New Capabilities
- `filament-bypass`: Complete local filament bypass disengagement support allowing MMU-free direct toolhead printing, auto-loading on toolhead sensor insertion, and silent ejection bypasses.

### Modified Capabilities
- `klipper-integration`: Extend Klipper extras to support automatic G-code execution on toolhead sensor insertion under bypass mode and robust toolchange sentinel handling.

## Impact

- `klipper/mmu.py`: Modify `cmd_MMU_CHANGE_TOOL`, `cmd_MMU_EJECT`, `cmd_SET_MMU`, and `get_status` to support the bypass sentinel `-2`, export `bypass` status, and safely skip MMU serial transactions.
- `klipper/flare_mmu.cfg`: Update `_FLARE_ON_TOOLHEAD_INSERT` macro to check for bypass and trigger `MMU_LOAD` automatically.
- `scripts/flare_daemon.py`: Add `bypass` status caching and prevent synchronization feedback loops with Klipper's state.
- `scripts/webui/`: Add the virtual Filament Bypass card, style it using outfit typography / glassmorphism, and gate control button enables/actions.
