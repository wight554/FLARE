# External Integrations & Hardware Protocols

**Analysis Date:** 2026-09-11

This document specifies all communication protocols, host interfaces, Klipper macro contracts, and hardware peripheral integrations across the FLARE ("Filament Lane Automation and Reload Engine") system.

---

## 1. USB CDC Serial Wire Protocol

The physical transport between the host (Klipper Raspberry Pi) and the RP2040 microcontroller is USB CDC (Communications Device Class) running at 115200 baud over virtual serial (`/dev/ttyACM*` or `/dev/cu.usbmodem*`).

### 1.1 Wire Framing & Transport Semantics

The protocol is human-readable ASCII, line-buffered, and terminated by newline (`\n`, with optional ignored `\r`):

- **Line Budget & Buffer Limits** (`firmware/src/protocol.c:27-30`):
  - Line accumulation buffer: 64 bytes (`CMD_PARAM_MAX 64`).
  - Polling budget: max 128 bytes or 4 commands per superloop tick (`CMD_POLL_BYTE_BUDGET 128`, `CMD_POLL_COMMAND_BUDGET 4`).
  - Event burst limiter: max 8 events per 100 ms sliding window (`CMD_EVENT_WINDOW_MS 100`, `CMD_EVENT_BUDGET 8`) to protect superloop timing from USB backpressure. Critical fault-class events bypass this budget limiter.
- **Request Format**:
  ```
  CMD:PAYLOAD\n
  ```
  Payload may be empty (e.g. `LO:\n` or `VR\n`).
- **Response Format**:
  Every command guarantees exactly one immediate reply line:
  ```
  OK:DATA\n      (Command accepted and/or immediate status/data returned)
  ER:REASON\n    (Command rejected or syntax/state machine error)
  ```
- **Event Format**:
  Unsolicited notifications emitted asynchronously at any point:
  ```
  EV:TYPE:DATA\n
  ```

### 1.2 Synchronous Contract vs Asynchronous Completion

Commands in FLARE fall into two operational classes:

1. **Immediate / Non-Blocking**:
   Commands like `T:n`, `BS:`, `ST:`, `TS:<0|1>`, `SET:...`, `GET:...` execute immediately within the superloop tick and return `OK:` or `ER:`.
2. **Asynchronous Long-Running Operations**:
   Commands that trigger mechanical motion (`FL:`, `UL:`, `UM:`, `RL:`, `CU:`, `CX:`) immediately return `OK` to release serial parser lock, then emit asynchronous `EV:...` lines as milestones are reached.
   The host client (`scripts/flare_cmd.py`) maintains an explicit map of terminal completion and failure events (`COMPLETION_EVENTS`):

| Command Verb | Success Completion Event (`ok_evs`) | Error Event (`err_evs`) |
|---|---|---|
| `RL:` (Reload Load) | `EV:RELOAD:LOADED` | `EV:TC:ERROR` |
| `CU:` (Cut Sequence) | `EV:CUT:DONE` | `EV:CUT:ERROR` |
| `CX:` (Bare Cut) | `EV:CUT:DONE` | `EV:CUT:ERROR` |

*Note on Toolchange (`TC:n`) and Exact Move (`MV:`)*: Both return `OK` after initiating motion. This allows Klipper to execute coordinated extruder/gantry moves while firmware lane motion proceeds concurrently.

### 1.3 Full Command Taxonomy

