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

## 15. Standalone WebUI Control Deck Parity with Fluidd

The daemon-served dashboard (`scripts/webui/`) mirrors Fluidd's `MmuControls.vue` button set, naming, and enable/disable gating, adapted to FLARE C-commands (the WebUI talks to the board via `POST /cmd`, not Klipper G-code).

- **Button → FLARE command map** (acts on the active lane):

  | Fluidd button | FLARE cmd | Enabled when (active lane gate_status `g`) |
  |:---|:---|:---|
  | Preload | `LO:` (run to OUT) | `g == 0` (empty) |
  | Eject | `UM:` (unload to IN clear) | `g != 0` |
  | Check Gate | `?:` (status query) | online |
  | Unload | `UL:` (unload extruder) | `g == 2` (loaded to toolhead) |
  | Load | `FL:` (full load to toolhead) | `g != 2` |

- **gate_status in JS**: derived client-side from the SSE telemetry exactly as the daemon's klipper-syncer does — `2` if `in && out && y_split && toolhead`, `1` if `in`, else `0`. The per-lane OUT switch gates the shared `y_split`/`toolhead`, so a non-active lane never reads loaded. All controls disable while disconnected/offline.
- **Lane selector**: `Lane 1`/`Lane 2` retained as FLARE-native naming (not renamed to Gate 0/1). Fixed to send `T:n` (select, no motion); the previous `LN:n` is not a firmware command and was a no-op.
- **Dropped**: `Cut Filament` (`CU:`) and `Unload Model` removed to match Fluidd's deck. `Load` is the primary (teal) action; `Unload` secondary.
- **Recover / Unlock**: omitted — FLARE firmware has no error-lock or recover command (`RS:` is reset-settings, not recover). These cannot be removed from upstream Fluidd's `MmuControls.vue` without forking it; in our mock they stay inert (Unlock is gated by `isMmuPausedAndLocked`, which the mock never reports; Recover calls `MMU_RECOVER`, a harmless ack).
- **Load semantics in standalone**: `FL:` loads to the toolhead sensor only. Hotend push/purge (`_FLARE_POST_TC_LOAD`) is Klipper-macro-only and not available in the daemon-only path, so the WebUI Load stops at the toolhead.

## 16. MMU Usage Statistics (Event-Counted, Daemon-Owned)

`MMU_STATS` was previously a static stub printing all zeros. Real counters are now derived from board events. Firmware does nothing for stats — counting is host-side only.

- **Source of truth = daemon.** The board emits reliable events (`EV:TC:DONE`, `EV:TC:ERROR`, `EV:LOADED`, `EV:UNLOADED`). `flare_daemon.py` counts them in the serial-reader loop (which sees every event, so nothing is missed by a transient poll):
  - `TC:DONE` → `swaps_total++`, `swaps_success++`
  - `TC:ERROR` → `swaps_total++`, `swaps_failed++`, `last_error = data`
  - `LOADED` → `loads_success++`
  - `UNLOADED` → `unloads_success++`

  Only `TC:` (and RELOAD) drive the firmware `tc_state` machine; `FL:`/`LO:`/`UL:`/`UM:` are lane tasks, so swaps are cleanly isolated from loads/unloads.
- **Persistence.** Counters persist to `flare_mmu_stats.json` (`~/printer_data/config` → `~` → `/tmp` fallback) and reload on daemon start, surviving restarts.
- **Push to Klipper, idempotent.** The daemon pushes **absolute** totals via `SET_MMU` (`SWAPS_TOTAL`, `SWAPS_SUCCESS`, `SWAPS_FAILED`, `LOADS_SUCCESS`, `UNLOADS_SUCCESS`, `MMU_LAST_ERROR`). `mmu.py` mirrors them (never accumulates locally), so a Klipper restart can't double-count — the next `SET_MMU` overwrites with the persisted totals.
- **Consumers**:
  - `cmd_MMU_STATS` renders live values + success rate; `SHOWCOUNTS=1` appends a per-gate status/spool breakdown.
  - `mmu.get_status` exports `num_toolchanges = swaps_total` so Fluidd's filament-path "(N swaps)" counter works, plus the raw stat fields.
  - The daemon includes `mmu_stats` in every SSE telemetry frame; the WebUI shows Tool Swaps / Success Rate / Loads / Unloads / Last Error in a Usage Statistics panel.

## 17. Stale-Active-Lane Protection on Sync Auto-Start

