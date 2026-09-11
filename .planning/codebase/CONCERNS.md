# Technical Concerns, Debt, and Operational Risks

**Analysis Date:** 2026-09-11

This document provides a comprehensive, prescriptive catalog of known technical debt, architectural constraints, race conditions, hardware-coupling hazards, and electrical safety considerations across the FLARE embedded firmware and host tooling stack.

---

## 1. Executive Summary

FLARE operates in a hybrid bare-metal and distributed host environment. While its physical isolation philosophy (autonomous on-device safety, local runout recovery without host dependence) provides resilience against host disconnections, the codebase contains structural debt and hardware constraints that require strict discipline from future developers:

1. **Watchdog & Superloop Timing Constraints**: The RP2040 operates a single-threaded cooperative superloop bounded by a strict 1000 ms hardware watchdog (`firmware/src/main.c:575`). Any blocking operation—including flash erases or multi-attempt TMC UART reads—risks triggering unrecoverable hardware resets.
2. **Buffer Catch & Retract Dynamics (Phase 2)**: Fast extruder retracts (30–60 mm/s) outpace baseline buffer priming rates (10 mm/s), exacerbated by an asynchronous host CLI race where commands return before MCU prime completes, slamming the Type-P proportional carriage into mechanical compression stops.
3. **Hardware-Coupled Verification Gap (Phase 1)**: Core firmware state-machine fixes (e.g., flash write activity gating, cutter re-entry guards, runout escalation paths) are code-complete and simulated, but critically depend on pending physical bench validation on real rigs.
4. **TMC2209 Single-Wire UART Hazards**: Bit-banged PIO communication over direct single-wire traces lacks bus isolation, carries strict bus turnaround timing requirements, risks trapping drivers in silent standby mode, and provides no runtime heartbeat to detect power-loss register erasure.
5. **Flash Wear & Destructive Schema Migrations**: Settings persist to a single fixed 4 KB flash sector without wear leveling, ping-pong rotation, or power-loss journaling. Bumping `SETTINGS_VERSION` (currently `61u`) wipes all operator calibration back to factory defaults.
6. **LAN Command Bridge**: `scripts/flare_daemon.py` default bind address is hardened to `127.0.0.1` and CORS is restricted to loopback/same-host origins (`403 Forbidden` on untrusted origins). However, when an operator explicitly passes `--host 0.0.0.0` for LAN access, raw `POST /cmd` lacks token authentication.

---

## 2. Race Conditions & Hardware Watchdog Constraints

### 2.1 Watchdog Architecture & Loop Period Budget

Firmware execution is governed by an RP2040 hardware watchdog configured for 1000 ms in `firmware/src/main.c:575`:

```c
watchdog_enable(1000, true);
while (true) {
    g_now_ms = to_ms_since_boot(get_absolute_time());
    watchdog_update();
    ...
```

If the main loop stalls for >= 1000 ms, the hardware watchdog forces an immediate MCU reset. While `watchdog_caused_reboot()` (`firmware/src/main.c:519`) reports `EV:SYSTEM:WATCHDOG_RESET` after 2000 ms of recovery uptime, all active step trains, buffer positions, and toolchange phases are lost.

#### Timing Budget & Blocking Hazards

The superloop must execute at high frequency (typical loop period < 1 ms, capped by `MAIN_LOOP_SLEEP_US` in `firmware/src/main.c:617`). The primary hazards that can starve `watchdog_update()` include:

1. **Flash Sector Erase (`flash_range_erase`)**:
   - Located in `firmware/src/settings_store.c:384`.
   - Erasing a 4 KB NOR flash sector stalls execute-in-place (XIP) flash reads and disables interrupts for **30 to 100 ms**.
   - If an operator issues consecutive save commands or if combined with slow peripherals, loop jitter spikes dramatically.
2. **TMC2209 UART Reply Timeouts**:
   - Located in `firmware/src/tmc2209.c:257-265`.
   - Each register read (`tmc_read()`) performs up to `TMC_REPLY_ATTEMPTS = 2` attempts with a `TMC_REPLY_TIMEOUT_US = 5000` (5 ms) busy-wait loop using `tight_loop_contents()`.
   - If a motor driver is unpowered or disconnected, a single `tmc_read()` blocks the superloop for **10 to 12 ms**. Batch register dumps (e.g., `TMC:STATUS` or live tuner telemetry) can accumulate 100+ ms of pure busy-wait blocking.
