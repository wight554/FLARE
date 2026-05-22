# Design: Host UI & Daemon Integration

## 1. Telemetry Mapping (Mainsail/Fluidd Masquerade)

Mainsail and Fluidd look for specific fields in the `printer.mmu` Klipper state object. When Moonraker/Klipper is detected, the daemon will push updates to these variables at a low frequency (2–4Hz) via the local Unix Domain Socket or HTTP API.

| FLARE Telemetry | Happy Hare Mock Object | Mainsail/Fluidd Rendering |
|:---|:---|:---|
| `g_buf_pos` | `printer.mmu.sync_feedback` | Vertical slider offset (`-1.0` to `+1.0`) |
| `g_sync_state` | `printer.mmu.sync_feedback_state` | Status text (`neutral`, `compressed`, `expanded`) |
| `active_lane` | `printer.mmu.active_gate` | Highlights active lane (L1 = Gate 0, L2 = Gate 1) |
| `I1`, `I2` (In-switches) | `printer.mmu.gate_sensor` | Highlights the pre-gate sensor green dots |
| `O1`, `O2` (Out-switches) | `printer.mmu.gate_status` | Spool presence / loaded status: 0 = empty, 1 = preloaded (IN), 2 = loaded (IN+OUT+YS) |
| `TH` (Toolhead switch) | `printer.mmu.toolhead_sensor` | Green dot at bottom of toolhead track |

### Rescaling Formula for Buffer Position
```python
# Convert raw mm to the -1.0 to 1.0 float expected by Mainsail
max_travel = 15.0  # mm (derived from BUF_SWITCH_SPAN or BUF_MAX_TRAVEL_MM)
sync_feedback = clamp(g_buf_pos / max_travel, -1.0, 1.0)
```

---

## 2. Klipper-Independence & Standalone Execution

`flare_daemon.py` must run reliably on any Linux host (Raspberry Pi, laptop, or industrial PC) without Klipper or Moonraker dependencies.

- **Optional Integration**: During startup, `flare_daemon.py` checks for the presence of the Moonraker API socket (typically `/tmp/moonraker.sock` or `http://localhost:7125`).
- **Graceful Degradation**: If Moonraker is not found, the Klipper-masquerade bridge is completely bypassed. The daemon prints a warning to logs and runs purely as a standalone serial-to-HTTP proxy.
- **Standalone Dashboard**: The embedded static web server hosts the same HTML/JS Canvas UI. Standalone users can access the dashboard by navigating to `http://<pi_ip>:8088` in their browser, gaining full access to:
  - Real-time buffer telemetry graphs.
  - Manual load, unload, and lane swap commands (`TC:`, `FL:`, `UL:`, `UM:`).
  - Interactive sensor calibration assistants.

---

## 3. UI Tech Stack & Sleek Dark-Mode Design

The custom dashboard will be served directly by the daemon (`http://<pi_ip>:8088`).

### Code Stack
- **Structure**: Vanilla HTML5. Highly semantic, unique IDs for browser automation testing.
- **Logic**: Vanilla ES6 JavaScript. Uses the standard browser **EventSource (Server-Sent Events / SSE)** for real-time telemetry streaming and standard Fetch APIs for sending commands.
  - **Why SSE?**: 100% native HTTP protocol, requires zero custom WebSocket frame parsing in std-lib Python, extremely robust, and instantly supported by modern browsers.
- **Styling**: Vanilla CSS3. Utilizes CSS Custom Properties (variables) for theme tokens.
- **Visualization**: HTML5 Canvas API. Telemetry points are pushed into a rolling buffer and rendered at 60FPS using `requestAnimationFrame()`, avoiding CPU overhead.

### Premium Aesthetic System
- **Colors**: Curated deep grey background (`#0d0f12`), translucent glass card panels (`rgba(22, 26, 30, 0.75)` with `backdrop-filter: blur(12px)` and `border: 1px solid rgba(255, 255, 255, 0.05)`).
- **Accents**: Neon teal (`hsl(174, 100%, 45%)`) for active loaded states, neon coral (`hsl(354, 100%, 60%)`) for tension warnings, and pure white for telemetry labels.
- **Typography**: Imported Google Font `Outfit` (`sans-serif`, weight 400 & 600) for a state-of-the-art tech aesthetic.

---

## 4. Graceful Client Fallback (flare_cmd.py)

To prevent breaking `TEST_CASES.md` and manual bring-up checklists, `scripts/flare_cmd.py` implements the following fallback sequence when a user runs a command:

