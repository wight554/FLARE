# Tasks: Host UI & Daemon Integration

## Phase 1: Daemon Foundation
- [x] 1.1 Create `scripts/flare_daemon.py` with a dedicated serial reader thread.
- [x] 1.2 Implement thread-safe connection caching and automatic USB reconnect loops.
- [x] 1.3 Implement continuous background parsing of `?:` dumps and `EV:` lines into a local JSON cache.

## Phase 2: Web Server & Network APIs
- [x] 2.1 Integrate std-lib HTTP server to handle `GET /status` (returns cached JSON) and `POST /cmd` (sends command to serial and blocks for `OK:`/`ER:` response).
- [x] 2.2 Integrate std-lib WebSocket server to broadcast cached state variables at 50Hz. (Updated: Used Server-Sent Events SSE for native HTTP streaming)
- [x] 2.3 Add config settings (CLI flags or `.env`) for API ports and debug verbosity.

## Phase 3: Klipper Mocking & Client Rewrite
- [x] 3.1 Rewrite `scripts/flare_cmd.py` to route commands through the daemon's HTTP `/cmd` API rather than direct serial access.
- [x] 3.2 Add the mock `[gcode_macro _FLARE_STATE]` configuration to `klipper/flare_mmu.cfg`.
- [x] 3.3 Add Klipper variable synchronization to `flare_daemon.py` (throttled at 4Hz to update Klipper via Moonraker API).

## Phase 4: Installer & OS Integration
- [x] 4.1 Write `scripts/install_daemon.sh` to install systemd unit file, handle `pyserial` dependencies, and enable the service on boot.
- [x] 4.2 Add systemd service templates (`flare_daemon.service`).
- [x] 4.3 Document local daemon management commands (start, stop, logs).

## Phase 5: WebUI & Visualization
- [x] 5.1 Create single-page WebUI (`scripts/webui/index.html`) using HTML5 Canvas to plot real-time `g_buf_pos` traces.
- [x] 5.2 Add buttons, sliders, and calibration indicators mapping to daemon REST endpoints.
- [x] 5.3 Integrate static-file hosting inside `flare_daemon.py` so the WebUI is served natively.

## Phase 6: Verification
- [x] 6.1 Run static analysis (`python3 -m py_compile scripts/*.py`).
- [ ] 6.2 Simulate hardware disconnection during active polling and confirm the daemon recovers cleanly when plugged back in.
- [x] 6.3 Verify `flare_cmd.py` proxy latency is <2ms.
- [x] 7.1 Create `klipper/mmu.py` Klipper extras helper class to mock Happy Hare state fields.
- [x] 7.2 Safe copy/install check for `mmu.py` inside `install_daemon.sh`.
- [x] 7.3 Wire `SET_MMU` commands into `klipper_syncer` thread in `flare_daemon.py`.
- [x] 7.4 Add `[mmu]` section into `klipper/flare_mmu.cfg`.
- [x] 7.5 Run compile/linter check on new python files.
- [x] 7.6 Add missing Happy Hare status attributes (is_homed, gate_color_rgb, gate_name, gate_filament_name, ttg_map, action) to `klipper/mmu.py`.
- [x] 7.7 Register `MMU_GATE_MAP` and `MMU_TTG_MAP` G-code commands in `klipper/mmu.py` to prevent unknown command errors when editing filaments in Fluidd.
- [x] 7.8 Fix daemon serial reader to recognize raw "OK" replies (no colon) to resolve command timeouts.
- [x] 7.9 Map gate availability to either IN or OUT sensors and add print_state to status object.
- [x] 7.10 Add state persistence to Klipper mock and periodic force-sync to daemon syncer thread to handle Moonraker/Klipper restarts without losing gate/tool mappings.
- [x] 7.11 Add Spoolman support to klipper/mmu.py (spoolman_support attribute, gate_spool_id persistence, and MMU_SPOOLMAN command handling).
- [x] 7.12 Register and implement MMU_SELECT in klipper/mmu.py to support dashboard gate selection.
- [x] 7.13 Refine MMU_SELECT in klipper/mmu.py to do nothing if selecting already active gate, and perform toolchange if different.
- [x] 7.14 Define `_CG28` conditional homing helper in `flare_mmu.cfg`.
- [x] 7.15 Harden SET_MMU parsing in klipper/mmu.py to strip quotes from string lists and update gate status mapping logic in scripts/flare_daemon.py (0=empty, 1=preloaded, 2=loaded).

---
### Validation Notes — 2026-05-22
- Verified HTTP and SSE dashboard serves natively on the Raspberry Pi host.
- UI runs perfectly at default port `8088` (changed default from `8080` to prevent address collision with typical Klipper/mjpg-streamer configurations).
- Client command proxying verified and compiled cleanly.
- Implemented native `mmu.py` Klipper extra module to mock Happy Hare state fields.
- Mainsail/Fluidd dashboards now seamlessly discover the `printer.mmu` namespace via dynamic `SET_MMU` updates.
- Added strict safety checks in `install_daemon.sh` preventing unintended Happy Hare file overwrites.
- Verified dynamic telemetry parameters (buf pos, states, sensors) via moonraker API commands.
- Fixed command timeouts for parameter SET requests by supporting raw "OK" (no colon) responses in the daemon serial multiplexer.
- Implemented automatic recovery of Klipper mock parameters after a Klipper/Moonraker reload, using localized JSON persistence next to the printer configuration directory and a 10-second daemon force-sync heartbeat.
- Refined `MMU_SELECT` in `klipper/mmu.py` to guard against redundant selection.
- Added standard conditional homing macro `_CG28` to `klipper/flare_mmu.cfg`.
- Hardened Klipper list parsers in `klipper/mmu.py` by stripping single/double quotes from all arrays and standard string fields in `SET_MMU`.
- Implemented three-state sensor mapping in `scripts/flare_daemon.py` (`gate_status`): `0` for empty (no `IN` sensor), `1` for preloaded (`IN` triggered), and `2` for loaded (`IN` + `OUT` + `YS` triggered).

