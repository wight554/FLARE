# Tasks: MMU_LOAD Auto-Unload & Lane Switch

Track development tasks and verification steps.

## Phase 1: Implement Auto-Unload inside MMU_LOAD
- [x] 1.1 Edit `klipper/mmu.py` `cmd_MMU_LOAD` to replace the error raise with running `_FLARE_CHANGE_LANE LANE={lane}` if `other_gate` is loaded.
- [x] 1.2 Validate Python syntax using `python3 -m py_compile klipper/mmu.py`.

## Phase 2: Web UI Confirmation Dialogs
- [x] 2.1 Edit `scripts/webui/app.js` to store `lastTelemetryData`, implement `loadActiveLane()` and `unloadActiveLane()`, and update `ejectActiveLane()` to prompt with confirmation dialogs if a lane is loaded in the toolhead.
- [x] 2.2 Edit `scripts/webui/index.html` to change the click handlers for Load and Unload.
- [x] 2.3 Verify javascript syntax and visual rendering.