```
                  [ User runs scripts/flare_cmd.py "TC:1" ]
                                     │
                                     ▼
                      [ Attempt HTTP POST to Daemon ]
                          [ http://localhost:8088/cmd ]
                                  /     \
                       Connection/       \Connection
                        Succeeds/         \Refused (Daemon Offline)
                               /           \
                              ▼             ▼
                      [ Proxy Command ]   [ Fallback to Direct Serial ]
                     [ via Daemon Port ]  [ Open /dev/ttyACM0 directly ]
                              │             │
                              ▼             ▼
                      [ Read Response ]   [ Execute Command on Board ]
                              │             │
                              ▼             ▼
                          [ Print OK: / ER: and Exit ]
```

This guarantees that:
- Standalone / direct USB setups continue to work without a daemon running.
- In-daemon environments automatically bypass serial handshake overhead, running instantly.
- Test suites run identically on both developer laptops (no daemon) and target print hosts (daemon active).

---

## 5. Daemon Architecture & Threading Model

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
                  │  │      (Threading/       │  │
                  │  │     SocketServer)      │  │
                  │  └───────────┬────────────┘  │
                  │              │               │
                  │    ┌─────────┴─────────┐     │
                  │    ▼                   ▼     │
                  │ ┌──────┐          ┌────────┐ │
                  │ │ HTTP │          │  SSE   │ │
                  │ │ Server│         │ Stream │ │
                  │ └──────┘          └────────┘ │
                  └──────────────────────────────┘
```

### Auto-Reconnect Logic:
If the USB cable is physically unplugged or the board resets:
1. `pyserial` raises `SerialException`.
2. The serial thread catches the exception, enters `DISCONNECTED` state, and closes the port handle.
3. Every 2 seconds, the serial thread attempts to reconnect to `/dev/ttyACM*`.
4. While disconnected, the HTTP server returns `503 Service Unavailable` with `{"error": "board_offline"}`. The SSE stream broadcasts offline state.
5. **No daemon crash occurs.** Graceful recovery is fully automatic.

---

## 6. Network Specifications

### HTTP Server (Port: `8088`)
- **`GET /status`**: Returns the latest cached state of the RP2040 in JSON format.
- **`POST /cmd`**: Body contains raw FLARE C-command string (e.g. `{"cmd": "TC:1"}`).
  - Daemon immediately writes the string to serial.
  - Blocks and returns `200 OK` with response payload when `OK:` or `ER:` is received.
- **`GET /telemetry`**: Continuous Server-Sent Events stream of real-time stats at 20Hz.
- **`GET /`**: Serves the standalone HTML/Canvas WebUI.

---

## 7. Spoolman Integration (Happy Hare Mocking)

To support spool assignment from Mainsail/Fluidd natively:
1. `spoolman_support` is exported in the `[mmu]` state as `"get"`.
2. A G-code command `MMU_SPOOLMAN` is registered to handle spool mappings:
   - `MMU_SPOOLMAN GATE=<gate> SPOOLID=<id>` associates a spool.
   - `MMU_SPOOLMAN GATE=<gate> CLEAR=1` unmaps the spool.
3. The assigned spool IDs are stored in `gate_spool_id` list and persisted inside `flare_mmu_vars.json` to survive restarts/reloads.

## 8. Dashboard Gate Selection (MMU_SELECT)

When a user selects a tool or gate in Fluidd/Mainsail dashboard:
1. G-code command `MMU_SELECT` is issued with `GATE=<int>` or `TOOL=<int>`.
2. The mock handler maps the gate index to the corresponding FLARE lane (`lane = gate + 1`).
3. If the selected gate is already the active gate (`gate == active_gate`), do nothing and return.
4. If a different gate is selected, run Klipper command `_FLARE_CHANGE_LANE LANE=<lane>` to perform the toolchange/selection automatically.
5. Unloading of the previous active lane is handled by `_FLARE_CHANGE_LANE` (running `FLARE_UNLOAD_TOOLHEAD`).

## 9. Gate Array Length Hardening & Check Gate Refinement

To guarantee consistent discovery of multiple gates in Mainsail/Fluidd:
1. **Explicit Gate Count**: `SET_MMU` commands sent from the daemon `klipper_syncer` thread must explicitly pass `NUM_GATES=2`. This ensures Klipper registers the correct number of gates even if the persistent file `flare_mmu_vars.json` is absent or corrupt.
2. **List Size Invariants**: Pad/truncate all gate-specific lists (`gate_status`, `gate_sensor`, `gate_color`, `gate_material`, `gate_spool_id`, `gate_color_rgb`, `gate_name`, `gate_filament_name`, `ttg_map`) to exactly `self.num_gates` elements during initialization, state updates, and load operations in Klipper `mmu.py`. This prevents persistent data with mismatched sizes from shrinking the list lengths reported to Moonraker.
3. **Interactive Gate Verification**: Implement `MMU_CHECK_GATE` in `mmu.py` to trigger a daemon-level refresh (`?:` query) and output a detailed, human-readable gate state report (including sensor status, loaded/preloaded states, material, and color info) in the G-code console.