3. **USB CDC Serial Polling**:
   - Located in `firmware/src/protocol.c:1890`.
   - Handled via `CMD_POLL_BYTE_BUDGET` and `CMD_POLL_COMMAND_BUDGET`. If these budgets are relaxed or if `printf`/serial output blocks due to an unread USB endpoint buffer, loop cadence degrades immediately.

### 2.2 Global State Coupling & Tick Lifecycle Dependencies

The firmware avoids dynamic memory allocation and RTOS queues, relying instead on **161 mutable global variables** declared in `firmware/include/controller_shared.h` and defined in `firmware/src/main.c`.

Because all modules read and mutate shared globals without concurrency locks, **the execution order of tick handlers in `firmware/src/main.c:590-615` is strictly load-bearing**:

```c
// 1. Sample inputs & debounce
debounced_input_update(&g_lane_l1.in_sw);
...
// 2. Poll serial commands
cmd_poll(g_now_ms);

// 3. Background stabilization
buffer_stabilize_tick(g_now_ms);

// 4. Subsystem state machines (CRITICAL ORDER)
cutter_tick(g_now_ms);
tc_tick(g_now_ms);          // Must run before lane_tick to dispatch tasks
autopreload_tick(g_now_ms);
lane_tick(&g_lane_l1, g_now_ms);
lane_tick(&g_lane_l2, g_now_ms);
buf_sensor_tick(g_now_ms);   // Must compute g_buf_pos before sync_tick
sync_tick(g_now_ms);         // Depends on fresh buf_sensor_tick calculations
```

#### Documented Failure Traps:
- **`sync_tick()` Silently Disabled Outside `TC_IDLE`**: In `firmware/src/sync.c`, `sync_tick()` early-exits if `tc_state() != TC_IDLE`. If a toolchange or reload phase hangs, sync velocity estimation is completely suppressed, preventing normal buffer tracking from recovering.
- **Buffer Position Latching**: `sync_tick()` assumes `g_buf_pos` has been recalculated by `buf_sensor_tick()` during the current millisecond pass. Reversing their invocation order causes the estimator to lag by one tick, inducing phase lag in the velocity control loop.

### 2.3 Runout Escalation vs. Rail Saturation Race (PSF Type-P)

A severe architectural race was uncovered in `.planning/backlog/psf-runout-escalation-race-fix/design.md`:

```
+-------------------------------------------------------------------------+
|                  THE TYPE-P RUNOUT STARVATION RACE LOOP                 |
+-------------------------------------------------------------------------+

  Complete Runout
         │
         ▼
  Tension Rail Hit (-1.0 norm)
         │
         ├──────────────────────────────────────────────┐
         ▼                                              ▼
  Fast Guard Path:                              Slow Dwell Path:
  sync_tick_type_p_rail_guard()                 sync_check_tension_dwell_and_ramp()
  Timeout: 1000 ms (CONF_PSF_WALL_SAT_MS)        Timeout: 6000 ms (FLARE_INT_SYNC_TENSION_DWELL)
         │                                              │
         ▼ (FIRES FIRST)                                │ (NEVER REACHED)
  sync_fault_hold() entered                             │
         │                                              │
         ▼                                              │
  sync_rearm_active() called ───────────────────────────┘ Resets dwell timer to 0!
         │
         ▼
  Loop restarts: FAULT_HOLD -> AUTO_START -> Rail Saturation -> FAULT_HOLD
  (System loops indefinitely, never escalating to RELOAD:SWITCHING)
```

- **Root Cause**: `sync_tick()` executed `sync_tick_gated_checks()` first, which evaluated the 1000 ms rail saturation guard (`CONF_PSF_WALL_SAT_MS`). When triggered, it entered `sync_fault_hold()`, which called `sync_rearm_active()`.
- This reset the 6000 ms slow-dwell timer (`FLARE_INT_SYNC_TENSION_DWELL_STOP_MS`) back to 0. Consequently, on physical Type-P rigs, a clean spool runout trapped the firmware in an infinite fault-hold retry loop rather than escalating to lane reload.
- **Prescription**: All runout escalation checks must bypass transient fault-recovery re-arming when filament sensors indicate both `IN` and `OUT` are clear.

---

## 3. Buffer Catch & Retract Synchronization Challenges (Phase 2)