- **Problem**: The UI lets the operator switch the active lane to inspect or eject a non-loaded lane (`T:n`). If they leave the selection on an unloaded lane, the firmware's tension-triggered sync auto-start (`sync_tick`, `AUTO_MODE && !sync_enabled && s == BUF_TENSION`) would enable sync on the *wrong* (empty) lane.
- **Fix**: At the auto-start point, before enabling sync, adopt the physically loaded lane — if filament is at the hub (`on_al(&g_y_split)`), set active to whichever lane has its OUT sensor engaged (`lane_out_present`). Re-fetch the active lane pointer after switching so tail-assist/bootstrap use the corrected lane.
- **Safety**: Double-load (both OUT engaged) is already excluded by the existing guard, so the correction never fires ambiguously. The switch only happens when a single lane's OUT is engaged at the hub, and only when it differs from the current active lane.

## 18. Preload Semantics + Unsupported-Button Handling

### Preload = stage to gate (LO:), not full load
- `MMU_PRELOAD` previously ran `FLARE_LOAD` (`T:n; FL:`) = a full load to the toolhead, which FL:'s `OTHER_LANE_ACTIVE` guard then blocked while another lane was loaded. Now `MMU_PRELOAD` runs the new `FLARE_PRELOAD` macro (`T:n; LO:`) — a true preload to the lane's own OUT, matching Happy Hare semantics and the WebUI.
- **Enabled only on an empty gate** (`gate_status == 0`, no IN) in both Fluidd (`GATE_EMPTY`) and the WebUI. `LO:` spins the gear and grabs filament as it is inserted, so preload is the "spin-and-insert" flow; on a gate that already has filament it is disabled (nothing to preload — use Load). The earlier IN-present guard was removed because it blocked exactly this spin-and-insert use.
- Preload is mostly redundant on FLARE (auto-preload fires on insertion); it is kept for setups with `AUTO_PRELOAD` disabled, where the operator clicks Preload then inserts.

### Buttons we cannot back, and what we can/can't disable
Fluidd buttons gated only by `!klippyReady || !canSend` have **no `printer.mmu` field** to disable them from the mock — they cannot be greyed without forking Fluidd. This includes **Recover**, Check Gate, Motors On/Off, Sync Gear Motor.
- **Hidden via mock feature flags** (already): Servo/Home/Grip/Release (`selector_type = VirtualSelector`), LEDs (`mmu_leds` absent), Encoder (`encoder` absent), Bypass (`has_bypass = false`).
- **Cannot disable, so handled gracefully** (avoid "Unknown command"): `MMU_RECOVER` → emits a `!!`-prefixed "not implemented on FLARE" message (Klipper renders `!!` as a console error, so the user sees it is not a real action); `MMU_SYNC_GEAR_MOTOR SYNC=0/1` → mapped to real `SM:0/1` (extruder sync); `MMU_MOTORS_ON/OFF` → no-op (drivers firmware-managed).

### Double-load guard on FD:
`FD:` (raw forward feed) now rejects `OTHER_LANE_ACTIVE` when the hub is occupied by the other lane (same guard as `FL:`/`RL:`), since it could otherwise feed a second filament into the hub. `MV:` is intentionally left unguarded as the raw escape hatch for worst-case manual recovery.

### Maintenance-dialog extruder-only load/unload
The maintenance dialog issues `MMU_LOAD EXTRUDER_ONLY=1` / `MMU_UNLOAD EXTRUDER_ONLY=1` — operate on the extruder/hotend portion only, no MMU gate movement:
- `MMU_LOAD EXTRUDER_ONLY=1` → `_FLARE_LOAD_HOTEND` (push parked filament through the meltzone + purge); skips the leading `FL:` and the MMU approach/sync grab.
- `MMU_UNLOAD EXTRUDER_ONLY=1` → `FLARE_UNLOAD_TOOLHEAD` (tip forming + gear retract); skips the trailing `UL:` gate unload.

No-op handlers use the same `!!`-prefixed style as Recover so they read as "not implemented" in the console: `MMU_MOTORS_ON/OFF` emits `!! MMU motor on/off is not implemented on FLARE`.

## 19. WebUI Manual Move (MV:) Panel

