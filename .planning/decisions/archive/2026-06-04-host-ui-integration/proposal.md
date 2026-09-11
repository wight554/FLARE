## Why

FLARE currently uses `scripts/flare_cmd.py` via Klipper `gcode_shell_command` which opens and closes the `/dev/ttyACM0` serial port per command. This design has significant limitations:
1. **Serial Conflict**: Only one connection can access `/dev/ttyACM0` at a time. A WebUI or continuous telemetry poll would lock the port and cause Klipper commands to crash with `SerialException: device busy`.
2. **Latency Bottleneck**: Spawning a new Python process and opening the serial port takes 100–200ms per command, which is slow for real-time macro handoffs and UI responses.
3. **No Continuous Telemetry**: We cannot stream high-frequency (50Hz) buffer positions (`g_buf_pos`) to a chart because polling `?:` via a shell process would choke Klipper.

This proposal introduces a persistent background service, `flare_daemon.py`, which owns the serial connection, exposes a multiplexed local WebSocket/HTTP API, and mock-updates Klipper states when available. 

---

## Non-Negotiable Architectural Constraints

To preserve FLARE's standalone nature and verify it against all testing plans:

1. **Klipper-Independence**: The host infrastructure must NOT be tied to Klipper. `flare_daemon.py` must run as a generic, standalone Python process. If Klipper/Moonraker are absent, the daemon must run perfectly, serving the WebUI and API without throwing errors.
2. **Hostless / Standalone Operation**: The RP2040 firmware must remain 100% autonomous. If USB is disconnected, the onboard C firmware must execute all motion, sync feedback, and RELOAD logic without host assistance. This proposal requires **zero** firmware modifications.
3. **Graceful Fallback for Testing**: `scripts/flare_cmd.py` is the foundation of all automated and manual hardware test plans (e.g., `TEST_CASES.md`). To prevent breaking any test cases or user workflows:
   - If `flare_daemon.py` is active, `flare_cmd.py` routes commands through the daemon API (reducing latency to <2ms).
   - If `flare_daemon.py` is stopped or absent, `flare_cmd.py` **automatically falls back to direct serial connection** to talk to the RP2040. All existing test cases and scripts remain 100% functional.

---

## Tech Stack & Design Aesthetics

To ensure maximum performance on low-resource Klipper hosts (Raspberry Pi) and provide a state-of-the-art UI experience:

1. **Ultra-Lightweight Core**: Zero heavy frameworks (no React, Vue, or Next.js). The frontend uses **Vanilla HTML5, ES6 JavaScript, and Vanilla CSS3**.
   - No `node_modules` compilation or installation overhead on the Pi.
   - Extremely small footprint (<20KB total package) serving in under 1ms.
2. **Hardware-Accelerated Visualization**: Real-time buffer force tracking is drawn using **HTML5 Canvas** at 60FPS. 
   - Uses browser GPU acceleration directly, imposing **0% CPU load** on the Raspberry Pi host.
3. **Premium Aesthetics**:
   - Modern typography using the Google Font **Outfit** or **Inter**.
   - Deep, curated dark-mode theme utilizing harmonious HSL-tailored neon accents (no raw primary colors).
   - Glassmorphism UI panel containers utilizing CSS backdrop-filter blur effects (`backdrop-filter: blur(12px)`).
   - Dynamic hover micro-animations and smooth CSS transitions.

---

## What Changes

### 1. Host Daemon: `scripts/flare_daemon.py` [NEW]
- Persistent daemon written in Python (std-lib only, except for `pyserial`).
- Connects to `/dev/ttyACM0` at boot and handles auto-reconnect.
- Polls `?:` at 20Hz (or higher when sync is active) and parses events (`EV:`).
- Caches telemetry in-memory.
- Exposes:
  - **Tornado or simple http.server/websockets API** for high-speed WebUI stream (50Hz WebSocket).
  - **HTTP POST endpoint** for commands (proxies commands instantly to the open serial port).
- **Optional Klipper Bridge**: Only attempts to push state variables to Moonraker/Klipper if the Moonraker Unix Domain Socket is available. Fails silently if running standalone.

### 2. Client Script: `scripts/flare_cmd.py` [MODIFY]
- Implement the "Smart Proxy Fallback":
  - First, attempt to send commands via local HTTP/socket API on `flare_daemon`.
  - If connection is refused (daemon offline), fall back to opening `/dev/ttyACM0` directly.
- Maintain identical CLI interface, exit codes, and stdout format to keep all automated scripts and `TEST_CASES.md` completely compatible.

### 3. Klipper Integration & Masquerade: `klipper/flare_mmu.cfg` [MODIFY]
- Define mock `printer.mmu` variables so Mainsail/Fluidd natively display FLARE status and animate the dynamic sync offset slider using `g_buf_pos` (only used when Klipper is present).

### 4. Installer & Service: `scripts/install_daemon.sh` [NEW]
- Automates installation on Raspberry Pi / Linux host.
- Registers `flare_daemon` as a `systemd` service (`flare_daemon.service`) that starts automatically at boot.
- Supports a `--no-klipper` flag to skip Moonraker macro installation.

### 5. Standalone WebUI: `scripts/webui/index.html` [NEW]
- A lightweight single-page application (Vanilla HTML5/Canvas/CSS).
- Hosted natively by the `flare_daemon`'s internal static file server or by Moonraker.
- Graphically displays high-frequency `g_buf_pos` traces, live sensor indicators, and calibration guides.
- Exposes all interactive commands (`TC:`, `FL:`, `UL:`, `UM:`, `CU:`, `SET:`, `GET:`, `MV:`) as buttons, sliders, and a developer console.

---

## Capabilities

### New Capabilities
- `host-serial-proxy`: Non-blocking, multiplexed serial access for host scripts and external UI clients.
- `klipper-independent-telemetry`: Complete telemetry and calibration server running without Klipper dependencies.
- `native-mmu-masquerade`: Complete compatibility with Mainsail/Fluidd's built-in MMU dashboard and status indicators (optional integration).
- `realtime-buffer-trace`: Fluid 50FPS visualization of the active lane's spring-trolley buffer position.

---

## Impact & Regressions

- **Firmware Impact**: **Zero**. RP2040 C code remains completely untouched. No new firmware commands or state machines are required.
- **Testing Compatibility**: **100% compatible**. Because `flare_cmd.py` has the direct serial fallback, all checklists in `TEST_CASES.md` and `validate_regression.sh` remain valid under any configuration.