During 3D printer operation, rapid print-end, pause, or toolchange retractions draw filament backward from the toolhead into the buffer at speeds between **30 and 60 mm/s**. Synchronizing the MMU motor to absorb this incoming slack without mechanical shock presents acute control challenges.

### 3.1 Velocity Mismatch and Mechanical Rail Slam

On Type-P proportional buffers (`BUF_SENSOR_TYPE=1`), the carriage travel is physically limited (e.g., `BUF_MAX_TRAVEL_MM = 16` mm on standard rigs):

| Subsystem Component | Speed (sps) | Speed (mm/s) | Source Reference |
|---|---|---|---|
| Type-P Buffer Prime Rate (`BUF_STAB_SPS`) | 4,092 | 10.0 | `tune.h:45`, `firmware/src/sync.c:786` |
| Sync PD Ceiling (`SYNC_MAX_SPS`) | 15,004 | 36.7 | `tune.h:46` |
| Observed Extruder Retract (`BL:T:30:3000`) | ~20,460 | 50.0 | Extruder physical feedrate |
| Absolute Catch Ceiling (`GLOBAL_MAX_SPS`) | 34,101 | 83.3 | `tune.h:76`, `firmware/src/motion.c:54` |

#### The Slam Sequence (`.planning/phases/02-buffer-retract-catch-hardening/02-SPEC.md`):
1. Prior to retraction, a buffer stabilize (`BS`) command drives the trolley toward `BUF_GOAL = 0.700` (which normalizes to **+0.40 norm on the compression side**).
2. The host executes `BL:T:30:3000` (Buffer Lock Tension) to prepare for the retract.
3. The board begins priming toward `-0.90 norm` at only `10.0 mm/s` (`BUF_STAB_SPS`).
4. Concurrently, the extruder fires its retract at `40–50 mm/s`. Net filament accumulation in the buffer is `+30 mm/s` toward compression.
5. In **< 500 ms**, the trolley slams hard into the physical compression endstop, causing stepper skipping, filament shaving, or trolley binding.

```
Buffer Position (norm)
  +1.0 COMPRESSION |           ,--------- SLAM INTO MECHANICAL STOP (< 500 ms)
                   |          /
  +0.4 ------------+---------X (BS parks here on compression side)
   0.0             |        / \  (Prime crawls toward tension at only 10 mm/s)
                   |       /   \
  -0.9 ------------+ - - -/ - - \ - - - - Target Rail (Never reached before retract hits)
  -1.0 TENSION     |     /
                   +----+-----+-----+-----+-----> Time (seconds)
                       0.0   0.5   1.0   1.5
```

### 3.2 The Host-Board Asynchronous Completion Race

A primary architectural contributor to buffer slams is the asynchronous nature of host command dispatch:

- In `scripts/flare_cmd.py`, issuing `BL:T:...` transmitted the command over serial and returned immediately as soon as the firmware acknowledged receipt with `OK:` (~10 ms).
- On the MCU, `BL_PRIME` was just beginning its multi-second motion (`~1.05 s` travel time).
- Slicer or macro scripts (e.g., `_FLARE_RETRACT_GUARD`) immediately executed the subsequent G-code retract line (`G1 E-30 F3000`), racing the still-moving MMU carriage.
- **Requirement**: `flare_cmd.py` and Klipper macros must block synchronously until the firmware emits `EV:BL:LOCKED` or `EV:BUF_STAB:DONE` before releasing G-code execution.

### 3.3 Pull-In Torque Limits vs. Instant Velocity Steps

The original `buffer-state-lock` specification called for an instant jump to `GLOBAL_MAX_SPS` (83.3 mm/s) upon detecting carriage break. 

On physical hardware, **the RP2040 stepper motor cannot instantaneously accelerate from 0 to 34,101 sps without stalling** due to rotor inertia and pull-in torque limits. The firmware was forced to implement acceleration ramps (`RAMP_STEP_SPS = 5115` per `RAMP_TICK_MS = 5`), introducing an unavoidable 20–30 ms latency before reaching maximum catch velocity.

### 3.4 Proportional Catch Limit Cycle (Hysteresis Oscillation)

Type-P buffer lock follow uses two position thresholds in `firmware/src/sync.c`:
- Gate down to `BL_LOCKED`: `PSF_FOLLOW_RAIL_NORM = 0.95` (`firmware/src/sync.c:948`)
- Break back to `BL_FOLLOW`: `PSF_HOME_THRESHOLD_NORM = 0.90` (`firmware/src/sync.c:898`)

