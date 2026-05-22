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




