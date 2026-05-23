# Proposal: MMU_LOAD Auto-Unload & Lane Switch

## Problem
In Klipper MMU workflow, when user/UI selects new gate (`MMU_SELECT GATE=1`) while other gate is loaded (gate 0), subsequent `MMU_LOAD` raises error: `FLARE: Cannot load gate 1 - gate 0 is currently loaded.`
Forces manual intervention or explicit macros.

In Web UI, clicking "Load", "Unload", or "Eject" triggers direct raw commands to the board (`FL:`, `UL:`, `UM:`). If the lane is currently loaded in the toolhead and the user triggers these without first ensuring the toolhead gears/extruder are clear or Klipper is coordinating, it can cause gear grinds, filament jams, or hardware damage.

## Solution
1. **Klipper Host**: Make `MMU_LOAD` smart. If target gate to load is requested, but other gate is currently loaded, automatically run `_FLARE_CHANGE_LANE LANE=<lane>` instead of raising error. This unloads old lane, selects new lane, loads to toolhead, and syncs gears automatically.
2. **Web UI**: In the web dashboard, add safety confirmation dialogs:
   - For **Load**: If the other lane is loaded to the toolhead, prompt: `"Make sure you unloaded the current lane from the toolhead. Proceed?"`
   - For **Unload / Eject**: If the active lane is loaded to the toolhead, prompt: `"Make sure you unloaded the current lane from the toolhead. Proceed?"`

## Scope
- `klipper/mmu.py`: Modify `cmd_MMU_LOAD` to check if other gate is loaded (`self.gate_status[other_gate] == 2`). If loaded, execute `_FLARE_CHANGE_LANE LANE=<lane>` and return.
- `scripts/webui/app.js`: Implement `loadActiveLane()`, `unloadActiveLane()`, and update `ejectActiveLane()` to prompt with confirmation dialogs if a lane is loaded in the toolhead.
- `scripts/webui/index.html`: Update the Load and Unload buttons to invoke their respective helper functions (`loadActiveLane()` and `unloadActiveLane()`).