#### Motion Commands (`firmware/src/protocol.c`)
- `T:<1|2>`: Select active filament lane without motion. Returns `ER:BUSY:LANE` if motion active.
- `LO:`: Preload active lane forward until drive gear `OUT` sensor triggers. Limit: `AUTOLOAD_MAX`.
- `FL:`: Full load forward until toolhead sensor (`TS:1`), timeout, or buffer geometry triggers loaded state. Limit: `LOAD_MAX`.
- `RL:`: Retrigger autonomous runout reload sequence from current sensor state.
- `UL:`: Retract active lane until `OUT` sensor clears. If cutter is configured and `UNLOAD_CUT=1`, coordinates retract-cut-retract. Limit: `UNLOAD_MAX`.
- `UM[:<1|2>]`: Unload active or specified lane until entrance `IN` sensor clears.
- `TC:<1|2>`: Orchestrated toolchange to target lane (awaits toolhead clear, unloads, cuts, swaps, and loads).
- `MV:<mm>:<F>[:<D>][:I]`: Exact move `mm` at `F` mm/min. `D` is optional direction (`F`/`R`). `I` ignores buffer tension/compression bounds.
- `FD:`: Continuous open-loop feed until explicit `ST:`.
- `BS:`: Buffer stabilize. Cancels compatible buffer operations and returns carriage toward `NEUTRAL`.
- `ST:`: Emergency stop. Aborts all active lane tasks, halts PWM step generation, and clears toolchange state.
- `CU:`: Full cut sequence on active lane (Open -> Feed -> Close -> Open -> Repeat -> Block). Requires lanes idle and preloaded (`IN=1`, `OUT=0`).
- `CX:`: Bare cut sequence without feeding filament.
- `CP:<us>`: Set cutter servo pulse width directly (400–2700 µs) for mechanical calibration.

#### Buffer Lock Commands
- `BL:T:<dist>:<rate>`: Arm active lane to pre-tension buffer against carriage spring up to `<dist>` mm at `<rate>` mm/min, then hold.
- `BL:C:<dist>:<rate>`: Arm active lane to pre-compress buffer against carriage spring.
- `BL:R`: Disarm / release buffer lock.

#### Status & System Commands
- `?:`: Full telemetry dump (detailed in Section 1.4).
- `VR:`: Returns firmware version string (`OK:FLARE_0.2.0`).
- `TS:<0|1>`: Toolhead sensor synchronization edge pushed by host.
- `MARK:<tag>`: Stores telemetry marker for log tagging (returned in subsequent `?:` as `MK:<seq>:<tag>`).
- `SET:<param>:<val>`: Modifies live configuration parameter.
- `GET:<param>`: Queries live configuration parameter value.
- `SV:`: Persists active runtime settings to RP2040 NOR flash sector.
- `LD:`: Reloads persisted settings from flash.
- `RS:`: Resets runtime settings to compiled defaults in `firmware/include/tune.h`.
- `BOOT:`: Reboots MCU into USB mass storage BOOTSEL mode (`reset_usb_boot(0, 0)`).

#### Low-Level Diagnostics (`firmware/src/protocol_tmc.c`)
- `TR:<lane>:<reg>`: Read raw 32-bit TMC2209 register value over UART.
- `TW:<lane>:<reg>:<val>`: Write raw 32-bit TMC2209 register value over UART.
- `RR:<lane>`: Probe TMC UART addresses `0..3` and return raw datagram frames.

### 1.4 Telemetry Status (`?:`) Payload Schema

Executing `?:` produces a comma-separated key-value status response formatted as `OK:<field>:<val>,<field>:<val>,...`:

| Key | Units / Type | Description |
|---|---|---|
| `LN` | `1` \| `2` | Active lane ID. |
| `TC` | string | Toolchange state (`IDLE`, `UNLOADING`, `SWAPPING`, `LOADING`, `ERROR`). |
| `L1T`, `L2T` | string | Current motor task on Lane 1 and Lane 2 (`IDLE`, `FEED`, `RETRACT`, etc.). |
| `I1`, `O1` | `0` \| `1` | Lane 1 inlet (`IN`) and drive-gear (`OUT`) optomechanical switches. |
| `I2`, `O2` | `0` \| `1` | Lane 2 inlet (`IN`) and drive-gear (`OUT`) optomechanical switches. |
| `TH` | `0` \| `1` | Host-reported toolhead filament sensor state. |
| `YS` | `0` \| `1` | Y-splitter junction filament sensor state. |
| `BUF` | string | Discrete buffer state (`NEUTRAL`, `COMPRESSION`, `TENSION`, `UNKNOWN`). |
| `MM` | mm/min | Commanded MMU motor feed rate. |
| `BF` | mm/min | Active control baseline rate. |
| `BP` | mm | Continuous virtual or physical buffer position. |
| `SM` | `0` \| `1` | Sync mode active flag. |
| `BL` | `T` \| `C` \| `0` | Buffer lock arm state. |
| `ST` | integer | Discrete sync controller state ID. |
| `EST` | mm/min | Extruder demand velocity estimated from buffer transitions. |
| `RE` | mm | Reserve position error relative to setpoint. |
| `AV` | mm/s | Damped buffer trolley velocity. |
| `SC` | mm/min | TMC StealthChop / SpreadCycle switching threshold. |
| `RT` | mm | Target reserve setpoint. |
| `TT`, `CT` | ms | Continuous dwell time at tension or compression physical rails. |
| `CF` | 0.0–1.0 | Estimator signal confidence. |
| `ES` | mm | Estimator uncertainty ($\sigma$). |
| `TPX` | integer | Rolling count of tension pin entries within `TENSION_RISK_WINDOW_MS`. |
| `MK` | `seq:tag` | Active telemetry marker sequence number and string tag. |

### 1.5 Unsolicited Event (`EV:`) Taxonomy

- `EV:AUTO_LOAD:<lane>`: Automatic load triggered upon filament insertion into empty unit.
- `EV:PRELOAD:<lane>`: Filament detected at inlet; auto-preloading to drive gear `OUT` sensor.
- `EV:RUNOUT:<lane>`: Active lane inlet sensor cleared during printing.
- `EV:LOADED:<lane>`: Filament arrived at toolhead or drive gear.
- `EV:UNLOADED:<lane>`: Filament cleared drive gear or entrance sensor.
- `EV:LOAD_TIMEOUT:<lane>`: Move exceeded `LOAD_MAX` distance.
- `EV:UNLOAD_TIMEOUT:<lane>`: Move exceeded `UNLOAD_MAX` distance.
- `EV:FAULT:DRY_SPIN:<lane>`: Motor energized $> 8$ s without filament presence (`IN=0`). Sticky safety interlock.
- `EV:SYNC:<subevent>`: Sync state shifts (`AUTO_START`, `AUTO_STOP`, `FAULT_HOLD`, `FAULT_HOLD_RECOVERY`, `TENSION_DWELL_WARN`, `TENSION_RISK_HIGH`).
- `EV:BUF:EST_LOW_CF`: Estimator confidence dropped below warning threshold.
- `EV:BUF:EST_FALLBACK`: Estimator uncertainty exceeded hard cap; fallback to relay control.
- `EV:BUF_STAB:<status>`: Buffer neutralization progress (`START`, `DONE`, `TIMEOUT`, `STAGNANT_TIMEOUT`).
- `EV:BL:<phase>`: Buffer lock lifecycle (`PRIME`, `LOCKED`, `FOLLOW`, `FOLLOW_DONE`, `TIMEOUT`).
- `EV:TC:<phase>`: Toolchange progression (`TC:UNLOADING`, `TC:SWAPPING`, `TC:LOADING`, `TC:DONE`, `TC:ERROR`).
- `EV:RELOAD:<phase>`: Autonomous spool redundancy failover (`RELOAD:SWITCHING`, `RELOAD:JOINING`, `RELOAD:LOADED`, `RELOAD:FAULT`).
- `EV:CUT:<status>`: Servo blade cycle state (`FEEDING`, `DONE`, `ERROR`).
- `EV:FLASH:WEAR_WARNING`: Cumulative NOR flash erase cycles crossed 80,000 operations.

---

