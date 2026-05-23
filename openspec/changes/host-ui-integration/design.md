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
2. The mock handler checks if the `TOOL` parameter was explicitly passed:
   - **Physical Toolchange (with `TOOL` parameter)**: Maps the gate index to the corresponding FLARE lane (`lane = gate + 1`) and runs Klipper command `_FLARE_CHANGE_LANE LANE=<lane>` to perform physical toolchange automatically.
   - **Pure Gate Selection (no `TOOL` parameter, e.g. clicking gate card in UI)**: Only marks the gate selected in the UI by updating `self.active_gate = gate`, and issues a `RUN_SHELL_COMMAND CMD=flare PARAMS="T:<lane>"` to update the active lane on the RP2040 board without triggering motor movements.
3. If the selected gate is already the active gate (`gate == active_gate`), do nothing and return.
4. Unloading of the previous active lane during a physical toolchange is handled by `_FLARE_CHANGE_LANE` (running `FLARE_UNLOAD_TOOLHEAD`).

Dashboard load/eject actions route through the selected gate:
1. `MMU_LOAD` resolves `GATE` or `active_gate`, runs `FLARE_LOAD LANE=<gate+1>`, then runs `_FLARE_POST_TC_LOAD LANE=<gate+1>`.
2. `FLARE_LOAD LANE=<n>` sends `T:<n>` before `FL:` so the board loads the selected lane even if a different lane had been active previously.
3. `_FLARE_POST_TC_LOAD` performs the same post-toolchange handoff used by `_FLARE_CHANGE_LANE`: MMU-only pickup, synchronized extruder grab, hotend load, and purge.
4. `MMU_EJECT` resolves `GATE` or `active_gate`, skips `FLARE_UNLOAD_TOOLHEAD` when the selected gate is only preloaded, then runs `FLARE_EJECT LANE=<gate+1>`.
5. `FLARE_EJECT LANE=<n>` sends `UM:<n>`, relying on firmware's explicit standby-lane eject safety checks for inactive preloaded lanes.

## 9. Gate Array Length Hardening & Check Gate Refinement

To guarantee consistent discovery of multiple gates in Mainsail/Fluidd:
1. **Explicit Gate Count**: `SET_MMU` commands sent from the daemon `klipper_syncer` thread must explicitly pass `NUM_GATES=2`. This ensures Klipper registers the correct number of gates even if the persistent file `flare_mmu_vars.json` is absent or corrupt.
2. **List Size Invariants**: Pad/truncate all gate-specific lists (`gate_status`, `gate_sensor`, `gate_color`, `gate_material`, `gate_spool_id`, `gate_color_rgb`, `gate_name`, `gate_filament_name`, `ttg_map`) to exactly `self.num_gates` elements during initialization, state updates, and load operations in Klipper `mmu.py`. This prevents persistent data with mismatched sizes from shrinking the list lengths reported to Moonraker.
3. **Interactive Gate Verification**: Implement `MMU_CHECK_GATE` in `mmu.py` to trigger a daemon-level refresh (`?:` query) and output a detailed, human-readable gate state report (including sensor status, loaded/preloaded states, material, and color info) in the G-code console.

## 10. Klipper mmu_machine Mocking for Multi-Gate Visibility

To resolve the missing second gate spool card in Fluidd:
1. **Background**: Fluidd queries `printer.mmu_machine` to retrieve unit-level gate metadata (e.g., `unit_0.num_gates`).
2. **Issue**: Because `mmu_machine` was missing in Klipper, Fluidd fell back to `numGates = 1`, hiding the second gate.
3. **Solution**: Mock the `mmu_machine` Klipper object by registering `MMUMachineMock` under the name `mmu_machine` via `printer.add_object('mmu_machine', ...)`. This object dynamically returns the `num_gates` currently tracked by the `mmu` object.

## 11. UI Bugfixes & Polish (Active Gate & Spoolman Save Mappings)

### 11.1 Active Gate Highlighting
To resolve Fluidd only highlighting Gate 0 as active/loaded, we expose `gate` in the `mmu` object's Klipper status dictionary. `mmu_cmd` in `flare_daemon.py` includes `GATE={active_gate}` alongside `ACTIVE_GATE={active_gate}`. In `klipper/mmu.py`, `self.gate` is defined and updated via `SET_MMU GATE={active_gate}`.

### 11.2 Spoolman MAP Parameter Saving
Fluidd transmits all filament and spool updates using a dictionary parameter `MAP="{...}"` to `MMU_GATE_MAP`.
We parse this string using Python's safe `ast.literal_eval`. For each gate in the dictionary:
- We update `gate_material`, `gate_spool_id`, `gate_status`, `gate_name`, and `gate_filament_name`.
- We strip `#` and any 8-char alpha channel from `color` to ensure standard 6-char hex color compat, then update both `gate_color` and `gate_color_rgb`.
- We save the updated parameters to `flare_mmu_vars.json` to persist the configuration.

