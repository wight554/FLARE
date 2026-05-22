# Proposal: Double-Load Prevention & Manual Recovery Hardening

Prevent double-loading filament at both firmware and host levels, allow manual recovery unloads on either lane under jammed conditions, and bypass cutter activation when unloading if the Y-splitter combiner is still occupied by the other lane's filament.

## Problem Context
During a toolchange/select sequence, if a gate selection (`MMU_SELECT GATE=1`) occurs while the other lane is still loaded, and is followed by `MMU_LOAD`, Klipper blindly issues `FLARE_LOAD` then immediately triggers `_FLARE_POST_TC_LOAD`. If the firmware blocks the load or is in a jammed state, the host still executes the hotend extruder grab and reload sequence, causing extruder grinds or severe hardware damage. Furthermore, if a user enters a double-loaded state, they need to be able to select and manually unload either lane safely, without triggering the cutter when retracting the non-loaded lane since its filament is already clear of the cutter and the other lane's filament is occupying the extruder/combiner.

## Objectives
1. **Firmware Double-Load Blocking**: Prevent `FL` (Full Load) and `RL` (Reload) in `firmware/src/protocol.c` and `TC_LOAD_START` in `firmware/src/toolchange.c` if the Y-splitter sensor (`on_al(&g_y_split)`) is triggered, unless the target lane is already past its `OUT` sensor (which indicates it's the one occupying the combiner).
2. **Klipper Safety & Wait States**: Add robust pre-load checks in `klipper/mmu.py`'s `cmd_MMU_LOAD` to raise an error if another gate is loaded or if the Y-splitter is occupied. Add a polling loop inside `cmd_MMU_LOAD` that waits synchronously for the filament to reach the active lane's `OUT` sensor (`self.gate_sensor_active == 1`) before executing `_FLARE_POST_TC_LOAD`.
3. **Manual Recovery Selection & Unloads**: Allow manual unloads on either lane by using `T:X` to set the active lane, followed by `UM` / `UL`.
4. **Conditional Cutter Bypass**: Inside the manual unload tick machine, if `g_manual_unload.cut_pending` is true, check if the Y-splitter sensor (`on_al(&g_y_split)`) remains triggered when the target lane clears the `OUT` sensor. If it is still triggered, clear `cut_pending = false` and bypass the cutter start, moving straight to finishing the unload or retracting to `IN` clear.

Generated-By: Antigravity (Gemini 3.1 Pro)