## 2. Klipper Host Architecture & Tooling

To ensure concurrent access without USB serial port conflicts between Klipper print macros and Moonraker/Mainsail web dashboards, FLARE isolates port ownership in a background daemon.

```
                  +-----------------------------------+
                  |         Mainsail / Fluidd         |
                  +-----------------------------------+
                       │ HTTP/WebSocket   │ SSE/REST (Port 8088)
                       ▼                  ▼
+-----------------------------+     +-----------------------------+
|          Moonraker          |     |     flare_daemon.py         |
|   (Printer API / Spoolman)  |     |   - Port Arbitrator         |
+-----------------------------+     |   - 20 Hz SSE Stream        |
       │ UDS / Unix Socket          |   - Moonraker Syncer (4 Hz) |
       ▼                            |   - SQLite Cache (flare.db) |
+-----------------------------+     +-----------------------------+
|         Klippy Core         |                   │
|  klipper/mmu.py (Mock)      |                   │
|  klipper/flare_mmu.cfg      |                   │
+-----------------------------+                   │
       │ RUN_SHELL_COMMAND                        │
       ▼                                          │
+-----------------------------+                   │
|    scripts/flare_cmd.py     |──HTTP POST /cmd───┘
+-----------------------------+                   │
                                                  ▼ pyserial (115200)
                                    +-----------------------------+
                                    |    RP2040 ERB Controller    |
                                    +-----------------------------+
```

### 2.1 The Background Daemon (`scripts/flare_daemon.py`)

- **Role**: Exclusive owner of the physical USB serial port (`serial.Serial(port, 115200, timeout=0.5, exclusive=True)`).
- **HTTP REST & SSE Server**: Runs an internal multithreaded HTTP server on port `8088`:
  - `GET /status`: Returns JSON snapshot of parsed board status.
  - `GET /telemetry`: Server-Sent Events (SSE) broadcasting real-time board events at up to 20 Hz.
  - `POST /cmd`: Accepts JSON payload `{"cmd": "..."}`, acquires the serial lock, sends the line to the controller, waits for `OK:` or `ER:`, and returns JSON response.
  - `GET /gatemap` & `POST /gatemap`: Reads/writes spool assignments and active tool mappings.
- **Persistent State Database**: SQLite store at `~/.local/share/flare/flare.db` (override via `FLARE_DATA_DIR`) persisting MMU toolchange counts (`swaps_total`, `swaps_success`, `swaps_failed`) and Spoolman mappings across restarts.
- **Klipper Syncer Thread (`klipper_syncer`)**:
  - Periodically queries Moonraker (`http://localhost:7125/printer/objects/query?idle_timeout`) at 4 Hz.
  - Pauses UI update pushes when Klipper is busy running blocking operations (e.g. `MPC_CALIBRATE`) to avoid starving the G-code execution lock.
  - Synchronizes active gate spool IDs with Moonraker's Spoolman integration (`/server/spoolman/spool_id`).

### 2.2 CLI & Macro Transport (`scripts/flare_cmd.py`)

Klipper G-code macros communicate with FLARE via `gcode_shell_command` invoking `flare_cmd.py`:
- **Daemon Proxy Mode** (Default): Detects running daemon at `http://127.0.0.1:8088`. Routes command via HTTP `POST /cmd`. For commands in `COMPLETION_EVENTS`, it opens an SSE listener to await terminal events.
- **Direct Serial Fallback**: If the daemon is offline, directly discovers `/dev/tty.usbmodem*` or `/dev/ttyACM*` and opens exclusive serial connection.
- **Dump & Configuration Sync** (`--dump`): Iterates across all parameters in `DUMP_PARAMS` via `GET:<key>` and outputs valid, formatted `config.ini` syntax.

### 2.3 Klipper Happy-Hare Mock (`klipper/mmu.py`)