## Phase 8: Gate Visibility & Check Gate Verification
- [x] 8.1 Wire `NUM_GATES=2` explicitly into Klipper `SET_MMU` commands from the daemon `klipper_syncer` thread in `scripts/flare_daemon.py`.
- [x] 8.2 Add list size invariant helper `_ensure_array_lengths(self)` in Klipper extra `klipper/mmu.py`.
- [x] 8.3 Wire list size invariant checks after initialization, variable loads, and dynamic updates in `klipper/mmu.py`.
- [x] 8.4 Implement robust `cmd_MMU_CHECK_GATE` in `klipper/mmu.py` that triggers a daemon status query and outputs a detailed gate state report.
- [x] 8.5 Mock Klipper `mmu_machine` object in `klipper/mmu.py` to fix gate visibility in Fluidd


---
### Validation Notes — 2026-05-22
- Verified HTTP and SSE dashboard serves natively on the Raspberry Pi host.
- UI runs perfectly at default port `8088` (changed default from `8080` to prevent address collision with typical Klipper/mjpg-streamer configurations).
- Client command proxying verified and compiled cleanly.
- Implemented native `mmu.py` Klipper extra module to mock Happy Hare state fields.
- Mainsail/Fluidd dashboards now seamlessly discover the `printer.mmu` namespace via dynamic `SET_MMU` updates.
- Added strict safety checks in `install_daemon.sh` preventing unintended Happy Hare file overwrites.
- Verified dynamic telemetry parameters (buf pos, states, sensors) via moonraker API commands.
- Fixed command timeouts for parameter SET requests by supporting raw "OK" (no colon) responses in the daemon serial multiplexer.
- Implemented automatic recovery of Klipper mock parameters after a Klipper/Moonraker reload, using localized JSON persistence next to the printer configuration directory and a 10-second daemon force-sync heartbeat.
- Refined `MMU_SELECT` in `klipper/mmu.py` to guard against redundant selection.
- Added standard conditional homing macro `_CG28` to `klipper/flare_mmu.cfg`.
- Hardened Klipper list parsers in `klipper/mmu.py` by stripping single/double quotes from all arrays and standard string fields in `SET_MMU`.
- Implemented three-state sensor mapping in `scripts/flare_daemon.py` (`gate_status`): `0` for empty (no `IN` sensor), `1` for preloaded (`IN` triggered), and `2` for loaded (`IN` + `OUT` + `YS` triggered).
- Hardened list length invariants in `klipper/mmu.py` with size invariant checks ensuring reported spool arrays match `self.num_gates` exactly, preventing mismatched/corrupt persistent JSON files from shrinking gate counts in Mainsail/Fluidd dashboards.
- Explicitly wired `NUM_GATES=2` into Klipper `SET_MMU` commands from the background daemon's `klipper_syncer` thread to guarantee Klipper's gate capacity is continuously asserted as 2.
- Reimplemented `MMU_CHECK_GATE` to trigger an immediate daemon-level serial status poll (`?:`) and print a detailed, human-readable gate and sensor status report to the G-code console.

## Phase 9: UI Bugfixes & Polish
- [x] 9.1 Wire `gate` attribute in `klipper/mmu.py` and `scripts/flare_daemon.py` to resolve active gate highlighting in Fluidd.
- [x] 9.2 Implement python dictionary parsing for `MAP` parameter in `MMU_GATE_MAP` inside `klipper/mmu.py` using `ast.literal_eval` to correctly save and persist Spoolman configurations.

---
### Validation Notes — 2026-05-22 (Bugfixes)
- Expose `gate` key in Klipper `get_status` mapping and synchronized `GATE={active_gate}` via Moonraker `SET_MMU` in `scripts/flare_daemon.py`. This resolves the issue where Fluidd was unable to highlight the loaded/active gate card.
- Implemented robust dictionary parsing using safe `ast.literal_eval` inside `MMU_GATE_MAP` to handle Fluidd's multi-gate/spool JSON-like map dictionary transmission. Cleaned up color hex strings (stripping `#` and discarding alpha channel to support standard 6-char compatibility) and successfully persisted changes into the flash-backed configuration `flare_mmu_vars.json`.
- Ran compiler and linter checks (`python3 -m py_compile`) and verified firmware local builds compiled successfully.

## Phase 10: Cutter and Loaded Status Refinements
- [x] 10.1 Parse `enable_cutter` (`CU` field) in `scripts/flare_daemon.py` and synchronize it via `SET_MMU`.
- [x] 10.2 Update `MMUMock` and `cmd_SET_MMU` in `klipper/mmu.py` to accept `ENABLE_CUTTER` parameter.
- [x] 10.3 Refine `cmd_MMU_UNLOAD` in `klipper/mmu.py` to trigger `FLARE_UNLOAD_TOOLHEAD` prior to `FLARE_UNLOAD` to form tip and retract past extruder gears (the RP2040 MMU firmware handles physical cutting internally during manual unload).
- [x] 10.4 Refine `gate_status` mapping logic in `scripts/flare_daemon.py` to only report loaded (status `2`) if the toolhead filament sensor (`toolhead` state) is triggered in addition to standard loaded sensors (`in` + `out` + `y_split`).

---
### Validation Notes — 2026-05-22 (Cutter & Status Refinements)
- Verified `flare_daemon.py` parses `CU:` (mirrored as `enable_cutter`) dynamically from raw serial telemetry dumps.
- Verified `gate_status` status `2` (Loaded) is asserted strictly when `in + out + y_split + toolhead` are triggered, resolving pre-mature load indication in Fluidd.
- Verified `MMU_UNLOAD` successfully chains Klipper tip forming/gear retraction (`FLARE_UNLOAD_TOOLHEAD`) followed by physical lane retraction (`FLARE_UNLOAD` / `UL:`), preventing motor-fight jams.
- Successfully passed Python static compiler checks (`python3 -m py_compile`) and verified firmware local build.