The WebUI Control Deck has a Fluidd-style manual-move block driving the FLARE lane motor via `MV:`.
- **Inputs**: Length (mm) and Speed (**mm/s**). `MV:` expects mm/min, so the handler multiplies speed by 60. Extrude sends `MV:<+len>:<feed>`, Retract sends `MV:<-len>:<feed>` (firmware takes direction from the sign).
- **Lane awareness**: `MV:` acts on the firmware active lane. The WebUI's `Lane 1`/`Lane 2` selector (`T:n`) sets that, so the move targets the selected lane. Extrude/Retract are gated on a lane being active (and disabled while offline).
- `MV:` disables sync for the finite move (firmware behavior); validation requires length > 0 and speed > 0.

## 20. Filament Position Readout + Spoolman Active-Spool Tracking

### Synthetic filament_position (Fluidd "Filament: X mm")
Fluidd reads `mmu.filament_position` (mm tip position). FLARE has no continuous encoder, so it previously stayed 0. `mmu.get_status` now synthesizes it from how far the strand has advanced through the (cascaded) path sensors, scaled by an approximate load-path length:
- toolhead → 100%, hub/gate → 60%, gear(OUT) → 30%, pre-gate(IN) → 10%, none → 0.
- Path length = `_FLARE_VARS` toolhead geometry (`dist_sensor_to_extruder + dist_extruder_to_meltzone + dist_meltzone_to_nozzle_tip`, default 117 mm).

This is an indicator (tip progress), **not** consumption, and is approximate by design — it mirrors Happy Hare's non-zero readout rather than measuring real mm.

### Spoolman active-spool tracking (consumption)
We do not compute filament usage; **Moonraker's** Spoolman module does, billing the *active spool*. Like Happy Hare, the daemon keeps Moonraker's active spool aligned to the loaded gate:
- `klipper_syncer` tracks the loaded gate; on change it reads `mmu.gate_spool_id` from Moonraker and `POST`s `/server/spoolman/spool_id` with the loaded gate's spool (or `null` when nothing is loaded).
- Pushed only on loaded-gate change (no per-cycle spam); Moonraker then attributes extruder usage to the correct spool across toolchanges.
- Spoolman is optional: if not configured the `POST` 404s and is silently ignored. We still only *map* spools (`spoolman_support = "get"`); the gate↔spool attributes shown in Fluidd are fetched by Fluidd directly from Spoolman.

## 21. WebUI Spool Cards (Klipper-Agnostic Gate Map)

The standalone WebUI shows painted spool cards in the Active Lane selector and lets the user edit the gate map, mirroring the Fluidd MMU widget but working without Klipper.

- **Shared store**: the daemon reads/writes the same `flare_mmu_vars.json` that `klipper/mmu.py` uses (identical path-resolution), so the WebUI and the Fluidd widget share one gate map.
- **Endpoints**:
  - `GET /gatemap` → per-gate `{material, color, name, spool_id, spool}` where `spool` is the Spoolman enrichment.
  - `POST /gatemap` `{gate, material?, color?, name?, spool_id?}` → persist the edit.
- **Edit propagation**: on edit the daemon **pushes `MMU_GATE_MAP`** (single-quoted python-dict literal, like Fluidd) to Moonraker so the Klipper mock + Fluidd update live; if Moonraker is down it writes `flare_mmu_vars.json` directly (mmu.py picks it up on next load). So edits flow both ways between the WebUI and the Fluidd widget.
- **Spoolman enrichment**: for gates with `spool_id >= 0`, the daemon fetches name/material/color_hex/remaining via **Moonraker proxy first, then direct Spoolman API** (`--spoolman-url`, default `:7912`); cached 30 s. Display precedence is local gate-map value first, Spoolman second.
- **UI**: each card shows a color-painted spool icon, name, material, lane, and remaining weight; clicking the card body selects the lane (`T:n`), the ✎ button opens an inline editor (material / color picker / name / spool ID). Cards refresh on connect, after edits, and every 30 s.

## 22. Filament Usage Tracking (Standalone, No Moonraker Required)

Consumption is tracked host-side so it works without Moonraker, mirroring Happy Hare's "bill the active spool" behavior.

- **Signal**: firmware exposes a new status field `TF:<mm>` = `g_sync_mmu_total_mm`, the cumulative filament the MMU feeds during sync (≈ what the print consumes on the active lane). It only grows during sync (print), not during load/unload, so loads aren't miscounted.
- **Tracker**: a daemon thread (`filament_usage_tracker`, runs regardless of `--no-klipper`) watches `TF` deltas, attributes each positive delta to the **loaded gate's spool**, and:
  - **Spoolman reachable**: `POST /v1/spool/{id}/use {use_length: <mm>}` (Moonraker proxy first, then direct `--spoolman-url`). Spoolman computes grams from its own filament density — no math on our side.
  - **Spoolman unavailable**: accumulate per-gate `used_mm` + estimated `used_g` (1.75 mm × 1.24 g/cm³ default) in `flare_spool_usage.json`.
