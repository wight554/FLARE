# Testing Patterns and Verification Protocols

**Analysis Date:** 2026-09-11

This document outlines the multi-tiered testing architecture of FLARE, covering host-compiled C simulation (`flare_sim`), Python automated regression validation (`scripts/validate_regression.py`), static schema/protocol unit tests, and the physical hardware testing protocol (`TEST_CASES.md`).

---

## 1. Host Simulation Test Harness (`tests/host/` & `flare_sim`)

The host simulation framework executes the real, unmodified C firmware control logic on a development machine (macOS/Linux) without embedded hardware. It validates state machines, motion planning, buffer dynamics, fault recovery, and deadlock avoidance under simulated physical conditions.

### 1.1 Architectural Philosophy & Simulation Authority Boundary

- **Verbatim Source Execution**: `flare_sim` compiles and links production C sources directly:
  - `firmware/src/sync.c`
  - `firmware/src/sync_buf.c`
  - `firmware/src/sync_relay.c`
  - `firmware/src/sync_analog.c`
  - `firmware/src/motion.c`
  - `firmware/src/toolchange.c`
  - `firmware/src/cutter.c`
  - `firmware/src/settings_store.c`
- **Simulation Authority Boundary**: The simulation proves algorithmic correctness, state machine completeness, numerical stability, timer transitions, and invariant preservation. **The simulation does NOT qualify real-world control tuning, physical spring rates, motor torque limits, or electrical noise.** A passing simulation run screens logic, but never satisfies an `HW:` hardware validation gate (Rule 12).

---

### 1.2 Build Architecture & Header Shimming

The simulation binary is configured via `tests/host/CMakeLists.txt` and built with CMake and Ninja:

```bash
cmake -S tests/host -B build_sim -G Ninja
ninja -C build_sim
```

#### Key Build Mechanisms:
1. **Dynamic Config Generation**: Automatically runs `scripts/gen_config.py` on `config.ini` to create `firmware/include/tune.h`.
2. **Global Variable Synthesis**: Uses `scripts/gen_sim_globals.py` to parse `firmware/src/main.c` and generate `build_sim/generated/sim_globals.c`. This ensures simulation globals stay in sync with firmware definitions without manual duplication.
3. **Shim Interception**: The include path `tests/host/shims` is passed before `firmware/include`. This redirects `#include "pico/stdlib.h"`, `#include "hardware/gpio.h"`, etc., to lightweight host fakes instead of the ARM Pico SDK.

---

### 1.3 Hardware Register Fakes & Callback Shims (`sim_fakes.c`)

The simulation fakes hardware peripherals and external module dependencies:

1. **Hardware Register Fakes (Record-Only)**:
   - GPIO (`gpio_put`, `gpio_get`, `gpio_set_dir`): Maintains static state array `s_gpio_out[30]`.
   - PWM (`pwm_set_chan_level`, `pwm_set_wrap`, `pwm_set_clkdiv`): Tracks commanded motor slices without simulating counter circuits.
   - ADC: Emulates RP2040 12-bit ADC via `g_sim_adc_counts` updated by the plant model.
   - Flash: Simulates flash memory via in-memory buffer `uint8_t g_sim_flash[256 * 1024]`.
2. **Virtual Deterministic Clock**:
   - `get_absolute_time()` returns `(absolute_time_t)g_now_ms * 1000u`.
   - `to_ms_since_boot()` returns `g_now_ms`.
   - Simulated time advances in discrete steps (default 20 ms) governed strictly by the main loop.
3. **Callback Implementations**:
   - `cmd_event()` & `cmd_event_critical()`: Append event text strings into the ring buffer `g_sim_events[SIM_EVENT_MAX]`.
   - `set_toolhead_filament()`: Mirrors production sync-enable/disable logic from `firmware/src/main.c` verbatim.

---

### 1.4 Kinematic Plant Model (`sim_plant.c`)

The plant models the physical buffer mechanism and filament dynamics:
- **Core State Variable**: `slack_mm` representing filament slack.
  - Sign convention: `+slack_mm` = Compression (excess filament pushing into buffer), `-slack_mm` = Tension (extruder pulling filament taut).
  - Clamping: Hard travel limits clamp slack to `[-g_buf_max_travel_mm, +g_buf_max_travel_mm]`.