### 12.1 Cutter Derivation & Fluidd Load Button Fix
- **Cutter Derivation**: Klipper exposes `enable_cutter` as a read-only state variable in `get_status` derived from the board's internal telemetry. No manual configure/set via Klipper-side G-codes is required.
- **Fluidd Load Button**: To fix the disabled load button in Fluidd when the active lane is preloaded but not loaded, we separate selector/active gate tracking (`ACTIVE_GATE`) from the loaded filament tracking (`GATE` and `TOOL`). `GATE` and `TOOL` are set to `active_gate` only when the active gate's filament is fully loaded to the toolhead (all sensors: `in && out && y_split && toolhead` are 1). If not fully loaded, `GATE` and `TOOL` are set to `-1`. This correctly reports the extruder as empty to Fluidd, enabling the `LOAD` button for preloaded lanes.

### 13. Correct Preloaded Gate UI Selection While Another Gate is Loaded
- **Problem**: When selecting a preloaded gate card (`T1`) while another gate (`T0`) is loaded, Klipper evaluates `is_physically_loaded` as `True` because `self.toolhead_sensor == 1` globally. This incorrectly sets `self.gate` and `self.tool` to `1` (as if Gate 1 is loaded), and the daemon subsequently overwrites it back to `0` (since Gate 0 is physically loaded). This feedback loop forces Fluidd's UI to jump selection back to `T0` and misreports Gate 1 loaded state.
- **Solution**: Refine `is_physically_loaded` in `cmd_MMU_SELECT` to check if `self.gate == gate` and `self.toolhead_sensor == 1`. During pure UI gate selection, preserve `self.gate` and `self.tool` unchanged by defaulting them to `self.gate`/`self.tool` if not physically loaded, keeping physical loaded state intact while setting `self.active_gate = gate` to correctly select/highlight the preloaded gate card and display its filament details without any jump-back.

## 14. Fluidd Filament-Path Sensor Key Contract

- **Problem**: Only 2 of the expected sensors (Extruder, Toolhead) render on Fluidd's MMU filament-path widget. The "Runout Sensors" card lists all sensors fine, masking the bug.
- **Root cause**: Fluidd's `MmuFilamentStatus.vue` renders a track dot only when an exact key exists in `printer.mmu.sensors` (`hasSensor(name) = name in this.sensors`). It looks for `mmu_pre_gate`, `mmu_gear`, `mmu_gate`, `extruder`, `toolhead`, plus sync `filament_tension` / `filament_compression` / `filament_proportional`. The mock `sensors` dict used `pre_gate`, `gate`, `hub`, `tension`, `compression` — so only `extruder` and `toolhead` matched. The "Runout Sensors" card is a separate Klipper path that enumerates `filament_switch_sensor` objects (registered by `mmu_sensors.py`) and is unrelated to the MMU widget.
- **Sensor key contract** (FLARE physical → Fluidd path slot):

  | FLARE physical | `mmu.sensors` key | Fluidd label |
  |:---|:---|:---|
  | IN switch (active lane) | `mmu_pre_gate` | Pre-Gate |
  | OUT switch | `mmu_gear` | Gear |
  | Y-splitter / hub (shared) | `mmu_gate` | Gate |
  | toolhead sensor | `toolhead` | Toolhead |
  | buffer state | `filament_tension` / `filament_compression` | sync piston |

  Fluidd's single path exposes ONE `mmu_pre_gate` slot (active lane only); the inactive lane's pre-gate appears on the spool/gate cards, not the track. FLARE has a single physical toolhead sensor (`TS:`), so only the `toolhead` slot is emitted; the `extruder` slot is intentionally omitted to avoid a duplicate dot (both would read the same `toolhead_sensor`). `toolhead` is preferred over `extruder` because Fluidd's fill animation (`endOfBowdenPos`, `filamentRectHeight`) keys off `sensors['toolhead']`. Realistic maximum is 4 dots + the sync piston.

- **Sync-feedback (buffer piston) fixes**:
  1. **State vocabulary**: firmware emits `TENSION`/`COMPRESSION`/`NEUTRAL`; daemon lowercases to `tension`/`compression`. Fluidd expects `tension`/`compressed`/`neutral` (`SYNC_FEEDBACK_COMPRESSED='compressed'`). Daemon normalizes `compression → compressed` so both the path booleans and `sync_feedback_state` label render. The dead `== "expanded"` checks are removed (firmware never emits that token).
  2. **Piston position**: Fluidd reads `sync_feedback_bias_modelled` (not `sync_feedback`). Export `sync_feedback_bias_modelled` (= rescaled `-1..1` value) and `sync_feedback_enabled` from `mmu.get_status` so the piston animates instead of freezing at center.

- **Downstream sensor cascade (shared-sensor leakage)**:
  - **Problem**: `mmu_gate` (Y-splitter / hub) and `toolhead` are shared across both lanes (daemon sets them from `y_split` / `toolhead_sensor` globally). When viewing a non-loaded gate while the other gate is loaded, these shared dots show green even though the viewed gate's per-lane gear (OUT) sensor is clear — implying filament is past a point it never reached.
  - **Rule**: a strand cannot occupy a sensor it has not reached. In `mmu.get_status`, once the per-lane gear (OUT) sensor (`mmu_gear`) is clear, force the downstream shared sensors (`mmu_gate`, `toolhead`) clear.
  - **Anchor at gear, not pre-gate**: pre-gate (IN) sits before the drive; a spool runout can clear IN while filament remains threaded past the gear, so cascading from pre-gate would wrongly blank a still-loaded path. Gear (OUT) is the first post-drive, per-lane sensor and is the correct cascade anchor.