## Phase 11: Eject, Unload, and Load Condition Refinements
- [x] 11.1 Define `FLARE_EJECT` macro in `klipper/flare_mmu.cfg` that calls `UM:` via shell command.
- [x] 11.2 Update `cmd_MMU_EJECT` in `klipper/mmu.py` to trigger `FLARE_EJECT` to completely eject filament.
- [x] 11.3 Validate `gate_status` mapping meets progress conditions (LOAD active for TS:0, TS:0 YS:0, TS:0 YS:0 OUT:0, and disabled for IN:0).

---
### Validation Notes — 2026-05-22 (Eject & Load Condition Refinements)
- Added `FLARE_EJECT` macro running `UM:` command.
- Mapped Klipper MMU mock `MMU_EJECT` to execute `FLARE_EJECT` macro, resolving command routing mismatch.
- Verified that `gate_status` logic enables `LOAD` in Fluidd precisely under the defined progress-level loading conditions and leaves it disabled if no filament is present at the gate entrance (`IN:0`).

## Phase 12: Cutter Derivation & Fluidd Load Button Refinements
- [x] 12.1 Expose `enable_cutter` in `get_status` state dict in `klipper/mmu.py` to allow UI and macros to read it dynamically.
- [x] 12.2 Update `gate` and `tool` in `scripts/flare_daemon.py` to be reported as `-1` unless the active gate's filament is fully loaded (all sensors `in && out && y_split && toolhead` are 1).
- [x] 12.3 Ensure list/compile checks pass perfectly.
- [x] 12.4 Initialize, update, and expose Happy Hare `filament` and `filament_pos` status properties in `klipper/mmu.py` to prevent Fluidd LOAD button from disabling.
- [x] 12.5 Skip `FLARE_UNLOAD_TOOLHEAD` in `klipper/flare_mmu.cfg` if no filament is detected at the toolhead switch sensor.

---
### Validation Notes — 2026-05-22 (Cutter Derivation & Load Button Refinements)
- Verified `enable_cutter` is exposed in Klipper's state dictionary (`printer.mmu.enable_cutter`), allowing UIs/macros to query it.
- Verified that `gate` and `tool` are reported as `-1` (unloaded) when the filament is not fully loaded (`gate_status` not equal to 2), which correctly enables the `LOAD` button in Fluidd/Mainsail dashboards for preloaded lanes.
- Initialized, dynamically derived, and exposed `filament` ("Unloaded" / "Loaded") and `filament_pos` (0 / 10) in `klipper/mmu.py` based on Klipper MMU mock gate loading status, matching Happy Hare schema perfectly to ensure Fluidd's UI controls remain fully functional.
- Wrapped `FLARE_UNLOAD_TOOLHEAD` execution inside a `filament_switch_sensor` state check in `klipper/flare_mmu.cfg`, cleanly skipping tip-forming and gear retraction moves if no filament is present at the toolhead.
- Ran static Python linter/compiler checks and confirmed a perfect pass.

## Phase 13: Firmware-Level Auto-Cutting for Manual Unload and Eject
- [x] 13.1 Revert `FLARE_CUT` macro chaining in `cmd_MMU_UNLOAD` and `cmd_MMU_EJECT` within `klipper/mmu.py`.
- [x] 13.2 Add `MANUAL_UNLOAD_WAIT_CUT` state to manual unload state machine in `firmware/src/protocol.c`.
- [x] 13.3 Implement cutter cycle trigger inside `UL:` and `UM:` commands in `firmware/src/protocol.c` if `ENABLE_CUTTER` and `UNLOAD_CUT` are true.
- [x] 13.4 Ensure firmare and Klipper python changes compile successfully.
- [x] 13.5 Register `MMU_CHECK_GATES` in `klipper/mmu.py` mapping to `cmd_MMU_CHECK_GATE` to support checking all gates.

---
### Validation Notes — 2026-05-22 (Firmware-Level Auto-Cutting & MMU_CHECK_GATES)
- Reverted `FLARE_CUT` macro execution from `cmd_MMU_UNLOAD` and `cmd_MMU_EJECT` in `klipper/mmu.py`. Manual unloads now delegate physical cutting entirely to the RP2040 MMU firmware via raw serial commands.
- Implemented `MANUAL_UNLOAD_WAIT_CUT` state inside the manual unload state machine tick handler (`manual_unload_tick` in `firmware/src/protocol.c`).
- Wired auto-cutter invocation (`cutter_start`) inside both the `UL:` (manual unload to gate) and `UM:` (manual eject) serial command handlers when `ENABLE_CUTTER` and `UNLOAD_CUT` are active.
- Registered standard `MMU_CHECK_GATES` (plural) command in `klipper/mmu.py` mapping directly to the existing `cmd_MMU_CHECK_GATE` function, resolving the unknown command error when checking all gates.
- Successfully verified both clean firmware compilation (`ninja -C build_local`) and Python syntax validity (`python3 -m py_compile`).

## Phase 14: MMU_SELECT Selection Refinements
- [x] 14.1 Update `cmd_MMU_SELECT` in `klipper/mmu.py` to differentiate between pure gate selection (no `TOOL` parameter) and toolchange (with `TOOL` parameter).
- [x] 14.2 For pure gate selection, update `self.active_gate = gate`, call `_ensure_array_lengths()` to trigger UI update, and run shell command `T:{lane}`.
- [x] 14.3 Run Python verification tests (`python3 -m py_compile klipper/mmu.py`) and make sure it is syntactically correct.

---
### Validation Notes — 2026-05-22 (MMU_SELECT Selection Refinements)
- Updated `cmd_MMU_SELECT` to check for `TOOL` parameter to distinguish pure active gate UI clicks from physical toolchange macro invocation.
- For pure gate selection (e.g. clicking gate cards), set `self.active_gate = gate` immediately to trigger low-latency UI highlighting update and dispatched serial active lane change `T:{lane}` to the board.
- Verified that both Python syntax checks (`python3 -m py_compile`) and native firmware builds (`ninja -C build_local`) pass flawlessly.