Under sustained external retraction, the trolley continuously cycles across this 0.05 norm window (0.4 mm physical travel on a 16 mm rail). While bounded, velocity servo escalation steepens the ramp on each cycle, producing audible motor chatter and cyclical mechanical shock.

---

## 4. Hardware Dependency & Validation Gaps (Phase 1)

A significant operational risk is the gap between host software simulation and real physical test-bench validation.

### 4.1 Host Simulation vs. Physical Reality

The project maintains an automated host regression harness (`tests/host/`, `scripts/test_sync_sim.py`) that mocks hardware registers, GPIOs, and the Pico SDK. While this successfully validates logical state transitions, it **cannot simulate physical filament dynamics**:

1. **Non-Linear Spring Forces**: The QuattroSync spring buffer exhibits non-linear compression gradients; analog Hall sensor readings non-linearly saturate near mechanical stops.
2. **PTFE Tube Friction & Backlash**: In reverse-Bowden runs exceeding 1 meter, static and dynamic friction vary wildly with bend radius, filament material (PLA vs. TPU vs. abrasive CF filaments), and feed speed.
3. **Stepper Back-EMF & Slip**: TMC2209 drivers run without closed-loop encoder feedback. Real-world filament resistance causes microstep loss long before a software timeout registers a fault.

### 4.2 Phase 1 Pending Physical Rig Verifications

As detailed in `.planning/phases/01-hardware-validation-and-audit-closeout/01-01-PLAN.md`, multiple audit hardening and reliability fixes are code-complete in Git, but **remain strictly unvalidated on physical hardware**:

| Validation Target | Proposed Verification Scenario | Failure Mode if Defective |
|---|---|---|
| Activity-Gated Persistence | Send `CAL` or `SV:` while lane stepper is driving active feed | Flash erase disables XIP; interrupts disabled; motor drops steps or MCU crashes |
| Cutter Command Re-entry | Dispatch `CP` (Cutter Position) while blade servo is mid-stroke | Servo PWM signal interrupted; blade wedges partially through filament strand |
| Toolchange Command Guard | Dispatch `T:` or `TC:` during active toolchange sequence | Multi-phase state machine variables overwritten; simultaneous lane feeds jam Y-splitter |
| Buffer Lock Timeout Telemetry | Allow buffer lock to expire without retract | Event emission dropped; daemon state remains locked; print hangs indefinitely |
| Type-P Runout Reload Grab | Genuine runout with active extruder consumer on Type-P rig | Standby lane fails to contact compression stop; reload aborts with false `FOLLOW_JAM` |
| Reload Staging Without Motion | Issue `RL:` when standby lane is already staged at toolhead | Firmware spuriously reports `RELOAD:LOADED` without moving filament, starving hotend |

---

## 5. TMC2209 UART Communication Timing & Electrical Safety

Stepper motor control relies on Trinamic TMC2209 drivers communicating with the RP2040 via dedicated UART interfaces in `firmware/src/tmc2209.c`.

```
RP2040 Microcontroller                              TMC2209 Stepper Driver
+--------------------+                              +--------------------+
|                    |     Single-Wire Direct Trace  |                    |
|  GPIO 11 (X-UART)  |<────────────────────────────>| Pin 14 (PDN_UART)  |
|  GPIO 17 (Y-UART)  |     (No isolation resistors) |                    |
|                    |                              | Internal pull-down |
|  PIO0 State Mach.  |                              | (~few kΩ)          |
+--------------------+                              +--------------------+
```

### 5.1 Bus Topology & Electrical Drive Hazards

As documented in `HARDWARE.md:139-173`, the FYSETC ERB V2.0 board wires RP2040 GPIOs directly to TMC2209 `PDN_UART` pins **without series isolation resistors, bidirectional level shifters, or external pull-ups**:

1. **Pull-Down Conflict**: By default (`pdn_disable = 0`), the TMC2209 activates an internal pull-down resistor (~few kΩ). The RP2040 internal pull-up resistor (~50 kΩ) cannot overcome this load. If the firmware attempts to send '1' bits by releasing the pin to input-pullup, the line is pulled LOW by the TMC, corrupting the UART datagram.
   - *Fix Implemented*: The PIO transmitter must actively drive `OUTPUT HIGH` during idle and stop bits (`tmc2209.c:180`).
