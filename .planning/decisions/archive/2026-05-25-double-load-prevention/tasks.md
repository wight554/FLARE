# Tasks: Double-Load Prevention & Manual Recovery Hardening

Track development tasks and verification steps.

## Phase 29: Firmware Double-Load Blocking & Y-Splitter Hardening
- [x] 29.1 Edit `firmware/src/toolchange.c` to check `on_al(&g_y_split)` unconditionally in `TC_LOAD_START` state.
- [x] 29.2 Edit `firmware/src/protocol.c` `FL` and `RL` command handlers to reject loading if `on_al(&g_y_split) && !lane_out_present(A)` is true.
- [x] 29.3 Run `ninja -C build_local` to verify clean compilation.

## Phase 30: Manual Unload Cutter Bypass
- [x] 30.1 Edit `firmware/src/protocol.c` in `manual_unload_tick` (`MANUAL_UNLOAD_WAIT_FIRST_CLEAR` state) to check `on_al(&g_y_split)` before triggering cutter. Clear `cut_pending = false` and skip cutter start if combiner is occupied.
- [x] 30.2 Run `ninja -C build_local` to verify firmware compiles successfully.

## Phase 31: Klipper Safety Verification & Wait States
- [x] 31.1 Edit `klipper/mmu.py` `cmd_MMU_LOAD` to add robust checks verifying other lane is not loaded and Y-splitter is not occupied.
- [x] 31.2 Edit `klipper/mmu.py` `cmd_MMU_LOAD` to poll `self.gate_sensor_active` for up to 15s before launching `_FLARE_POST_TC_LOAD`.
- [x] 31.3 Verify python syntax and style using `python3 -m py_compile klipper/mmu.py`.

## Phase 32: T: Handler Double-Load Recovery Hardening
- [x] 32.1 Add double-load guard to firmware `T:` command handler in `protocol.c`: when both `lane_out_present(&g_lane_l1)` and `lane_out_present(&g_lane_l2)` are true, call `tc_abort()` + `sync_set_state(SYNC_OFF)` before `set_active_lane(ln)` to kill any in-flight TC state machine and prevent auto-loading from resuming via `tc_tick` once a valid active_lane is set.
- [x] 32.2 Add `FLARE_SELECT LANE=X` macro to `klipper/flare_mmu.cfg`: bare `T:lane` firmware command with no loading sequence, for explicit recovery use.
- [x] 32.3 Guard `_FLARE_CHANGE_LANE` in `klipper/flare_mmu.cfg`: check `_FLARE_STATE.out1 AND out2`; if double-load detected, emit error message and skip full toolchange, instructing user to use `FLARE_SELECT` + `FLARE_UNLOAD`.
- [x] 32.4 Keep `mmu.py` `cmd_MMU_SELECT` clean — `_FLARE_CHANGE_LANE` guard already covers `MMU_SELECT TOOL=X` path; no redundant check needed at Python level.
- [x] 32.5 Run `ninja -C build_local` to verify clean compilation.

### Validation Notes — 2026-05-23 (T: Handler Double-Load Recovery)
- **Root cause**: In double-load state with `RELOAD_MODE=1`, a non-IDLE TC state machine (e.g. `TC_RELOAD_WAIT_Y`) could silently activate once `set_active_lane` gives it a valid `A` pointer. The `T:` handler's `sync_retract_assist_set(false)` also had subtle sync state side effects. Both paths skipped when double-load is detected.
- **TC abort path**: `tc_abort()` stops all motors, resets TC state to IDLE, and aborts cutter. `toolhead_has_filament` is cleared as a side effect (acceptable in recovery; daemon will re-sync on next status poll). `cmd_event("TC:ERROR", "ABORTED")` is emitted as a diagnostic.
- **Recovery flow**: `T:1` or `T:2` → selects lane (no load), then `UL:` or `FLARE_UNLOAD` clears it. `FLARE_SELECT LANE=X` in Klipper provides the same safe path from the UI.
- **Firmware build**: Clean, no warnings.


- **Firmware Double-Load Blocking**: Unconditionally checking Y-splitter state at the start of automated toolchange load state (`TC_LOAD_START`) and manual load/reload commands (`FL`, `RL`), returning `ER:OTHER_LANE_ACTIVE` or `HUB_NOT_CLEAR` immediately if occupied, protecting hardware from colliding filament feeds.
- **Manual Recovery Selection & Unloads**: Confirmed that since selector command `T:X` changes the active lane without initiating any load moves, users can safely select any target lane and run a manual recovery unload (`UM` or `UL`).
- **Conditional Cutter Bypass**: Inside the manual unload machine `MANUAL_UNLOAD_WAIT_FIRST_CLEAR` state, if the Y-splitter/combiner remains triggered (`on_al(&g_y_split)`) after the target filament clears its `OUT` sensor, we clear `cut_pending = false` and skip calling `cutter_start`, avoiding cutting the other lane's active filament and moving directly to a clean retraction to `IN` clear.
- **Host-Side Load Hardening**: Added robust verification to `klipper/mmu.py` `cmd_MMU_LOAD` preventing command execution if another lane is loaded or if the Y-splitter combiner is occupied. Inserted a non-blocking 15-second polling loop to wait synchronously for the filament to reach the `OUT` sensor (`self.gate_sensor_active == 1`) before executing the `_FLARE_POST_TC_LOAD` hotend handoff.
- **Compilation & Syntax**: Both C/C++ RP2040 firmware and Python Klipper module compile successfully and clean of any errors.

Generated-By: Antigravity (Gemini 3.1 Pro)