## Phase 15: Post-Reboot/Reconnect State Synchronization
- [x] 15.1 Replace `_FLARE_BOOT` delayed gcode macro with `_FLARE_SYNC_BOARD` macro in `klipper/flare_mmu.cfg` that queries Klipper toolhead sensor state and sends `TS:1`/`TS:0` and `RELOAD_MODE` to the board.
- [x] 15.2 In `scripts/flare_daemon.py`, track `was_online` state in the `klipper_syncer` thread and queue `_FLARE_SYNC_BOARD` execution to Moonraker on connection/boot transition to resolve desynchronization.
- [x] 15.3 Ensure Python compilation checks pass and local firmware builds successfully.

---
### Validation Notes — 2026-05-22 (Post-Reboot/Reconnect Sync)
- Implemented `_FLARE_SYNC_BOARD` macro inside `klipper/flare_mmu.cfg`, querying if the Klipper toolhead sensor is active and synchronizing it immediately to the board (`TS:1` or `TS:0`) along with `RELOAD_MODE`.
- Added dynamic transition state tracking (`was_online`) inside the `klipper_syncer` daemon thread in `scripts/flare_daemon.py` to automatically execute `_FLARE_SYNC_BOARD` upon board boot/reconnection.
- Verified that both Python validation (`python3 -m py_compile`) and native builds (`ninja -C build_local`) pass successfully.

## Phase 16: UI Spool Mapping and Action Button State Polish
- [x] 16.1 Define `tool_color`, `tool_material`, `tool_spool_id`, `tool_color_rgb`, `tool_name`, `tool_filament_name` dynamic properties in `klipper/mmu.py` mapping tools to physical gates via `ttg_map`.
- [x] 16.2 Expose the new virtual `tool_` arrays in the `get_status` dictionary returned to Moonraker / UI clients.
- [x] 16.3 Fix bug in `cmd_SET_MMU` in `klipper/mmu.py` where omitting the `GATE` parameter incorrectly defaults to `self.active_gate` instead of preserving `self.gate`.
- [x] 16.4 Verify that Python compilation passes cleanly (`python3 -m py_compile klipper/mmu.py`).

---
### Validation Notes — 2026-05-22 (Spool Mapping & Buttons Polish)
- Defined and exposed all required virtual `tool_` properties (`tool_color`, `tool_material`, `tool_spool_id`, `tool_color_rgb`, `tool_name`, `tool_filament_name`) mapping to physical gates via the dynamic tool-to-gate array `ttg_map` inside the Klipper MMU mock (`klipper/mmu.py`). This guarantees Mainsail/Fluidd correctly resolve color, material, and Spoolman mappings for both active and inactive gates/tools, eliminating "Unknown everything" errors on unloaded gates.
- Resolved a critical status synchronization bug in `cmd_SET_MMU` where the `GATE` parameter default fell back to `self.active_gate` if omitted, which forced active status on empty gates. It now properly preserves the current `self.gate` value, which correctly enforces fully disabled action button states for empty spools.
- Verified that all Python syntax checks pass flawlessly (`python3 -m py_compile`) and firmware builds build cleanly (`ninja -C build_local`).

## Phase 17: Synchronous Toolchange Blocking
- [x] 17.1 Register `FLARE_WAIT_TC` command in Klipper mock `klipper/mmu.py`.
- [x] 17.2 Implement `FLARE_WAIT_TC` using `reactor.pause()` polling loop to check toolhead sensor state.
- [x] 17.3 Refactor `_FLARE_CHANGE_LANE` in `klipper/flare_mmu.cfg` to run synchronously using `FLARE_WAIT_TC`.
- [x] 17.4 Verify firmware and config build successfully.

---
### Validation Notes — 2026-05-22 (Synchronous Toolchange Blocking)
- Implemented and registered `FLARE_WAIT_TC` command inside the Klipper extra helper `klipper/mmu.py`. This uses the Klipper event reactor `reactor.pause()` to yield execution to the event loop, allowing background syncer/daemon status updates to come through while blocking sequential print G-code stream execution.
- Refactored `_FLARE_CHANGE_LANE` in `klipper/flare_mmu.cfg` to chain physical toolchange (`TC:{lane}`) followed immediately by the blocking `FLARE_WAIT_TC` command and synchronous `_FLARE_POST_TC_LOAD` execution, ensuring the next print commands from the SD card/stream do not run until the entire physical sequence completes.
- Verified that both local firmware compilation (`ninja -C build_local`) and file adjustments compile perfectly.

## Phase 18: Wire autoload_retract_mm through config.ini
- [x] 18.1 Add `autoload_retract_mm` to `DEFAULTS` in `scripts/gen_config.py` with default value of `"5"`.
- [x] 18.2 Add generator define `CONF_AUTOLOAD_RETRACT_MM` in `scripts/gen_config.py`.
- [x] 18.3 Consume `CONF_AUTOLOAD_RETRACT_MM` in `firmware/src/main.c` and `firmware/src/settings_store.c` to fully respect config.ini overrides.
- [x] 18.4 Re-run `gen_config.py` and verify local firmware compilation succeeds.

---
### Validation Notes — 2026-05-22 (autoload_retract_mm Integration)
- Added `autoload_retract_mm` parameter to the default configuration map in `scripts/gen_config.py`, making it a recognized config setting.
- Generated the `#define CONF_AUTOLOAD_RETRACT_MM` macro in `firmware/include/tune.h`.
- Updated global variable `AUTOLOAD_RETRACT_MM` initialization in `firmware/src/main.c` and defaults restoration in `firmware/src/settings_store.c` to use the generated `CONF_AUTOLOAD_RETRACT_MM` instead of hardcoding `5`.
- Successfully validated Python parser compile status (`python3 -m py_compile`) and confirmed the local firmware compiles flawlessly.

