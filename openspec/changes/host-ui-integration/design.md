# Design: Host UI & Daemon Integration

## 1. Telemetry Mapping (Mainsail/Fluidd Masquerade)

Mainsail and Fluidd look for specific fields in the `printer.mmu` Klipper state object. We will mock these fields by updating Klipper variables via the Moonraker Unix Socket API.

| FLARE Telemetry | Happy Hare Mock Object | Mainsail/Fluidd Rendering |
|:---|:---|:---|
| `g_buf_pos` | `printer.mmu.sync_feedback` | Vertical slider offset (`-1.0` to `+1.0`) |
| `g_sync_state` | `printer.mmu.sync_feedback_state` | Status text (`neutral`, `compressed`, `expanded`) |
| `active_lane` | `printer.mmu.active_gate` | Highlights active lane (L1 = Gate 0, L2 = Gate 1) |
| `I1`, `I2` (In-switches) | `printer.mmu.gate_sensor` | Highlights the pre-gate sensor green dots |
| `O1`, `O2` (Out-switches) | `printer.mmu.gate_status` | Spool presence indicators (empty vs loaded) |
| `TH` (Toolhead switch) | `printer.mmu.toolhead_sensor` | Green dot at bottom of toolhead track |

### Rescaling Formula for Buffer Position
FLARE `g_buf_pos` represents raw millimeter displacement from the spring center. We convert it to the `-1.0` to `1.0` range expected by Mainsail:
```python
# physical_half is half of BUF_SWITCH_SPAN or BUF_MAX_TRAVEL_MM
max_travel = 15.0  # mm
sync_feedback = clamp(g_buf_pos / max_travel, -1.0, 1.0)
```

---

## 2. Daemon Architecture & Threading Model

To guarantee Klipper safety, the daemon must be robust, non-blocking, and std-lib only.

```
                  ┌──────────────────────────────┐
                  │       flare_daemon.py        │
                  │                              │
                  │  ┌────────────────────────┐  │
                  │  │     Serial Thread      │  │
                  │  │   (pyserial, blocking) │  │
                  │  └───────────┬────────────┘  │
                  │              │ (Queue)       │
                  │              ▼               │
                  │  ┌────────────────────────┐  │
                  │  │       Main Loop        │  │
                  │  │    (asyncio / select)  │  │
                  │  └───────────┬────────────┘  │
                  │              │               │
                  │    ┌─────────┴─────────┐     │
                  │    ▼                   ▼     │
                  │ ┌──────┐          ┌────────┐ │
                  │ │ HTTP │          │ WebSocket│
                  │ │ Server│         │ Server │ │
                  │ └──────┘          └────────┘ │
                  └──────────────────────────────┘
```

### Python std-lib Implementation Guidelines:
- **Serial Thread**: Runs a blocking `readline()` loop. When a line starts with `EV:` or matches the `status_dump` pattern, it pushes the parsed packet onto a thread-safe `queue.Queue`.
- **Main Event Loop**: Uses python's built-in `asyncio` or a basic `select` loop to handle the HTTP and WebSocket endpoints without spawning high numbers of subprocesses.
- **Auto-Reconnect**: If `serial.SerialException` is raised, close the port, sleep 2 seconds, and attempt to re-open `/dev/ttyACM0` or `/dev/ttyACM1` using glob matching.

---

## 3. Communication API Endpoints

### HTTP Server (Default Port: `8080` or configurable)

- **`GET /status`**: Returns the latest cached state of the RP2040 in JSON format.
- **`POST /cmd`**: Body contains raw FLARE C-command string (e.g. `{"cmd": "TC:1"}`).
  - Daemon immediately writes the string to serial.
  - Blocks and returns `200 OK` with response payload when `OK:` or `ER:` is received.

### WebSocket Server (Default Port: `8081` or unified port)

- Streams continuous telemetry at 50Hz to any connected client.
- Format:
  ```json
  {
    "timestamp": 1716382940.123,
    "active_lane": 1,
    "g_buf_pos": -0.45,
    "sync_feedback": -0.03,
    "buffer_state": "NEUTRAL",
    "sps": 1250.0
  }
  ```

---

## 4. Installer and Infrastructure

### Systemd Unit File: `flare_daemon.service`
```ini
[Unit]
Description=FLARE Standalone Serial Proxy & UI Daemon
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/FLARE
ExecStart=/usr/bin/python3 scripts/flare_daemon.py --port /dev/ttyACM0 --api-port 8080
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

### Installation Script: `install.sh`
1. Detect active serial port (falls back to `/dev/ttyACM0` or asks user).
2. Check if `pyserial` is installed; runs `pip3 install pyserial` if missing.
3. Write `flare_daemon.service` to `/etc/systemd/system/`.
4. Run `sudo systemctl daemon-reload`.
5. Run `sudo systemctl enable --now flare_daemon`.
6. Append dummy `[gcode_macro _FLARE_STATE]` and Moonraker configuration blocks to Klipper files.

---

## 5. Key Coding Tips for Implementation

- **Low-pass Filter on Klipper Updates**: Mainsail/Klipper only needs `sync_feedback` updates at 2–4Hz. Do **not** send every 50Hz WebSocket frame to Klipper via Moonraker's UDS, or you will bloat Klipper logs and trigger UI lag. Filter Klipper updates so they only push on state changes or every 250ms.
- **Write raw C-strings carefully**: RP2040 uses `\n` line termination. Ensure every command sent to serial ends in exactly `\n` and is flushed immediately.
- **Handle USB unplugging gracefully**: When `/dev/ttyACM0` disappears physically, python will raise `OSError` or `SerialException`. Catch this, release the serial handle, enter `CONNECTING` state, and periodically retry. **Do not crash the daemon process.**
