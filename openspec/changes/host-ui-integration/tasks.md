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