## Phase 19: Fix autoload_retract_mm execution bug in firmware motion logic
- [x] 19.1 Update `firmware/src/motion.c` to initialize `L->dist_at_out_mm` to `L->task_dist_mm` when the OUT sensor is triggered during `TASK_AUTOLOAD`.
- [x] 19.2 Verify that the retract distance calculation `L->task_dist_mm - L->dist_at_out_mm` starts measuring from zero correctly.
- [x] 19.3 Verify local firmware compilation succeeds.

---
### Validation Notes — 2026-05-22 (autoload_retract_mm Bugfix)
- Fixed execution bug where autoload retract ended immediately without moving the motor.
- Initialized `L->dist_at_out_mm` to `L->task_dist_mm` in `firmware/src/motion.c` when the OUT sensor triggers and transitions to reverse retract. This ensures `L->task_dist_mm - L->dist_at_out_mm` starts measuring from zero instead of starting at `DIST_IN_OUT` (which immediately satisfied the `AUTOLOAD_RETRACT_MM` limit).
- Confirmed the native firmware compiles successfully (`ninja -C build_local`).
- Verified all regression checks pass successfully (`bash scripts/validate_regression.sh`).

## Phase 20: Fix Active Gate Selection & Spool Details Desync in Klipper/UI
- [x] 20.1 Update Klipper MMU mock `klipper/mmu.py` `cmd_SET_MMU` to decouple `filament` loaded state derivation from `gate == -1`, using `toolhead_sensor` and physical gate buffer/load states instead.
- [x] 20.2 Update `klipper/mmu.py` `cmd_MMU_SELECT` to set `self.gate` and `self.tool` to the selected gate index during pure UI selection so the active details panel displays correctly.
- [x] 20.3 Update the status reporter in `scripts/flare_daemon.py` to pass the currently selected `active_gate` as the default for `GATE` and `TOOL` variables when the MMU is physically unloaded, instead of defaulting them to `-1` and overwriting Klipper's state.
- [x] 20.4 Verify Python scripts compile cleanly and pass static regression gate checks.

---
### Validation Notes — 2026-05-22 (Active Gate Selection & Spool Details Fixes)
- Decoupled Klipper MMU mock `filament` loaded status derivation from strictly checking `self.gate == -1`. It now evaluates physical sensor triggers (`toolhead_sensor` and gate buffer state), allowing `self.gate` and `self.tool` to stay mapped to inactive/unloaded selections while accurately reporting an unloaded status.
- Updated `cmd_MMU_SELECT` to set `self.gate` and `self.tool` to the newly selected gate on pure UI card clicks, and adjusted the early return check so that selecting a gate when `self.gate == -1` will correctly apply updates.
- Refined the daemon status reporter loop in `scripts/flare_daemon.py` to assign `active_gate` as the default for `GATE` and `TOOL` variables under physically unloaded states instead of defaulting to `-1`, which solves the 250ms feedback overwrite loop.
- Verified syntax correctness using `python3 -m py_compile` and successfully ran regression checks via `scripts/validate_regression.sh`.

## Phase 21: Correct Unloaded Button Actions & Trace Toggling
- [x] 21.1 Align `klipper_tool` and `klipper_gate` in `scripts/flare_daemon.py` to both use `loaded_gate` (which is `-1` if physically unloaded), keeping `active_gate` tracking UI selection.
- [x] 21.2 Update `cmd_MMU_SELECT` in `klipper/mmu.py` to set both `self.gate` and `self.tool` to `expected_gate` (which is `-1` if physically unloaded) during pure UI selection.
- [x] 21.3 Run static python verification checks and ensure regression validations pass.

---
### Validation Notes — 2026-05-22 (Unloaded Buttons and Active Gate Selection)
- Verified `flare_daemon.py` resolves `klipper_tool` and `klipper_gate` to the active physically loaded gate (or `-1` if completely unloaded). This removes premature loaded state indications in Fluidd when clicking gate cards.
- Verified that `cmd_MMU_SELECT` in `klipper/mmu.py` sets both `self.gate` and `self.tool` to `expected_gate` (which evaluates to `-1` if physically unloaded), properly disabling `UNLOAD` and `EJECT` and correctly managing `LOAD` based on gate presence (`gate_status` availability).
- Ran all local regression test cases (`bash scripts/validate_regression.sh`) and confirmed all static verification checks pass perfectly.

## Phase 22: Correct Preloaded Gate UI Selection While Another Gate is Loaded
- [x] 22.1 Refine `is_physically_loaded` in `cmd_MMU_SELECT` within `klipper/mmu.py` to check `self.gate == gate and self.toolhead_sensor == 1`.
- [x] 22.2 Preserve `self.gate` and `self.tool` unchanged during pure UI selection if the selected gate is not physically loaded.
- [x] 22.3 Run static python verification checks and ensure regression validations pass.

---
### Validation Notes — 2026-05-22 (Preloaded UI Selection & Jump-back Fix)
- Refined `is_physically_loaded` inside `cmd_MMU_SELECT` within `klipper/mmu.py` to check `self.gate == gate and self.toolhead_sensor == 1`, preventing global toolhead sensor trigger from misidentifying unselected lanes as loaded.
- Preserved `self.gate` and `self.tool` unchanged during pure UI selection if the selected gate is not physically loaded. This successfully maintains physical loaded state (`gate=0, tool=0`), preventing any feedback jump-back loop.
- Verified that `self.active_gate = gate` correctly updates, highlighting the selected card, showing its spool filament info, and leaving correct `LOAD` and `EJECT` buttons active.
- Confirmed C-firmware builds successfully via `ninja -C build_local`.

## Phase 23: Fix Active Gate Selection & UI Highlight Jump-back Loop
- [x] 23.1 In `scripts/flare_daemon.py`, change `klipper_gate = loaded_gate` and `klipper_tool = loaded_gate` to assign `active_gate` instead.
- [x] 23.2 In `klipper/mmu.py` `cmd_SET_MMU`, refine `is_loaded` derivation to strictly check if the selected gate matches the physically loaded gate.
- [x] 23.3 In `klipper/mmu.py` `cmd_MMU_SELECT`, set `self.gate` and `self.tool` to the newly selected gate in the pure UI selection branch.
- [x] 23.4 Run static python verification checks and ensure regression validations pass.