- **No double-counting**: if Moonraker's Spoolman integration is connected (`GET /server/spoolman/status` → `spoolman_connected`), Moonraker already bills the active spool we set (§19) from extruder moves, so the daemon tracker **skips** reporting (neither Spoolman `use` nor local) — checked, cached 15 s. The daemon only tracks when Klipper/Moonraker is not handling it (true standalone, or Spoolman used directly without the Moonraker integration).
- **Reset handling**: a `TF` rewind (board reboot) re-baselines instead of counting a negative delta.
- **UI**: the spool card shows Spoolman remaining weight when available, otherwise the locally-tracked `used Ng`. `GET /gatemap` includes `used` per gate.

## 23. Action Label ("Loading: X mm")

Fluidd's filament-path status line reads `${action}: ${filamentPosition} mm` when `mmu.action` is exactly `Loading` or `Unloading` (else it shows `Filament: X mm` / `Printing (N swaps)`). We never set `action`, so it stayed `Idle`.

- The daemon now parses the per-lane task fields `L1T`/`L2T` and derives `action` from the active lane's task + `tc_state` (`_derive_action`): toolchange `LOAD*`/`SWAP`/`RELOAD*` or lane task `AUTOLOAD`/`LOAD_FULL` → `Loading`; `UNLOAD*` or task `UNLOAD` → `Unloading`; `FEED`/`MOVE`/idle → `Idle`. It is pushed via `SET_MMU ACTION=...`.
- Combined with the synthetic `filament_position` (§20), the widget animates "Loading: X mm" / "Unloading: X mm" as the strand crosses the path sensors, then returns to "Filament: X mm" when idle.
- `L1T`/`L2T` are added only to the syncer's change-detection (not to the `_FLARE_STATE` variable list, which has no such fields).

## 24. Toolhead-Relative Cutting Sequence Tracking & Spool Update Timing

To resolve UI jumps and delayed active spool highlighting:
- **Instant Spool Update**: In `cmd_FLARE_WAIT_TC`, we instantly assign `self.active_gate`, `self.gate`, and `self.tool` to the target gate `lane - 1` right when the wait loop begins. We freeze these values during the HTTP status polling loops so the daemon does not revert them back to the old lane during the unload/cut phase.
- **Toolhead-Relative Cut Progress**: The physical cutter is located at the toolhead. During the `"cut"` phase, instead of gate-relative modeling (`0 -> 10 -> 0 mm`), we model position relative to the nozzle/cutter at `path_len` (e.g. ~1917 mm):
  - **0.0 to 1.5 s (feed forward)**: Count up from `path_len` to `path_len + 10.0` mm.
  - **1.5 to 2.5 s (cut/settle)**: Hold static at `path_len + 10.0` mm.
  - **2.5 s+ (retract)**: Count down from `path_len + 10.0` towards a safe retracted position at `path_len - 40.0` mm (using a speed of 50 mm/s).
- **Smooth Unload Start**: When transitioning to the `"unload"` phase, if `self.unload_cut` is active, the countdown starts smoothly at `path_len - 40.0` mm, aligning perfectly with the cut phase's ending position. If `self.unload_cut` is inactive, it starts at `path_len`. Both countdowns progress smoothly to `0.0` mm, completely eliminating any telemetry jumps in the Fluidd/Mainsail dashboard.

## 25. Separate Command Tracking, High-Fidelity Unload Countdown & Delayed Spool Highlighting

To address all telemetry and spool-change timing issues:
- **Separate G-Code Command Tracking**: The daemon syncer thread (`flare_daemon.py`) is updated to push `TC_STATE='{tc_state}'` in `SET_MMU`. When Klipper receives `SET_MMU` outside of a blocking `FLARE_WAIT_TC` wait loop (e.g. during a separate manual G-code command like `MMU_LOAD`, `MMU_UNLOAD`, `FLARE_LOAD`, or `FLARE_UNLOAD`), Klipper parses `TC_STATE` and `ACTION` to dynamically update `self.current_phase`. This enables identical smooth telemetry calculations in `get_status` for all background/manual movements.
- **Delayed Active Spool Highlighting**: Instead of changing the highlighted spool card immediately at the beginning of a toolchange (while the old filament is still unloading/cutting), we delay the swap. In `cmd_FLARE_WAIT_TC`, `self.active_gate`/`gate`/`tool` are initialized and held as `old_gate` during `"cut"` and `"unload"` phases. They only transition to `target_gate` when `"load"` phase physically begins, ensuring that the new spool transitions only after the previous one is fully unloaded.
- **High-Fidelity Unload leaving-TH Countdown**: To model the exact physical moment filament clears the toolhead sensor (TH) at distance `bowden_length`:
  - **Toolhead sensor active**: Clamp position to `max(bowden_length, ...)` during the initial unload to represent filament still past the sensor.
  - **Toolhead sensor clears**: Detect the edge transition to `path_toolhead = False`, capture the time, and count down smoothly from `bowden_length` to `0.0` mm, eliminating any position jumps.

