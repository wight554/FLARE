# Design: Double-Load Prevention & Manual Recovery Hardening

Detailed design, C firmware variables, Klipper properties, state machine interactions, and regression constraints.

## 1. Codebase Analysis & State Indicators

### 1.1 Y-Splitter Combiner Sensor
- **Variable**: `g_y_split` (type `din_t`).
- **Accessor**: `on_al(&g_y_split)` returns true if filament is present at the Y-splitter/combiner (GP6 pin).

### 1.2 Lane Sensors
- **IN (pre-gate) sensor**: `lane_in_present(lane_t *L)` checks if filament is detected at gate entrance.
- **OUT (gate/gear) sensor**: `lane_out_present(lane_t *L)` checks if filament is detected past the selector/extruder feed point.

### 1.3 Active & Other Lane Pointers
- **Active Lane**: Obtained via `get_active_lane_and_clear_error()` or `lane_ptr(active_lane)`.
- **Other Lane**: Obtained via `lane_ptr(other_lane(active_lane))`.

### 1.4 Manual Unload State Machine
- **State**: `g_manual_unload.state` (runs `manual_unload_tick` in `firmware/src/protocol.c`).
- **Target Lane**: `g_manual_unload.lane`.
- **Flags**: `g_manual_unload.cut_pending` determines if a cutter sequence should trigger when the filament clears `OUT`. `g_manual_unload.finish_to_in` determines if the filament should be fully retracted to the `IN` sensor.

---

## 2. Detailed Technical Design

### 2.1 Firmware Double-Load Blocking
We block any automated or manual load commands if the Y-splitter combiner contains another filament:
1. **Toolchange (`firmware/src/toolchange.c`)**:
   - In `TC_LOAD_START` state:
     ```c
     case TC_LOAD_START: {
         if (on_al(&g_y_split)) {
             tc_enter_error("HUB_NOT_CLEAR");
             break;
         }
     ```
     By removing `TC_TIMEOUT_Y_MS > 0 &&`, we guarantee this check runs unconditionally.
2. **Manual / Full Load (`firmware/src/protocol.c`)**:
   - In `FL` and `RL` handlers:
     ```c
     if (on_al(&g_y_split) && !lane_out_present(A)) {
         cmd_reply("ER", "OTHER_LANE_ACTIVE");
         return;
     }
     ```
     This rejects load commands with `OTHER_LANE_ACTIVE` if the Y-splitter is occupied by any other lane's filament (since the target lane is not yet past its `OUT` sensor).

### 2.2 Manual Recovery & Cutter Bypass
During a manual recovery unload (`UL:` or `UM:`):
1. The user selects the lane to recover using `T:1` or `T:2`.
2. The user executes the unload command. The active lane `A` retracts past `OUT`.
3. In `manual_unload_tick` at `MANUAL_UNLOAD_WAIT_FIRST_CLEAR` state:
   - When `A->task == TASK_IDLE` and `!lane_out_present(A)`:
     ```c
     if (g_manual_unload.cut_pending) {
         if (on_al(&g_y_split)) {
             // Combiner is still occupied by the other lane's filament.
             // Bypass cutting completely to prevent damage.
             g_manual_unload.cut_pending = false;
             if (g_manual_unload.finish_to_in) {
                 start_manual_unload_to_in(A, now_ms);
                 g_manual_unload.state = MANUAL_UNLOAD_WAIT_IN_CLEAR;
             } else {
                 manual_unload_reset();
             }
         } else {
             cutter_start(A, true, now_ms);
             g_manual_unload.state = MANUAL_UNLOAD_WAIT_CUT;
         }
     }
     ```

### 2.3 Klipper Host-Side Safety Hardening
1. **Safety Checks**: In `cmd_MMU_LOAD` (`klipper/mmu.py`), before sending `FLARE_LOAD`:
   - Raise `gcmd.error` if `self.gate_status[other_gate] == 2` (other lane is loaded).
   - Raise `gcmd.error` if `self.hub_sensor_active == 1` and `self.gate_sensor_active == 0` (Y-splitter is occupied by another lane).
2. **Synchronous Waiting**: In `cmd_MMU_LOAD`, after triggering `FLARE_LOAD`:
   - Non-blockingly poll `self.gate_sensor_active` for up to 15 seconds.
   - If it triggers, proceed to `_FLARE_POST_TC_LOAD`.
   - If it times out, raise `gcmd.error` to halt the print and prevent extruder gear grinds.

Generated-By: Antigravity (Gemini 3.1 Pro)