To provide native Multi-Material status panels in Mainsail and Fluidd without requiring the full Happy-Hare software suite:
- `klipper/mmu.py` registers the mock `mmu` and `mmu_machine` objects with Klippy.
- Implements `get_status(eventtime)` exposing gate configurations (`gate_status`, `gate_sensor`, `gate_color`, `gate_material`, `gate_spool_id`).
- Mainsail/Fluidd discover the unit as a `VirtualSelector` MMU, enabling native gate selection, color indicators, and load/eject buttons.

### 2.4 G-Code Macros (`klipper/flare_mmu.cfg`)

Configured in `printer.cfg` via `[include flare_mmu.cfg]`:
- **Toolchange Routine (`T0`, `T1`)**:
  Coordinates toolhead tip forming, retracts filament into the buffer, executes `_FLARE_CHANGE_LANE`, unloads the active spool past the Y-splitter, loads the target spool, and pushes filament to the nozzle.
- **Buffer-Locked Retract (`_FLARE_BL_RETRACT`)**:
  Commands `BL:T:<length>:<rate>` to the ERB controller, primes the buffer carriage into tension, then commands printer extruder retract `G0 E-<length>`. MMU follows open-loop, eliminating tip distortion caused by bowden slack.
- **Tip Forming (`_FLARE_TIP_FORMING`)**:
  Executes high-speed forward push, cooling pull, secondary smoothing moves, and dip pauses configured via `_FLARE_TIP_FORMING_DEFAULTS`.
- **Toolhead Reconcile (`_FLARE_SYNC_TOOLHEAD`)**:
  Reads physical microswitch sensor on the toolhead, forces synchronization to the board (`TS:1` or `TS:0`), and runs `_FLARE_BUFFER_STABILIZE` (`BS`). Wired into `PRINT_START`, `PRINT_END`, and `CANCEL_PRINT`.
- **Slicer Purge Coordination (`_FLARE_SET_PURGE`)**:
  Allows slicers (e.g. OrcaSlicer) to pass dynamic flush volumes per transition:
  ```gcode
  _FLARE_SET_PURGE PURGE=[flush_length]
  ```

---

## 3. Hardware Peripheral Interfaces

### 3.1 Buffer Sensors (Type-D vs Type-P)

FLARE maintains mechanical filament equilibrium between the spool drive and the printer toolhead using an inline spring buffer.

```
       Extruder Pulls (Tension) ◄──[ Carriage ]──► MMU Overfeeds (Compression)
                                        │
           +----------------------------+----------------------------+
           │                                                         │
   [ Type-D Switches ]                                       [ Type-P Analog ]
   Tension: GPIO 18                                          Hall/Pot: GPIO 29 (ADC3)
   Compression: GPIO 12                                      Normalized: 0.0 .. 1.0
```

#### Path A: Type-D Dual-Endstop Switch Buffer (`buf_sensor_type = 0`)
- **Tension Switch**: GPIO 18 (pin header adjacent to GPIO 12). Closed when extruder draws filament faster than MMU feeds.
- **Compression Switch**: GPIO 12 (labeled `PRE_GATE_0` on ERB v2.0). Closed when MMU feeds faster than extruder draws.
- **Control Strategy**: Demand-reconstructing hybrid relay law. Integrates step counts between switch crossings, applies asymmetric catchup/drain multipliers (`relay_catchup_frac`, `sync_compression_drain_frac`), and estimates extruder consumption rate (`EST`).

#### Path B: Type-P Proportional / Hall-Effect Buffer (`buf_sensor_type = 1`)
- **Analog Signal Wire**: GPIO 29 / ADC Channel 3 (`PIN_PSF 29`). Reads 0–3.3V representing exact carriage position.
- **Calibration**:
  - Homing extreme tension calibrated via `CAL:PSF_TENS` (default normalized: `0.0`).
  - Homing extreme compression calibrated via `CAL:PSF_COMP` (default normalized: `1.0`).
  - Resting center calibrated via `CAL:PSF_NEUT` (default normalized: `0.5`).