## 26. Zero-Jump Cutting Sequence & Unload Completion State Gating

To completely resolve cutter jumps and gear-rebound zeroing issues:
- **Unload Completion Gating**: We introduce an explicit `self.unload_completed` state boolean. During the `"unload"` phase, once the old filament tip reaches `0.0` mm or clears the gear sensor (`not path_gear` is True), we set `self.unload_completed = True`. While `self.unload_completed` is active, Klipper permanently forces the tip position to `0.0` mm. We also transition `self.unload_completed = True` when entering the `"cut"` phase. This locks the post-cut unload states (like `UNLOAD_REVERSE`) at `0.0` mm, matching the physical fact that the filament is cleanly cut and no longer at the toolhead. We only reset `self.unload_completed = False` upon entering the `"load"` phase. This successfully prevents telemetry from jumping to `100-200` mm when the new spool's gear sensor triggers while Klipper is transitioning phases.
- **Zero-Jump Gate-Side Cutter Modeling**: Since the cutter is physically located gate-side at the MMU, the filament is already fully unloaded back to the drive gears/gate. We hold the virtual position static at `0.0` mm during the entire `"cut"` phase, avoiding any hardcoded distance constants.
- **Continuous Unload Phase Countdown**: At the start of `"unload"`, if the toolhead sensor is cleared (`not path_toolhead`), the position counts down smoothly from `bowden_length` (1800 mm) down to `0.0` mm. Once the board enters the cutting phase, the position holds at `0.0` mm.


## 27. Asynchronous Manual Movements & Cooperative Wait Loops
To make manual loads and unloads (`FLARE_LOAD`, `FLARE_UNLOAD`) cooperative and fully interactive in Mainsail/Fluidd:
- **Asynchronous Dispatching by Default**: We remove the long-running manual commands (`FL`, `UL`, `UM`) from `COMPLETION_EVENTS` in `scripts/flare_cmd.py`. These commands now always return `OK` immediately upon receipt, allowing Klipper's main thread to continue execution without freezing.
- **Cooperative Unload Wait**: We register the `FLARE_WAIT_UNLOAD` G-code command in Klipper (`mmu.py`) which cooperatively polls the board's daemon status. It pauses slightly to let the board task initialize, then waits until the active lane's motor task returns to `IDLE` before exiting, allowing `get_status` progress countdowns to run smoothly and natively.
- **Cooperative Load Wait**: We chain `FLARE_WAIT_TC` to `FLARE_LOAD` after the asynchronous `FL:` command is issued, driving the load progress bar smoothly up to `bowden_length` without freezing the main thread.


## 28. Unified Phase Transitions & Seamless Toolhead Unload Tracking
To guarantee perfect telemetry progress updates without any jumps or cuts being missed:
- **Immediate Toolhead Unload Tracking (`FLARE_START_UNLOAD`)**: We introduce a new `FLARE_START_UNLOAD` Klipper command and call it at the very beginning of the `FLARE_UNLOAD_TOOLHEAD` macro. This ensures Klipper immediately transitions `current_phase = "unload"`, meaning the virtual position counts down smoothly from `1925` to `1800` mm *during* Klipper's extruder gear retract, seamlessly crossing into the bowden tube without any jumps when the toolhead sensor clears.
- **Unified `_update_phase` Helper**: We extract all virtual phase transition mapping logic into a single robust helper method `self._update_phase(tc_state, action, now)`. We call this unified method inside Klipper's background status receiver (`cmd_SET_MMU`) and both cooperative wait loops (`FLARE_WAIT_TC` and `FLARE_WAIT_UNLOAD`). This guarantees 100% synchronous and identical virtual phase resolution under all circumstances, fully correcting the manual unload cut-phase telemetry jumps.
## 29. Telemetry Race Protection & Interactive Load Telemetry