- **Integration Tick**: Every tick:
  $$\Delta \text{slack} = (\text{achieved\_feed} - \text{demand}) \times \Delta t$$
- **Sensor Publishing**:
  - **Type-D Dual Switch**: Translates `slack_mm` against physical threshold coordinates into discrete pin assertions for `tension_din` and `compression_din`.
  - **Type-P Analog / Hall**: Converts normalized buffer displacement to simulated 12-bit ADC counts (`g_sim_adc_counts`).
- **Stress Mode Injection**:
  - Motor slew rate limiting: Simulates acceleration curves via `achieved_feed_sps`.
  - Transport delay: First-order low-pass filter modeling bowden compliance via `stress_lag_ms` and `lagged_feed_mm_s`.

---

### 1.5 Scenario Engine (`sim_scenario.c`)

Simulations run predefined scenarios declaring demand waveforms, fault injections, and switch actions:

```c
typedef struct {
    const char *name;
    demand_profile_t demand;          // STEADY, STEP_UP, BURST, IDLE_ZERO, RETRACT, LONG_RETRACT
    gain_schedule_t feed_gain;        // Time-scheduled feeder slip (e.g. 0.0 for upstream jam)
    gain_schedule_t demand_gain;      // Time-scheduled extruder slip (e.g. 0.0 for grind)
    gain_schedule_t retract_gain;     // Time-scheduled retract jam
    sensor_force_schedule_t sensor_force; // STUCK, CHATTER, BOTH switch faults
    switch_script_t switch_script;    // Timed IN/OUT/YS switch transitions
    uint32_t tick_ceiling;            // Max ticks before simulation concludes
    bool auto_mode;
    bool start_sync_active;
    bool type_specific;
} sim_scenario_t;
```

#### Catalogued Scenario Highlights
- `steady`: Constant extruder demand to verify PD convergence.
- `step_up`: Abrupt demand doubling to verify acceleration tracking.
- `burst`: Intermittent pulse demand simulating tip shaping.
- `idle_zero`: Sudden stop of extruder demand to check against distance-clock overfeed.
- `retract` / `long_retract`: Negative demand to verify compression saturation handling.
- `jam_upstream`: `feed_gain` drops to 0 to simulate tangled spool.
- `sensor_chatter` / `both_switches_fault`: Type-D switch chatter and fault latching.
- `reload_genuine_runout_escalation`: Verifies Type-P runout escalation into RELOAD without looping `FAULT_HOLD`.
- `sem_cutter_feed_timeout_jam`: Simulates cutter feed timeout error reporting.
- `sem_persistence_fresh_board`: Verifies `settings_load()` fallback on unformatted flash.

---

### 1.6 Trace Emission & The 7 Global Invariants (`sim_trace.c`)

Every simulation tick outputs a standardized CSV trace row:
```csv
ts_ms,bp_mm,zone,feed_sps,demand_mm_s,sync_state,sat,events
```

On every tick, `sim_trace_tick()` verifies the **Seven Universal Invariants**. Any failure triggers an immediate abort with a diagnostic message:

1. **Finiteness**: `slack_mm`, `g_sync_current_sps`, `g_extruder_est_sps`, and `g_buf_pos` must never be `NaN` or `Inf`.
2. **Bounds**: Commanded feed rate must be strictly non-negative and never exceed `g_sync_max_sps`:
   $$0 \le g\_sync\_current\_sps \le g\_sync\_max\_sps$$
3. **Liveness**: Transient states (`SYNC_RETRACT_ASSIST`, `SYNC_RELIEF_PAUSE`) must exit within `SIM_LIVENESS_BACKSTOP_MS` (45 seconds simulated time).
4. **Fault Quiescence**: While in `SYNC_FAULT_HOLD`, commanded feed must remain 0 and event generation must cease (except for the initial entry announcement).
5. **Non-Oscillation**: No single sync state may be entered more than 50 times in a single scenario run.
6. **Saturation Bound**: Buffer rail saturation (`sat_compression` or `sat_tension`) must not persist continuously for more than 20 seconds.
7. **Event Rate Ceiling**: No single tick may emit more than 16 event notifications (`cmd_event`).