2. **Accidental Standby Trap**: Holding `PDN_UART` HIGH for **>= 1.0 ms** while `pdn_disable = 0` causes the TMC2209 to enter **low-power standby sleep**.
   - In standby, the driver shuts down its internal clock and disables UART communication.
   - The line reads HIGH, creating the illusion of active communication, but all read attempts timeout.
   - Preconditioning sequences must never leave the pin statically HIGH for >= 1 ms during boot bring-up.

### 5.2 Timing Hazards in PIO Half-Duplex Turnaround

The UART operates at 40,000 baud (`TMC_BAUD 40000u` in `firmware/src/tmc2209.c:16`). At 40 kbaud:
- 1 bit = 25 µs
- 1 byte (1 start, 8 data, 1 stop) = 250 µs
- An 8-byte write takes **2.0 ms**
- A read transaction (4 bytes TX + bus turnaround + 8 bytes RX) takes **> 3.1 ms**

#### Line Turnaround Sequence (`firmware/src/tmc2209.c:225-271`):
1. The RP2040 transmits the 4-byte read request datagram via the TX PIO state machine.
2. The state machine stalls at the stop bit; firmware enforces a mandatory `TMC_INTERFRAME_GAP_US = 30` µs gap.
3. Firmware adds `TMC_ECHO_SETTLE_US = 10` µs to ensure echo bytes clear the line.
4. Firmware flushes the RX FIFO of echo bytes.
5. Firmware switches pin direction to `INPUT` (`pio_sm_set_consecutive_pindirs(..., false)`).
6. Firmware enters a busy-wait loop awaiting 8 reply bytes with a 5000 µs timeout.
7. Firmware immediately restores pin direction to `OUTPUT HIGH`.

**Vulnerability**: If line noise, electrical crosstalk, or motor inductive spikes trigger the RX state machine while the line is idling, the RX state machine program counter (`PC`) becomes misaligned. A simple FIFO flush does not realign the state machine; `tmc_read_bytes()` is forced to manually stop, re-encode, and restart the RX state machine before every read (`tmc2209.c:236-240`).

### 5.3 Silent Driver Resets and Register Loss

The TMC2209 registers (`IHOLD_IRUN`, `CHOPCONF`, `PWMCONF`, `TPWMTHRS`) are **entirely volatile**. 

If the 24V motor power rail experiences a momentary brownout, thermal trip, or supply glitch:
- The TMC2209 resets to factory silicon defaults (analog VREF scaling, default microstepping, stealthChop disabled).
- **The firmware has no periodic heartbeat or register verification task**.
- The MCU continues issuing step pulses to a driver whose current limits and microstepping settings are corrupted, leading to rapid motor stalling, excessive heat, or layer shifts.

### 5.4 Unmonitored DIAG & StallGuard Pins

As documented in `HARDWARE.md:173` and `CONTEXT.md:150`, GPIO 13 (`X-DIAG`) and GPIO 19 (`Y-DIAG`) are physically wired to the TMC DIAG outputs, but **the firmware does not attach interrupt service routines to these pins**. 

StallGuard load measurements (`SG_RESULT`) and driver over-temperature pre-warning flags cannot trigger emergency motor stops; the firmware relies exclusively on optical switch debounce and buffer displacement thresholds.

---

## 6. Non-Volatile Flash Wear & Migration Deficits

Configuration persistence is managed by `firmware/src/settings_store.c`, writing directly to the RP2040's external SPI NOR flash.

```
RP2040 2MB NOR Flash Layout
0x000000 +-------------------------------------------------------+
         | Firmware Binary Image (UF2 Flash Target)              |
         | (Code, vector tables, rodata, PIO programs)           |
         |                                                       |
         ~                                                       ~
         |                                                       |
0x1FF000 +-------------------------------------------------------+ <- SETTINGS_FLASH_OFFSET
         | Single 4 KB Flash Sector (Wiped on every save)        |
         | [ settings_t: 512-byte buffer | Unused: 3584 bytes ]  |
0x200000 +-------------------------------------------------------+
```

### 6.1 Endurance & Lack of Wear Leveling

- **Single Sector Target**: Persisted settings are anchored to a single fixed offset:
  ```c
  #define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
  ```
  On a standard 2 MB flash chip, this targets offset `0x1FF000` exclusively (`settings_store.c:23`).