To ensure a perfectly smooth and continuous progress tracking during physical filament loading:
- **Full Path Loading Telemetry**: The virtual progress calculation during the `"load"` phase is scaled up to the full path length `path_len` (1925 mm) instead of capping at `bowden_length` (1800 mm). The Mainsail/Fluidd UI progress bar now counts up smoothly all the way to 100% (1925 mm) instead of stopping at 93% (1800 mm) and jumping abruptly upon toolhead insertion.
- **Post-Load Race Shielding**: Added toolhead-sensor triggered guard gates inside Klipper's `_update_phase` helper. When the toolhead sensor is active, Klipper immediately forces the current phase to `"idle"`. This shields Klipper from stale background `SET_MMU` commands sent by the 4Hz daemon during toolhead insertion, preventing Klipper from reverting to `"load"` and snapping the position back to `0.0` mm.


## 30. Real-Time Dynamic Filament State Resolution

To prevent status display freeze during G-code queue pauses:
- **Dynamic State Derivation inside `get_status`**: `self.filament` and `self.filament_pos` are calculated dynamically in real-time inside Klipper's `get_status` method based on active sensor triggers (`path_gear`, `path_toolhead`) and the current virtual phase (`current_phase == "load"` or `current_phase == "unload"`).
- **Cooperative Wait Loop Protection**: Because Klipper's G-code parser blocks G-code queue execution during cooperative wait loops (like `FLARE_WAIT_TC`), background `SET_MMU` updates are queued. Resolving the filament loaded state dynamically inside `get_status` (which is queried continuously by Fluidd/Mainsail) guarantees that the UI immediately and accurately reflects `"Partially Loaded"` as soon as the drive gears grip the filament or loading begins, bypassing the queued G-code bottleneck.


## 31. Seamless Unload Telemetry Wait Loop Carry

To guarantee perfect countdown continuity throughout the entire toolhead-to-lane retraction flow:
- **Unload Timer Protection**: The active unload virtual tracking timers (`self.unload_phase_start` and `self.th_clear_time`) inside `cmd_FLARE_WAIT_UNLOAD` and `cmd_FLARE_WAIT_TC` are guarded to prevent them from being reset if Klipper is already in the `"unload"` phase.
- **Continuous Countdown Flow**: The smooth countdown from `1925` to `1800` mm initiated by `FLARE_START_UNLOAD` at the very beginning of the `FLARE_UNLOAD_TOOLHEAD` macro is fully preserved when entering Klipper's synchronous wait loops. This prevents the virtual position from jumping back to `1925` or `1800` mm, realizing a 100% continuous, jump-free telemetry transition.


## 32. Regression Re-Analysis — Standalone Load/Unload/Cut Tracking + Drawn Tip (2026-05-28)

Operator observation contradicts the "smooth / jump-free / interactive" claims of
§§20, 23, 24, 25, 26, 29, 30, 31. **Toolchange animates correctly; the three
standalone flows and the drawn tip do not.** Reported behavior:

1. Standalone **load** (`MMU_LOAD` / `FLARE_LOAD`): no count-up — only `0`
   (string "Unloaded" flickers in) then a jump to the full path `1925` mm.
2. Standalone **unload** (`MMU_UNLOAD` / `FLARE_UNLOAD`): on full unload it starts
   a phantom *loading* count-up instead of holding `0`; the real unload is not
   tracked down.
3. **Cut** travel shows `1800` mm (≈ `bowden_length`) instead of holding `0`,
   then dropping to `0` on the post-cut unload.
4. The **drawn filament tip** never moves with the strand — it sits at one of
   three fixed spots (low / middle / high).

### Architecture as built (the two-track synthesizer)

```
flare_daemon ──push──► cmd_SET_MMU (periodic ~4-20Hz)
                         line 206:  if not self.is_loading: _update_phase(...)   ◄── GATE
gcode wait  ──poll──► FLARE_WAIT_TC      sets is_loading=True   (loop owns phase)
loops                FLARE_WAIT_UNLOAD   does NOT set is_loading (loop + daemon both write)
                         │ both call _update_phase() each 0.2s
                         ▼
Moonraker  ──query──► get_status() ~4Hz
                         filament_position = elapsed * loading_speed   (open-loop dead-reckon)
                         filament_pos      ∈ {0, 4, 10}                (discrete → drawn tip)
```