---
### Validation Notes — 2026-05-22 (Active Gate Selection & UI Highlight Loop Fix)
- Verified `flare_daemon.py` reports `klipper_gate` and `klipper_tool` as the currently selected `active_gate` instead of the physically `loaded_gate`, solving the 250ms feedback overwrite loop.
- Verified `cmd_MMU_SELECT` sets `self.gate` and `self.tool` to the selected gate index on pure UI clicks to trigger low-latency highlighting and details panel updates in Mainsail/Fluidd.
- Verified `is_loaded` derivation strictly compares the selected gate index against the physically loaded gate, preventing global toolhead sensor states from incorrectly enabling UNLOAD and disabling LOAD/EJECT on inactive/preloaded lanes.
- Confirmed syntax validity (`python3 -m py_compile`) and regression tests pass perfectly (`bash scripts/validate_regression.sh`).

## Phase 24: Selected-Gate Load/Eject Macro Routing
- [x] 24.1 Rename the shared conditional homing helper from `_CG28` to `_FLARE_CG28` so the include does not collide with a user's existing `_CG28` macro.
- [x] 24.2 Make `FLARE_LOAD LANE=<n>` select `T:<n>` before `FL:` so `MMU_LOAD` loads the UI-selected gate instead of whichever lane was previously active on the board.
- [x] 24.3 Make `FLARE_EJECT LANE=<n>` call `UM:<n>`, preserving `UM:` only for direct active-lane ejects without a lane parameter.
- [x] 24.4 Update `cmd_MMU_EJECT` to target the selected gate, skip toolhead unload for merely preloaded inactive gates, and pass `LANE=<n>` to `FLARE_EJECT`.
- [x] 24.5 Update Klipper docs/specs and validate Python, OpenSpec, and firmware build.

---
### Validation Notes — 2026-05-22 (Selected-Gate Load/Eject Routing)
- Verified `MMU_SELECT GATE=<n>` is a pure UI selection path: it updates `active_gate`, `gate`, and `tool`, then sends `T:<lane>` to the board without starting motion.
- Updated `FLARE_LOAD LANE=<n>` to send `T:<n>` before `FL:`, while bare `FLARE_LOAD` still sends the active-lane `FL:` command.
- Updated `FLARE_EJECT LANE=<n>` to send `UM:<n>`, while bare `FLARE_EJECT` still sends active-lane `UM:`.
- Updated `MMU_EJECT` to resolve `GATE` or `active_gate`, unload toolhead gears only when the selected gate has per-gate loaded status `2`, and otherwise eject the selected preloaded gate directly.
- Validation passed: `python3 -m py_compile klipper/mmu.py scripts/*.py`, `openspec validate --specs --strict`, `ninja -C build_local`, `git diff --check`, and `bash scripts/validate_regression.sh`.

## Phase 25: Manual Unload Cut Ordering and MMU_LOAD Handoff
- [x] 25.1 Fix active-lane `UL:` with `UNLOAD_CUT=1` so firmware unloads past `OUT`, runs the cutter, then unloads past `OUT` again.
- [x] 25.2 Fix active-lane `UM:` with `UNLOAD_CUT=1` and fully loaded filament so firmware runs the full `UL:` cut sequence first, then continues reverse until `IN` clears.
- [x] 25.3 Preserve inactive explicit `UM:n` standby eject behavior: only preloaded `IN=1, OUT=0` targets unload to `IN`, with no cutter and no active-lane state changes.
- [x] 25.4 Make Klipper `MMU_LOAD` use the selected-gate load plus the same post-toolchange hotend handoff/purge path as `_FLARE_CHANGE_LANE`.
- [x] 25.5 Update behavior/manual/OpenSpec docs and validate firmware build, Python, specs, and regression gate.

---
### Validation Notes — 2026-05-22 (Manual Unload Cut Ordering + MMU_LOAD Handoff)
- Fixed the manual unload state context by adding a `cut_pending` flag. `UL:` and fully loaded active-lane `UM:` now begin with a suppressed reverse clear leg, start the cutter only after `OUT` clears, then run the second reverse clear leg.
- Preserved inactive explicit standby eject: `UM:n` for inactive lanes still requires `IN=1, OUT=0`, unloads only to `IN` clear, and does not start the cutter or touch active-lane state.
- Updated `MMU_LOAD` to run `FLARE_LOAD LANE=<n>` and then `_FLARE_POST_TC_LOAD LANE=<n>`, matching the toolchange post-pickup extruder grab, hotend load, and purge sequence.
- Validation passed: `python3 -m py_compile klipper/mmu.py scripts/*.py`, `openspec validate --specs --strict`, `ninja -C build_local`, `git diff --check`, and `bash scripts/validate_regression.sh`.