---

## 2. Python Automated Regression Validation (`scripts/validate_regression.py`)

The primary pre-commit regression barrier is executed with a single command:

```bash
python3 scripts/validate_regression.py
```

### 2.1 The 9-Stage Validation Pipeline

`scripts/validate_regression.py` executes nine synchronous stages, failing immediately if any step returns non-zero:

```
┌──────────────────────────────────────────────────────────┐
│ 1. gen_config.py           Generate tune.h               │
├──────────────────────────────────────────────────────────┤
│ 2. CMake configure         FLARE_DEV_TUNING=ON superset  │
├──────────────────────────────────────────────────────────┤
│ 3. Ninja build             Compile real firmware         │
├──────────────────────────────────────────────────────────┤
│ 4. Host sync simulation    Build flare_sim & run tests   │
├──────────────────────────────────────────────────────────┤
│ 5. Python syntax           py_compile scripts/*.py       │
├──────────────────────────────────────────────────────────┤
│ 6. Python lint             ruff check scripts/           │
├──────────────────────────────────────────────────────────┤
│ 7. Python unit test suite  unittest discover             │
├──────────────────────────────────────────────────────────┤
│ 8. Mock MMU status test    test_flare_mmu_status.py      │
├──────────────────────────────────────────────────────────┤
│ 9. Diff hygiene            git diff --check              │
└──────────────────────────────────────────────────────────┘
```

---

### 2.2 Specialized Static Unit Guards

In addition to simulation, Python unit tests guard specific interface and persistence contracts:

- `scripts/test_settings_parity.py`: Parses `firmware/src/settings_store.c` AST to enforce round-trip persistence. Asserts that every field saved in `settings_save()` is read in `settings_load()`, and every global loaded is seeded in `settings_defaults()`.
- `scripts/test_protocol_param_width.py`: Verifies that all parameter names fit within `CMD_PARAM_MAX` (32) and match the `CMD_PARAM_SCAN_WIDTH` (31) regex in `firmware/src/protocol.c`.
- `scripts/test_wire_format.py`: Validates status strings, command formatting, and response token parsing.
- `scripts/test_flare_mmu_status.py`: Tests host-side status line regex extraction against known good snapshots.
- `scripts/test_sync_sim.py`: Subprocess test runner that executes `flare_sim` across the complete scenario matrix and evaluates CSV outputs.

---

### 2.3 Recommended Minimum Checks by Change Type

| Change Type | Minimum Required Checks |
|-------------|-------------------------|
| Firmware logic only | Build (`ninja`), Diff Hygiene (`git diff --check`), Host Sim (`test_sync_sim.py`) |
| Settings / Tunables | Config Gen, Build, Diff Hygiene, Settings Parity (`test_settings_parity.py`), Docs |
| Host Python scripts | Python Syntax (`py_compile`), Ruff Lint (`ruff check`), Unit Tests |
| Config generation | Config Gen (`gen_config.py`), Firmware Build, Settings Parity |
| Protocol / Serial API | Protocol Width (`test_protocol_param_width.py`), Wire Format, Docs |

---

## 3. Physical Hardware Testing Protocol (`TEST_CASES.md`)

Hardware testing validates physical electrical characteristics, sensor alignment, motor current tuning, and toolhead interactions on the physical FYSETC ERB V2.0 board.

### 3.1 Operational Safety Rules

1. **Initial Low-Speed Bringup**: Always throttle rates when commissioning new firmware or wiring:
   ```bash
   python3 scripts/flare_cmd.py \
     "SET:FEED_RATE:600" \
     "SET:REV_RATE:600" \
     "SET:AUTO_RATE:400" \
     "SET:JOIN_RATE:400" \
     "SET:PRESS_RATE:300"
   ```
2. **Filament Clearance**: Keep filament retracted from the printer extruder path unless explicitly testing full loading.
3. **Emergency Stop Preparedness**: Keep terminal ready to issue `ST:` (Stop) immediately upon unexpected motion or sensor misreading.
4. **Supervised RELOAD**: Never run autonomous runout/reload tests unattended.

