## Why

FLARE currently uses `scripts/flare_cmd.py` via Klipper `gcode_shell_command` which opens and closes the `/dev/ttyACM0` serial port per command. This design has significant limitations:
1. **Serial Conflict**: Only one connection can access `/dev/ttyACM0` at a time. A WebUI or continuous telemetry poll would lock the port and cause Klipper commands to crash with `SerialException: device busy`.
2. **Latency Bottleneck**: Spawning a new Python process and opening the serial port takes 100–200ms per command, which is slow for real-time macro handoffs and UI responses.
3. **No Continuous Telemetry**: We cannot stream high-frequency (50Hz) buffer positions (`g_buf_pos`) to a chart because polling `?:` via a shell process would choke Klipper.

This proposal introduces a persistent background service, `flare_daemon.py`, which owns the serial connection, exposes a multiplexed local WebSocket/HTTP API, and masquerades as the Happy Hare MMU to light up native Mainsail/Fluidd panels with dynamic buffer slider animations.

---

## What Changes

### 1. Host Daemon: `scripts/flare_daemon.py` [NEW]
- Persistent daemon written in Python (std-lib only, except for `pyserial`).
- Connects to `/dev/ttyACM0` at boot and handles auto-reconnect.
- Polls `?:` at 20Hz (or higher when sync is active) and parses events (`EV:`).
- Caches telemetry in-memory.
- Exposes:
  - **Tornado or simple http.server/websockets API** for high-speed WebUI stream (50Hz WebSocket).
  - **HTTP POST endpoint** for Klipper commands (proxies commands instantly to the open serial port).

### 2. Klipper Integration & Masquerade: `klipper/flare_mmu.cfg` [MODIFY]
- Define `_FLARE_STATE` macro variables.
- Add mock `printer.mmu` variables so Mainsail/Fluidd natively display FLARE status and animate the dynamic sync offset slider using `g_buf_pos`.
- Rewrite `flare_cmd.py` to talk to the local `flare_daemon` port instead of `/dev/ttyACM0`, reducing process overhead and latency from 150ms to <2ms.

### 3. Installer & Service: `scripts/install_daemon.sh` [NEW]
- Automates installation on Raspberry Pi / Klipper host.
- Registers `flare_daemon` as a `systemd` service (`flare_daemon.service`) that starts automatically at boot.
- Configures log rotation and user permissions.

### 4. Custom Iframe WebUI: `scripts/webui/index.html` [NEW]
- A lightweight single-page application (Vanilla HTML5/Canvas/CSS).
- Served by the Moonraker web server or `flare_daemon`'s internal static file server.
- Graphically displays high-frequency `g_buf_pos` traces, live sensor indicators, and calibration guides.

---

## Capabilities

### New Capabilities
- `host-serial-proxy`: Non-blocking, multiplexed serial access for host scripts and external UI clients.
- `native-mmu-masquerade`: Complete compatibility with Mainsail/Fluidd's built-in MMU dashboard and status indicators.
- `realtime-buffer-trace`: Fluid 50FPS visualization of the active lane's spring-trolley buffer position.

---

## Impact & Regressions

- **Firmware Impact**: **Zero**. RP2040 C code remains completely untouched. No new firmware commands or state machines are required.
- **Klipper Safety**: Because `flare_daemon.py` runs as an asynchronous host daemon outside of Klipper's process, it carries **zero** risk of triggering Klipper real-time step generation crashes.
- **User Interface**: Upgrading to the daemon is 100% transparent. Current macros and commands continue to work exactly as they do today, but execute ~75x faster.