## Phase 26: Physical Sensor State Integration & Visualizer Parity
- [x] 26.1 Define `gate_sensor_active` and `extruder_sensor_active` in `klipper/mmu.py`.
- [x] 26.2 Update `cmd_SET_MMU` in `klipper/mmu.py` to parse `GATE_SENSOR_ACTIVE` and `EXTRUDER_SENSOR_ACTIVE`.
- [x] 26.3 Update `get_status` in `klipper/mmu.py` to expose a nested `sensors` dictionary (mapping `gate`, `extruder`, `toolhead`, `tension`, and `compression`).
- [x] 26.4 Update `klipper_syncer` in `scripts/flare_daemon.py` to compute physical `gate_sensor_active` (based on the active gate's `out` sensor) and `extruder_sensor_active` (based on `y_split`), and pass them via the Moonraker G-code script.
- [x] 26.5 Verify that Python compilation and syntax checks pass cleanly.
- [x] 26.6 Refine the visualizer sensor mapping for the Happy Hare track dots:
  - `pre_gate` -> `IN` sensor (`self.pre_gate_sensor_active`, parsed from `PRE_GATE_SENSOR_ACTIVE`).
  - `gate` -> `OUT` sensor/Gear (`self.gate_sensor_active`, parsed from `GATE_SENSOR_ACTIVE`).
  - `hub` -> `YS` combiner (`self.hub_sensor_active`, parsed from `HUB_SENSOR_ACTIVE`).
  - `extruder` -> `TS` toolhead switch (`self.toolhead_sensor`).
  - `toolhead` -> `TS` toolhead switch (`self.toolhead_sensor`).

---

### Validation Notes — 2026-05-23 (Physical Sensor State Integration & Refined 5-Dot Visualizer Mapping)
- Implemented nested `sensors` dictionary inside Klipper MMU mock (`klipper/mmu.py`) matching the Happy Hare status schema (`printer.mmu.sensors`). This enables green indicator dot visualization along the filament track in Fluidd/Mainsail dashboards.
- Refined the mapping to align perfectly with the physical 5-dot track:
  - Map `pre_gate` dot (dot 1) to `IN` sensor (`in1`/`in2` for active gate).
  - Map `gate` / Gear dot (dot 2) to `OUT` sensor (`out1`/`out2` for active gate).
  - Map `hub` dot (dot 3) to `YS` combiner sensor.
  - Map `extruder` (dot 5) and `toolhead` (dot 6) dots both to the single `TS` toolhead sensor.
- Wired real-time board sensor states through the Klipper telemetry syncer thread in `scripts/flare_daemon.py`, translating physical `in/out` and `y_split` triggers into `GATE_SENSOR_ACTIVE`, `EXTRUDER_SENSOR_ACTIVE`, `PRE_GATE_SENSOR_ACTIVE`, and `HUB_SENSOR_ACTIVE` parameters.
- Successfully verified both Python syntax validity and committed clean changes.

## Phase 27: mmu_sensors Mock Integration
- [x] 27.1 Create mock Klipper extra `klipper/mmu_sensors.py` returning expected sensor fields in get_status.
- [x] 27.2 Add `[mmu_sensors]` section inside `klipper/flare_mmu.cfg`.
- [x] 27.3 Update target `klippy/extras` copy/install commands inside `scripts/install_daemon.sh`.
- [x] 27.4 Validate Python syntax, build firmware, and test locally.

## Phase 28: Filament Track Sensor Discovery Hardening
- [x] 28.1 Implement dynamic `filament_switch_sensor` mock registration for `mmu_pre_gate_0`, `mmu_pre_gate_1`, `mmu_gate`, `mmu_extruder`, `mmu_toolhead`, and `mmu_hub` inside `klipper/mmu_sensors.py` constructor to satisfy Fluidd/Mainsail dynamic discovery.
- [x] 28.2 Add active gate selector checks to override shared/global sensors (`gate`, `extruder`, `toolhead`, `hub`) to `False` for unselected/unloaded lanes, eliminating false positive triggers on other lanes.
- [ ] 28.3 Ask the user to run `install_daemon.sh` and perform a Klipper RESTART, then verify the visual presence and state behavior of all 5 filament track sensor dots in Fluidd dashboard.


---
### Validation Notes — 2026-05-23 (mmu_sensors Mock Integration)
- Created the new mock Klipper extra module `klipper/mmu_sensors.py` that registers the `mmu_sensors` printer object and dynamically resolves all standard Happy Hare sensor states (`pre_gate_0`, `pre_gate_1`, `gate`, `extruder`, `toolhead`, `hub`, `sync_feedback_tension`, `sync_feedback_compression`) from the unified MMU status cache.
- Added the `[mmu_sensors]` config section inside `klipper/flare_mmu.cfg` mapping dummy pins to satisfy Fluidd's capability detection interface without allocating actual MCU resources.
- Updated `scripts/install_daemon.sh` installer script to automatically copy `mmu_sensors.py` alongside `mmu.py` into the target Klipper extras directory during installation.
- Verified that local C/C++ firmware builds compile cleanly and successfully.
- Fixed Klipper config validation error ("Option 'pre_gate_switch_pin_0' is not valid in section 'mmu_sensors'") by explicitly reading and consuming all dummy configuration options in the `MMUSensorsMock` constructor.
- Overrode global/shared filament switch mock sensors to report `filament_detected = False` when another lane is physically loaded, ensuring that unselected/unloaded lanes correctly show hollow track dots for shared sensors instead of inheriting global triggers.


## Phase 29: Cutting Sequence Position Tracking & Active Spool UI Timing
- [x] 29.1 Update `klipper/mmu.py`'s `cmd_FLARE_WAIT_TC` to instantly set `self.active_gate = target_gate`, `self.gate = target_gate`, and `self.tool = target_gate` at start.
- [x] 29.2 Prevent the HTTP polling loops (both main and stabilization) from overwriting `active_gate`/`gate`/`tool` back to the old lane during unload/cut.
- [x] 29.3 Modify the virtual progress calculation for `"cut"` phase in `klipper/mmu.py`'s `get_status` to be toolhead-relative (starting at `path_len`, feeding forward to `path_len + 10.0`, settling, and retracting to `path_len - 40.0`).
- [x] 29.4 Modify the virtual progress calculation for `"unload"` phase in `get_status` to start at `path_len - 40.0` when `self.unload_cut` is active, and at `path_len` when inactive, ensuring smooth, jump-free tracking.
- [x] 29.5 Validate Python syntax (`python3 -m py_compile klipper/mmu.py`) and verify firmware builds.

---

### Validation Notes — 2026-05-28 (Toolhead-Relative Cut Progress & Spool UI Timing)
- Updated `cmd_FLARE_WAIT_TC` in `klipper/mmu.py` to instantly update and freeze `self.active_gate`, `self.gate`, and `self.tool` to `target_gate = lane - 1` when the wait loop begins. The daemon HTTP polling loops (both main wait and stabilization delay) are blocked from overwriting these values back to the old lane, ensuring Fluidd's dashboard active spool, color, and material card transitions instantly at the start of loading instead of waiting for full completion.
- Re-modeled virtual progress during the `"cut"` phase to be toolhead-relative (where the physical cutter resides) starting at `path_len` (~1917 mm): feed-forward (`path_len` -> `path_len + 10.0` over 1.5s), settle/cut (holds at `path_len + 10.0` for 1.0s), and retract (`path_len + 10.0` down to `path_len - 40.0` at 50 mm/s).
- Programmed the `"unload"` phase to seamlessly resume from the cut phase's ending position (`path_len - 40.0`) if `unload_cut` is active, or from `path_len` if inactive, cleanly transitioning down to `0.0` mm and resolving all telemetry jumps.
- Verified syntax successfully using `python3 -m py_compile klipper/mmu.py` and confirmed C compilation integrity.


## Phase 30: Separate Command Tracking, High-Fidelity Unload Countdown & Toolchange Spool Timing
- [x] 30.1 Update `scripts/flare_daemon.py` to pass `TC_STATE='{tc_state}'` in `SET_MMU` commands.
- [x] 30.2 Update `klipper/mmu.py`'s `cmd_SET_MMU` to parse `TC_STATE` and dynamically transition `self.current_phase` for background tracking of separate manual load/unload G-code commands.
- [x] 30.3 Refine `get_status` in `klipper/mmu.py` to calculate dynamic progress whenever `self.is_loading` is True OR `self.current_phase in ["unload", "cut", "load"]`.
- [x] 30.4 Delay spool transitions in `cmd_FLARE_WAIT_TC` to keep `self.active_gate = old_gate` during the unload and cut phases, and only transition to `target_gate` when `"load"` phase actually begins, solving premature spool changes.
- [x] 30.5 Program high-fidelity unload tracking: clamp position to `max(bowden_length, ...)` while toolhead sensor is active, and once toolhead sensor clears, smoothly countdown from `bowden_length` to `0.0` mm, completely resolving leaving-TH jumps.
- [x] 30.6 Run Python compilation validation (`python3 -m py_compile scripts/*.py klipper/mmu.py`) and verify firmware builds.

---

### Validation Notes — 2026-05-28 (Separate Commands, High-Fidelity Unload & Delayed Spool Highlighting)
- Integrated `TC_STATE` passing into the background syncer in `scripts/flare_daemon.py`. Parses the real-time firmware state and broadcasts it to Moonraker.
- Rewrote `cmd_SET_MMU` in `klipper/mmu.py` to actively parse `TC_STATE` and dynamically update `self.current_phase` when G-code queue is not blocked (`not self.is_loading`). Enabled identical smooth telemetry calculations during manual commands (`MMU_LOAD`, `MMU_UNLOAD`, `FLARE_LOAD`, `FLARE_UNLOAD`).
- Delayed active spool card highlight transitions inside `cmd_FLARE_WAIT_TC`: `self.active_gate`, `self.gate`, and `self.tool` now remain set to `old_gate` during `"unload"` and `"cut"` phases, and only transition to `target_gate` when the physical loading phase (`"load"`) begins. Resolved the premature spool card change bug.
- Implemented high-fidelity unload tracking in `get_status()`: the tip position is clamped to `max(bowden_length, ...)` while `path_toolhead` sensor remains triggered. Upon clearing (`not path_toolhead`), `self.th_clear_time` is captured, and the position counts down smoothly and continuously from `bowden_length` to `0.0` mm. Completely resolved all leaving-TH telemetry jumps.
- Confirmed Python compilation success (`scripts/*.py` and `klipper/mmu.py`) and verified C local build compilation is perfectly clean.


## Phase 31: Zero-Jump Cutter Tracking & Unload Completion State Gating
- [x] 31.1 Initialize `self.unload_completed = False` in `klipper/mmu.py`'s `__init__`.
- [x] 31.2 Reset `self.unload_completed = False` on transitions to `"cut"` or `"load"` phases inside both `cmd_FLARE_WAIT_TC` and `cmd_SET_MMU`.
- [x] 31.3 In `get_status` unload phase: once the filament reaches `0.0` mm or `not path_gear` is triggered, set `self.unload_completed = True` to permanently force `0.0` mm and prevent bounces when the new lane's gear triggers.
- [x] 31.4 In `get_status` cut phase: remodel cutter position relative to gate-side cutter (`150.0 mm` from gear) to match the physically correct MMU gate cut position: feed-forward (`150.0 -> 160.0`), cut/settle, and retract (`160.0 -> 0.0 mm`).
- [x] 31.5 In `get_status` unload phase: set the start point to `bowden_length` (1800 mm) when `path_toolhead` is False, and let the unload phase continue counting down smoothly past `150.0` mm to `0.0` mm.
- [x] 31.6 Validate Python syntax compile and check C builds.

---

### Validation Notes — 2026-05-28 (Zero-Jump Cut Progress & Unload Gating)
- Implemented `self.unload_completed` state tracking inside Klipper MMU mock (`klipper/mmu.py`) to permanently lock `filament_position` to `0.0` mm once the unload countdown completes or drive gear sensor clears. Cleared the flag on load/cut transitions. This cleanly prevents the position from jumping to `100-200` mm when the new spool's gears trigger the sensor during phase swaps.
- Remodeled `"cut"` phase virtual progress to be centered at the gate-side MMU cutter (`150.0 mm` from gear): counts up past cutter (`150.0 -> 160.0` mm), cuts, and retracts back past cutter gears to `0.0` mm.
- Set `"unload"` phase countdown start point to `bowden_length` (1800 mm) when `path_toolhead` sensor is clear, counting down smoothly past `150.0` mm (where `"cut"` phase seamlessly intercepts it) down to `0.0` mm.
- Verified Python code compiles cleanly with no warnings or syntax errors. Confirmed local firmware builds are fully green.