- **Erase Endurance**: RP2040 NOR flash sectors are typically rated for **~100,000 erase/program cycles**.
- **Destructive Write Path**: Every invocation of `settings_save()` performs:
  ```c
  flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(SETTINGS_FLASH_OFFSET, buffer, SETTINGS_FLASH_BUFFER_BYTES);
  ```
  There is no wear leveling, no circular log, and no A/B ping-pong sector swapping.
- **Wear Visibility Counter**: While `flash-wear-visibility` added `g_flash_erase_count` and emits `EV:FLASH:WEAR_WARNING` at 80,000 cycles (`settings_store.c:351`), this counter merely reports impending hardware death. Crucially, issuing a factory reset (`RS:`) resets the wear counter back to zero, erasing wear history.

### 6.2 Power-Loss Vulnerability (No Journaling)

The flash update sequence is non-atomic:
1. `stop_all()` shuts down active stepper timers.
2. Interrupts are disabled (`save_and_disable_interrupts()`).
3. `flash_range_erase()` erases the entire 4096-byte sector, setting all bits to `0xFF`.
4. `flash_range_program()` writes the new 512-byte payload.
5. Interrupts are restored.

**Failure Mode**: If power drops during the erase window (or before programming completes):
- The sector is left blank or corrupted.
- On reboot, `settings_load()` detects a magic/CRC32 mismatch and falls back to compiled hardcoded defaults.
- All calibrated motor currents, travel limits, buffer geometry limits, and PID tunings are instantly lost.

### 6.3 Destructive Schema Versioning

Schema changes are governed by `SETTINGS_VERSION` in `firmware/src/settings_store.c:25` (currently `61u`):

```c
if (s->magic != SETTINGS_MAGIC || s->version != SETTINGS_VERSION) {
    settings_reset_to_defaults();
    return false;
}
```

- **Zero Backward Compatibility**: Adding, removing, or reordering a single field in `settings_t` requires bumping `SETTINGS_VERSION`.
- A version mismatch causes the loader to **discard the stored sector entirely and reset all settings to compiled defaults**.
- Sixty-one firmware iterations have each wiped user calibration upon upgrade.
- The project lacks a Type-Length-Value (TLV) encoding, schema migration logic, or delta persistence.

### 6.4 Friction in the 10-Step Tunable Lifecycle

Adding or altering a persistent parameter requires updating ten separate locations (`CONTEXT.md:103-118`):
1. `config.ini` / `config.ini.example`
2. `scripts/gen_config.py` default table
3. Generated `firmware/include/tune.h` via script execution
4. Runtime variable in owning C module (`main.c`, `sync.c`, etc.)
5. `controller_shared.h` extern declaration
6. `settings_t` struct in `firmware/src/settings_store.c`
7. `settings_save()`, `settings_load()`, and `settings_reset_to_defaults()`
8. TMC register re-apply path (if a motor tunable)
9. Protocol parsers (`SET:`, `GET:`) in `firmware/src/protocol.c`
10. Version bump of `SETTINGS_VERSION`

Failure at any point breaks `scripts/test_settings_parity.py`, representing the highest friction development bottleneck in the repository.

---

## 7. Host Integration & Daemon Network Surface

The host tier consists of `scripts/flare_daemon.py`, a multi-threaded Python daemon bridging Klipper/Moonraker to the RP2040 USB serial interface.

### 7.1 Unauthenticated Remote Execution Bridge

By default, the daemon binds to all network interfaces (`scripts/flare_daemon.py:1436`):

```python
parser.add_argument("--host", default="0.0.0.0", help="HTTP server bind host (default: 0.0.0.0)")
```

- **Missing Authentication**: The HTTP server implements zero authentication, API tokens, or session validation.
- **Permissive CORS**: In `flare_daemon.py:836`, HTTP responses include:
  ```python
  self.send_header("Access-Control-Allow-Origin", "*")
  ```
- **Raw Command Passthrough**: The `POST /cmd` endpoint accepts raw JSON:
  ```json
  {"cmd": "UL:1"}
  ```
  The daemon forwards this string directly to the microcontroller via `execute_serial_command()`.