`get_status` fabricates `filament_position` purely from `now - *_phase_start`
times `loading_speed` (default 50 mm/s), gated by `current_phase ∈ {load,
unload, cut, else}`. There is **no background reactor timer**: outside the
synchronous wait loops, the only phase writer is the daemon's `SET_MMU` push.

### Root causes (all in `klipper/mmu.py` unless noted)

- **`is_loading` gate asymmetry** (`cmd_SET_MMU:206`, `if not self.is_loading`).
  `is_loading` is set True **only** by `FLARE_WAIT_TC` (`:929`) and cleared only
  in its `finally` (`:1002`). So during a toolchange the daemon push is gated
  off and the wait loop is the sole phase writer → clean. `FLARE_WAIT_UNLOAD`
  never sets `is_loading`, so during a manual unload the daemon push **and** the
  wait loop both call `_update_phase` → two writers race.

  | Flow | is_loading | phase writers | result |
  |------|-----------|---------------|--------|
  | Toolchange (→WAIT_TC) | True | loop only (push gated off) | clean ✓ |
  | Standalone unload (→WAIT_UNLOAD) | **False** | loop **+** daemon push | race ✗ (#2) |
  | Standalone load (→WAIT_TC) | True | loop only | timing defect, see below (#1) |

- **No terminal phase reset in `FLARE_WAIT_UNLOAD`** (contrast `FLARE_WAIT_TC`
  `finally: current_phase="idle"`, `:1003`). After unload, a daemon push of
  `action="Loading"` / a `LOAD_*` `tc_state` flips `current_phase="load"`,
  `load_phase_start=now` (`_update_phase:1094/1104`) → `get_status` counts UP
  from 0 → **phantom load (#2)**, and nothing resets it to idle.

- **Cut detection window too narrow** (`_update_phase:1089`): `"cut"` is set only
  for `tc_state ∈ {UNLOAD_WAIT_CUT, UNLOAD_CUT}`. During cut *travel* the board
  reports another state, so the phase stays `"unload"` with `path_toolhead`
  still true → `get_status:1325` `max(bowden_length, …)` clamps at **1800 (#3)**
  instead of the cut branch's `0.0` (`:1328`). §26's `unload_completed`-on-cut
  latch never fires because the cut phase is never entered.

- **Open-loop load timing** (`get_status:1330`, `filament_position = min(path_len,
  elapsed*loading_speed)`): `load_phase_start` is anchored at `FLARE_WAIT_TC`
  *entry* (`:927/955/958`), but `_FLARE_CHANGE_LANE` runs `FLARE_UNLOAD_TOOLHEAD`
  first — `_FLARE_CG28` + `_FLARE_HEAT_HOTEND` + `TEMPERATURE_WAIT` to load_temp
  — before `TC:`. That heat dead-time is counted as elapsed, and `loading_speed`
  (50 mm/s display) is decoupled from the real firmware feed, so the value
  either races to `1925` and sits, or never visibly progresses → **two-step
  jump (#1)**. §29's "full path" scaling is correct; the *anchor and rate* are
  the defect.

- **Discrete `filament_pos`** (`get_status:1295/1298/1301`, and `cmd_SET_MMU:300/
  307/310`): only `{0, 4, 10}` are emitted. The drawn tip is keyed to this enum
  (and the binary `sensors['toolhead']`), **not** the continuous
  `filament_position` mm — so it can occupy only three positions. During the
  whole load animation `filament_pos==4`, then snaps to 10. The mm value
  animates as text; the graphic cannot.

  **RESOLVED from the Fluidd source** (`src/components/widgets/mmu/
  MmuFilamentStatus.vue` + `src/mixins/mmu.ts`, develop): the drawn tip
  (`filamentRect` height) is discrete on `filament_pos` for every value
  **except** `START_BOWDEN (2)` / `IN_BOWDEN (3)`, where it interpolates
  `START_BOWDEN → endOfBowdenPos` by `mmu.bowden_progress` (a dedicated 0–100
  field, `bowdenProgress = mmuState.bowden_progress ?? -1`; interpolation only
  runs when `bowden_progress >= 0`). `filament_position` (mm) feeds **only** the
  text readout (`filamentPosition`), never the tip. FLARE emitted neither
  `filament_pos ∈ {2,3}` nor `bowden_progress`, so the interpolation branch
  never fired → three fixed stops (`0→`bottom, `4→`mid, `10→`top). HH enum:
  `-1 UNKNOWN, 0 UNLOADED, 1 HOMED_GATE, 2 START_BOWDEN, 3 IN_BOWDEN,
  4 END_BOWDEN, 5 HOMED_ENTRY, 6 HOMED_EXTRUDER, 7 EXTRUDER_ENTRY, 8 HOMED_TS,
  9 IN_EXTRUDER, 10 LOADED`.

### Fix directions (see Phase 37 tasks)

1. **Unify phase ownership**: generalize the `is_loading` gate (or add
   `is_unloading`) so the daemon `SET_MMU` push skips `_update_phase` during
   `FLARE_WAIT_UNLOAD` too — one writer per synchronous flow.
2. **Terminal reset**: give `FLARE_WAIT_UNLOAD` a `finally`-style
   `current_phase="idle"` so a finished unload settles at `0` and cannot leak to
   `"load"`.
3. **Harden `"unload"→"load"`**: require real load evidence (gate/out rise after
   empty path, or a fresh load command) before entering `"load"`, not a bare
   `action="Loading"` hint.
4. **Cut latch**: once a cut is entered (or `unload_cut`/`enable_cutter` is set
   for the unload) hold `0` through the trailing reverse states regardless of the
   exact `tc_state` string.
5. **Sensor-anchored load**: start `load_phase_start` at first real feed (first
   non-idle load `tc_state` / first gate-sensor rise), and align `loading_speed`
   to the board-reported feed (or interpolate between sensor crossings).
6. **Tip motion (RESOLVED + implemented)**: `get_status` now refines
   `filament_pos` onto HH landmarks from the synthetic mm tip and publishes
   `bowden_progress` (0–100) so Fluidd interpolates the tip across the bowden:
   `IN_BOWDEN (3)` + `bowden_progress = filament_position / bowden_length * 100`
   while in the tube, `HOMED_GATE (1)` just past the gate, `EXTRUDER_ENTRY (7)`
   past end-of-bowden, `LOADED (10)` at the toolhead, `UNLOADED (0)` when clear.
   Graphic-only — the `filament` string (button gating) is unchanged. Still
   wants on-hardware confirmation that the tip glides and buttons stay correct.

These supersede the "perfectly smooth / 100% jump-free / interactive" assertions
in §§24–31 for the **standalone** flows; the toolchange path is unaffected and
must stay behavior-equivalent.


## 33. Bypass UI — Hide Buffer Piston; Lane Buttons Not Field-Disable-able (2026-05-29)

Bypass feeds the extruder directly: no lane, no buffer. Two asks — hide the
buffer/sync piston, and limit actions to load/unload.

- **Piston (achievable via fields):** Fluidd's `MmuFilamentStatus` renders the
  sync-feedback piston under `v-if="hasSyncFeedback"`, where `hasSyncFeedback =
  hasSensor('filament_compression') || hasSensor('filament_tension') || …` —
  keyed purely on those keys existing in `mmu.sensors`. `get_status` previously
  emitted both keys unconditionally; it now omits them in bypass (alongside the
  already-omitted `mmu_pre_gate/gear/gate`), so the piston disappears. `toolhead`
  is kept (the filament really is at the toolhead). Matches the standalone WebUI
  already hiding its buffer panel in bypass.

- **Buttons (NOT field-controllable in this Fluidd):** `MmuControls` gates
  Preload on `currentGateStatus ∈ {AVAILABLE, AVAILABLE_FROM_BUFFER}`, Eject on
  `== EMPTY`, and Load/Unload on `filamentPos` (Load disabled unless
  `UNLOADED`). In bypass `gate = -2` → `gate_status?.[-2] ?? -1`, so Preload and
  Eject never trip their conditions; Check Gate and Recover have no gate gating
  at all (just `!klippyReady || !canSend`). So the four lane buttons cannot be
  visually disabled by manipulating `printer.mmu` — only a Fluidd change/upgrade
  could. FLARE already uses the documented fallback (see the `cmd_MMU_RECOVER`
  comment: "Fluidd's Recover button cannot be disabled from the mock"): make the
  handlers safe in bypass instead. Load/Unload are already correct
  (`filament_pos = 10` loaded → Load off, Unload on). Eject already bypass-guards
  (`cmd_MMU_EJECT`), Recover is a no-op, Check Gate is read-only (`?:` + print);
  added a bypass guard to `cmd_MMU_PRELOAD` (it would otherwise spin a lane
  gear). Net: every non-load/unload button is a safe no-op in bypass even though
  Fluidd still draws it enabled.