---

### 3.2 Acceptance Checklist by Lane

Before running toolchange or sync tests, both lanes must pass individual motion validation:

| Test Item | Verification Command | Expected Output | Lane 1 | Lane 2 |
|-----------|----------------------|-----------------|:------:|:------:|
| Lane Selection | `T:n` then `?:` | `LN:1` or `LN:2` | [ ] | [ ] |
| IN Sensor | Trigger switch physically | `I1:1` or `I2:1` (no crosstalk) | [ ] | [ ] |
| OUT Sensor | Trigger switch physically | `O1:1` or `O2:1` (no crosstalk) | [ ] | [ ] |
| Preload | `LO:` | Drives to OUT sensor and parks | [ ] | [ ] |
| Unload (bowden) | `UL:` | Retracts past OUT sensor | [ ] | [ ] |
| Unload (spool) | `UM:` | Retracts fully past IN sensor | [ ] | [ ] |
| Full Load | `FL:` | Stops on toolhead sensor (`TS:1`) | [ ] | [ ] |
| Dry Spin Guard | Run uncoupled | Emits `FAULT:DRY_SPIN` within 8s | [ ] | [ ] |

---

### 3.3 Core Hardware Test Cases (Summary)

Detailed in `TEST_CASES.md`:
1. **Case 1: Build and Serial Smoke Test**: Validates USB CDC communications via `VR:` (firmware version) and `?:` (status dump).
2. **Case 2: Sensor Polarity & Idle State**: Verifies discrete GPIO state updates (`I1`, `O1`, `I2`, `O2`, `YS`, `TS`) without uncommanded motor motion.
3. **Case 3: Active Lane Selection**: Verifies switching between lanes (`T:1`, `T:2`).
4. **Case 4: Preload and Unload**: Tests basic loading primitives (`LO:`, `UL:`, `UM:`).
5. **Case 5: Full Load to Toolhead**: Validates the toolhead sensor handoff sequence (`FL:` -> `TS:1` -> `EV:LOADED:<lane>`).
6. **Case 6: Toolchange Sequencing**: Verifies MMU lane change flow (`TC:2`) including cutter actuation and park-and-swap logic.
7. **Case 7: Sync Auto-Start and Auto-Stop**: Validates buffer-displacement-triggered motion engagement (`EV:SYNC:AUTO_START` on `TENSION`, `EV:SYNC:AUTO_STOP` on sustained `COMPRESSION`).
8. **Case 8: RELOAD Runout Recovery**:
   - **Type-D**: Validates switch-based runout transfer from depleted lane to standby lane.
   - **Type-P (8.2)**: Confirms that analog position `BP` must exceed `+0.5` (`PSF_LOAD_CONTACT_THRESHOLD_NORM`) during approach, ignores tension crossings during settle/boost window, and declares success on extruder pickup.
9. **Case 9: Persistence Guarding**: Confirms that flash commands (`SV:`, `LD:`, `RS:`) are blocked with `ER:PERSIST_BUSY` while motors are moving.
10. **Case 10: Bootloader & Flash Smoke**: Validates software reboot into bootloader via `BOOT:` and successful programming via `scripts/flash_flare.py`.

---

### 3.4 Hardware Gate Taxonomy (`HW:` Gates)

Validation items requiring a physical rig are tracked explicitly and must never be closed by automated tools or simulation passes:

- `pending-manual-hardware`: Requires a physical printer, dual-lane MMU, and live motion.
- `pending-type-d-rig`: Requires a dual-microswitch buffer assembly.
- `pending-analog-rig`: Requires an analog Hall-effect / magnet buffer setup.

#### Incident Triage & Record Keeping
When logging hardware failures or regressions, record:
- Firmware Git commit SHA.
- Active configuration parameters (`flare_cmd.py --dump --raw`).
- Exact command sequence dispatched.
- Raw status dump (`?:`) before and after the failure.
- Chronological `EV:` event stream around the incident.
- Lane reproduction details (Lane 1 only, Lane 2 only, or both).

---

*Testing patterns and verification analysis: 2026-09-11*
