# Phase 11: Firmware Forensics & Main Loop Jitter Instrumentation — Specification

**Goal**: Implement uninitialized RAM blackbox crash logging surviving watchdog resets, line-by-line post-mortem retrieval, and high-resolution main-loop execution jitter instrumentation.
**Scope**: Firmware (`forensics.c`, `forensics.h`, `main.c`, `protocol.c`), host simulation (`tests/host/sim_fakes.c`, `tests/host/test_forensics.c`), and host tooling (`scripts/flare_cmd.py`, `scripts/flare_daemon.py`).

---

## 1. Crash Storage & Memory Architecture

### Requirements
1. **Memory Location**:
   - Store blackbox buffer in RP2040 uninitialized SRAM (`__uninitialized_ram(s_crash_data)`).
   - Zero flash wear; survives hardware watchdog resets and software reboots without erase or program operations.
2. **Integrity & Validation**:
   - Fixed magic header: `0x43525348` (`"CRSH"`).
   - Version: `1`.
   - Trailer/Record CRC32 checksum verifying data validity before accepting the post-mortem snapshot.
   - If magic or CRC32 fails on boot, clear/re-initialize the retention buffer as clean.

---

## 2. Telemetry Payload & Ringbuffer Structure

### Requirements
1. **Capacity**: Fixed circular ringbuffer of 32 entries ($\approx 512$ bytes).
2. **Recorded Fields per Entry**:
   - `timestamp_ms`: 32-bit millisecond timestamp.
   - `type`: Entry type (`BREADCRUMB`, `TASK_CHANGE`, `TC_CHANGE`, `SYNC_CHANGE`, `SENSOR_EDGE`, `CMD_RECV`, `EVENT_EMIT`).
   - `lane`: Active lane number (0, 1, 2).
   - `tc_state`: Current toolchange state enum.
   - `sync_state`: Current sync state enum.
   - `sensors`: 8-bit sensor bitmask (`IN1`, `OUT1`, `IN2`, `OUT2`, `Y`, `TS`, `TENS`, `COMP`).
   - `buf_pos_raw`: int16_t scaled buffer position ($g\_buf\_pos \times 100$).
   - `step_rate`: int16_t active motor step rate (SPS).
   - `payload`: 32-bit auxiliary tag/data (command hash, task enum, event enum).
3. **Sampling Policy**:
   - Event-driven logging: Record immediately on lane task transitions, toolchange state transitions, sync state transitions, debounced sensor level changes, and command executions.
   - Periodic breadcrumbs: If no transitions occur within 100ms, record a breadcrumb entry with current telemetry snapshot.

---

## 3. Crash Signaling & Host Retrieval Interface

### Requirements
1. **Boot Signaling**:
   - On boot, if `watchdog_caused_reboot()` is true and valid crash signature is verified:
     - Set crash reason: `CRASH_WATCHDOG`.
     - Emit event: `EV:CRASH:DETECTED:WATCHDOG`.
2. **Retrieval Command (`GET:CRASHLOG`)**:
   - Host queries via `GET:CRASHLOG`.
   - Firmware streams sequential indexed lines from oldest to newest:
     - `OK:CRASH:HDR:reason=<R>,time=<T>,entries=<N>`
     - `OK:CRASH:00:t=<ms>,type=<type>,ln=<ln>,tc=<tc>,st=<st>,sw=<mask_hex>,bp=<bp>,sps=<sps>`
     - ...
     - `OK:CRASH:END`
3. **Reset Command (`CAL:CRASHLOG_CLEAR`)**:
   - Clears magic cookie and resets retention buffer. Returns `OK:`.

---

## 4. Main Loop Jitter & Headroom Benchmarking

### Requirements
1. **High-Resolution Timing**:
   - Measure each main loop pass using `time_us_32()`.
   - Measure execution duration of individual module slices (`debounced_inputs`, `cmd_poll`, `buffer_stabilize`, `cutter`, `tc`, `autopreload`, `lane_ticks`, `buf_sensor`, `sync`, `tmc_heartbeat`, `neopixel`).
2. **Tracked Metrics**:
   - `cur_us`: Duration of most recent loop iteration ($\mu\text{s}$).
   - `max_us`: Maximum loop duration observed since boot ($\mu\text{s}$).
   - `avg_us`: Exponential moving average of loop duration ($\mu\text{s}$).
   - `overrun_count`: Number of loop passes exceeding 10000 $\mu\text{s}$ (10ms).
   - `max_module`: String name of module that consumed the most time during the recorded `max_us` spike.
3. **Query Command (`GET:LOOP_STATS`)**:
   - Returns `OK:CUR:<cur>,MAX:<max>,AVG:<avg>,OVERRUNS:<n>,TOP:<module>`.
4. **Warning Event**:
   - If any loop iteration exceeds 15000 $\mu\text{s}$ (15ms), emit `EV:WARN:LOOP_LAG:<us>:<module>`.

---

## 5. Host Simulation & Test Verification

### Requirements
1. **Stateful Retention Mock**:
   - `tests/host/sim_fakes.c` provides uninitialized RAM simulation buffer and delay injection hook.
2. **Host Unit Tests (`tests/host/test_forensics.c`)**:
   - Verify ringbuffer wrapping at 32 entries.
   - Verify crash signature persistence and CRC validation across simulated watchdog reboot.
   - Verify `GET:CRASHLOG` line-by-line output order and fields.
   - Verify `CAL:CRASHLOG_CLEAR` wiping.
   - Verify loop timing calculation, culprit module identification, and `EV:WARN:LOOP_LAG` emission.