- **Security Impact**: Any device on the local network—or any malicious JavaScript payload executed in a browser on the same network—can issue arbitrary G-code and serial commands to FLARE. Attackers can actuate the blade cutter (`CUT`), drive steppers continuously to induce mechanical stalls, or trigger flash erase routines.

### 7.2 Concurrency Bottlenecks & Thread Starvation

In `scripts/flare_daemon.py`:
1. **Single Serial Lock**: All serial commands dispatch through a single `threading.Lock()` (`serial_lock`).
2. **HTTP Worker Model**: The daemon uses Python's `ThreadingMixIn`, spawning a new thread for every incoming HTTP request.
3. **Cascading 504 Timeouts**: If a serial command blocks awaiting completion (up to `command_event.wait(timeout=10.0)`), concurrent requests from Mainsail, Fluidd, and Klipper pile up on `serial_lock`. Once the 10-second timer expires, the daemon returns HTTP 504 Gateway Timeout, causing host UI components to report hardware disconnects.
4. **Unbounded SSE Queues**: Every Server-Sent Events client connected to `/telemetry` is allocated an unbounded `queue.Queue`. If a client ceases reading without closing the socket, telemetry events accumulate in memory indefinitely.

### 7.3 Klipper Happy-Hare Mock & Stubbed Actuators

The Klipper integration in `klipper/mmu.py` is implemented as an API facade mimicking Happy-Hare (`MMUMachineMock`):
- Many registered G-code handlers are stubs or no-ops (`cmd_MMU_NOOP`).
- Crucially, **`cmd_MMU_PAUSE` is an empty stub**. Clicking "Pause" in standard MMU UI interfaces does not halt the printer or stop filament motion in firmware. Operator safety depends entirely on local RP2040 firmware interlocks.

---

## 8. Prioritized Risk & Technical Debt Matrix

| Category | Specific Risk / Debt | Severity | Failure Symptoms | Prescriptive Mitigation |
| **Security** | Unauthenticated `POST /cmd` when running with `--host 0.0.0.0` | **MEDIUM** (mitigated from CRITICAL by `127.0.0.1` default & CORS restrictions) | Remote unauthorized motion, blade actuation, flash corruption via LAN | Change default bind to `127.0.0.1` (Done); restrict CORS (Done); implement strict command allowlist and token auth |
| **Control** | Fast retract buffer rail slam on Type-P proportional buffer | **HIGH** | Stepper skipping, filament grinding, mechanical carriage impact (<0.5 s) | Implement Phase 2 fast prime, synchronous host completion waits, and retract guard macros |
| **Firmware** | Single flash sector erase-then-program with no backup or wear leveling | **HIGH** | Complete loss of calibration on power glitch; sector death after ~100k writes | Implement A/B ping-pong sector swapping and atomic commit flags |
| **Architecture** | Destructive `SETTINGS_VERSION` bumps wiping all operator tunings | **MEDIUM** | User frustration; loss of motor and buffer tuning on firmware upgrade | Migrate flat struct to a tagged TLV (Type-Length-Value) storage format |
| **Reliability** | Type-P runout escalation race in `sync_tick` | **HIGH** | Spool runout causes infinite fault-hold looping; never switches to standby spool | Decouple runout escalation checks from transient rail-saturation timers |
| **Hardware** | Code-complete firmware fixes unverified on physical test rigs | **HIGH** | Unforeseen physical dynamics break bench prints despite passing unit tests | Execute Phase 1 hardware validation checklist on physical bench rigs |
| **Electrical** | TMC2209 silent standby mode on bootup timing glitch | **MEDIUM** | Driver unresponsive to UART; communication dead until power cycle | Ensure PDN_UART is never held HIGH >= 1 ms during boot bring-up |
| **Electrical** | Lack of TMC2209 runtime register verification | **MEDIUM** | Motor power drop causes silent reset to defaults; weak torque or overheating | Add low-frequency background tick to verify and restore driver registers |
| **Host** | Single `serial_lock` in daemon with 10 s timeout | **MEDIUM** | Multi-client HTTP 504 timeouts; Klipper UI disconnects during long operations | Split serial interface into high-priority control and asynchronous event streaming |
| **Maintainability** | 10-step manual ritual for adding persistent tunables | **LOW** | Development friction; merge conflicts; parity test breakages | Automate code generation across `settings_t`, `protocol.c`, and `config.ini` |

---

*Concerns and technical debt analysis: 2026-09-11*
<!-- refreshed: 2026-09-11 -->