- **Control Strategy**: Continuous Proportional-Derivative (PD) loop (`psf_control_law` in `firmware/src/sync_analog.c`). Applies velocity damping (`KD_PSF`), soft-wall deceleration boundaries (`CONF_PSF_SOFT_WALL_START`), and moving average setpoint filters.

### 3.2 Filament Detection Switches

All switches are debounced in software over a 30 ms sliding window (`firmware/src/main.c:debounced_input_update`):

- **Lane 1 Entrance (`IN`)**: GPIO 2 (`X-MIN`). Triggers `EV:PRELOAD:1` when filament is inserted from spool.
- **Lane 1 Drive Gear (`OUT`)**: GPIO 3 (`X-MAX`). Signals successful grip and stops preload feed.
- **Lane 2 Entrance (`IN`)**: GPIO 4 (`Y-MIN`). Triggers `EV:PRELOAD:2`.
- **Lane 2 Drive Gear (`OUT`)**: GPIO 5 (`Y-MAX`). Signals successful grip on Lane 2.
- **Y-Splitter Sensor**: GPIO 6 (`I2C1_SDA` pin header). Verifies filament has fully cleared the merge zone before switching lanes.
- **Toolhead Sensor**: Connected to printer toolhead controller (CAN or gantry board). Synchronized over USB via macro command `TS:<0|1>`.

### 3.3 Blade Cutter Servo

For toolheads without integrated filament cutters, FLARE operates an automated mechanical blade cutter on the MMU selector:

- **Signal Pin**: GPIO 23 (`SERVO` header).
- **PWM Configuration**:
  - Base frequency: 50 Hz (20 ms period frame via `SERVO_PWM_PERIOD_US 20000u`).
  - PWM clock divider: 125.0 (`SERVO_PWM_CLKDIV 125.0f`), clocking the 125 MHz sysclk down to exactly 1 µs counter resolution. Channel level corresponds directly to pulse width in microseconds.
- **Pulse Width Timings**:
  - Open position: default 500 µs (`servo_open_us`).
  - Cut / Close position: default 1400 µs (`servo_close_us`).
  - Block / Park position: default 950 µs (`servo_block_us`).
- **Actuation Sequencing** (`firmware/src/cutter.c`):
  1. `CUT_OPENING`: Servo moves to `servo_open_us`; dwells for `servo_settle_ms`.
  2. `CUT_FEEDING`: Motor feeds filament forward by `cut_feed_mm` at `cut_feed_rate`.
  3. `CUT_CLOSING`: Servo drives blade to `servo_close_us`; dwells for settle delay.
  4. `CUT_REOPENING`: Servo retracts blade to `servo_open_us`.
  5. `CUT_REPEAT_CHECK`: If configured for multiple cuts (`cut_amount`), repeats cycle.
  6. Final state: Parks blade at `servo_block_us` and disables PWM to prevent servo buzz.

### 3.4 Status NeoPixel Indicator

- **Signal Pin**: GPIO 21 (`NEO_PIXEL`).
- **Driver**: RP2040 PIO block 1 (`pio1`) executing `firmware/third_party/ws2812.pio`.
- **Signaling Format**: 800 kHz NRZ bitstream shifting out 24-bit GRB data (Green-Red-Blue wire ordering).
- **State Indication**:
  - Solid White: Normal idle standby.
  - Solid Green: Active lane synchronized and feeding.
  - Pulsing Blue: Buffer stabilization or homing in progress.
  - Solid Orange: Filament runout detected; awaiting reload or intervention.
  - Blinking Red: Motor dry-spin or hardware timeout fault.

---

*Pre-commit compliance: Run `clang-format -i firmware/src/*.c firmware/include/*.h` and `ruff check scripts/` before submitting pull requests.*
<!-- refreshed: 2026-09-11 -->
