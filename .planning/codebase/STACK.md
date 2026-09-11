# Technology Stack

**Analysis Date:** 2026-09-11

This document provides a comprehensive technical reference for the FLARE ("Filament Lane Automation and Reload Engine") codebase. It covers core programming languages, embedded and host runtime environments, build toolchains, microcontroller and motor-driver hardware specifications, software dependencies, and the multi-tier configuration architecture.

---

## 1. Programming Languages

FLARE spans embedded bare-metal firmware, host daemon multiplexing, Klipper plugin facades, and test/calibration toolchains.

| Language | Standard / Version | Primary Locations | Purpose / Characteristics |
|---|---|---|---|
| **C** | C11 (`set(CMAKE_C_STANDARD 11)`) | `firmware/src/`, `firmware/include/` | Bare-metal firmware running on the RP2040 MCU. Zero RTOS; single-threaded cooperative superloop with deterministic tick scheduling (`firmware/src/main.c`). Clean builds under `-Wall -Wextra`. |
| **Python** | Python 3.8+ (PEP 8, Ruff enforced) | `scripts/`, `klipper/`, `tests/` | Host serial daemon (`scripts/flare_daemon.py`), CLI client (`scripts/flare_cmd.py`), code generator (`scripts/gen_config.py`), Klipper MMU mock (`klipper/mmu.py`), sync/calibration analysis tools (`scripts/flare_analyze.py`). |
| **RP2040 PIO Assembly** | Raspberry Pi Pico PIO ASM | `firmware/src/tmc_uart.pio`, `firmware/third_party/ws2812.pio` | Hardware-timed peripheral bit-banging: half-duplex single-wire UART for TMC2209 at 40k baud; timing-accurate 800 kHz NRZ waveform for WS2812 NeoPixel LEDs. |
| **Shell (Bash / Zsh)** | POSIX / Bash / Zsh | `scripts/install_daemon.py` invocations, build scripts, development workflows | Environment bootstrap, service installation, cross-toolchain execution. |
| **Klipper G-Code / Jinja2** | Klipper Extended G-code | `klipper/flare_mmu.cfg` | Printer-side macro orchestration (`T0`, `T1`, `_FLARE_TIP_FORMING`, `_FLARE_BL_RETRACT`, `_FLARE_LOAD_HOTEND`). |

---

## 2. Embedded Runtime & Toolchain

### 2.1 Hardware Microcontroller

- **Core**: Raspberry Pi RP2040 silicon.
- **Architecture**: Dual ARM Cortex-M0+ cores @ 133 MHz (Core 0 runs the full FLARE superloop; Core 1 remains in sleep/unclaimed state).
- **Internal Memory**: 264 KB SRAM in 6 banks.
- **Non-Volatile Storage**: 2 MB external SPI NOR flash.
- **Dedicated Hardware Peripherals Utilized**:
  - **PWM**: Hardware slice/channel motor step generation (`pwm_set_wrap`, `pwm_set_clkdiv`), cutter servo control (`firmware/src/cutter.c`).
  - **PIO (Programmable I/O)**: `pio0` running dual state machines for TMC UART; `pio1` running WS2812 NeoPixel output.
  - **ADC**: Successive-approximation 12-bit ADC (channel 3 / GPIO 29) for Type-P analog buffer sensing.
  - **Hardware Watchdog**: Reset recovery and runaway guard (`hardware_watchdog`), ticking in the cooperative superloop.
  - **Flash Controller**: 4 KB sector erase/program for non-volatile settings storage (`hardware_flash`).

### 2.2 Board Target: FYSETC ERB v2.0

The FYSETC Enraged Rabbit Burrow (ERB) v2.0 board houses the RP2040, two Trinamic TMC2209 drivers, endstop pin headers, servo outputs, CAN transceiver, and an onboard buck regulator:

- **Power Architecture**:
  - External 24V supply powers stepper motor coils and the RY9330BP8 buck regulator (24V -> 5V / 3A max).
  - ME6217C33M5G LDO converts 5V -> 3.3V for RP2040 MCU logic and stepper `VCC_IO`.
  - Jumper `JP2` connects USB 5V to board 5V for logic flashing without 24V (*Warning: Never connect 24V while JP2 is closed*).
- **Sense Resistors**: 0.110 Ω onboard sense resistors (`CONF_RSENSE_OHM 0.110f` in `firmware/include/config.h`).

### 2.3 Compiler & Build Tools

The build pipeline relies on modern ARM cross-compilation tools and CMake presets:

- **Cross Compiler**: `arm-none-eabi-gcc` / `arm-none-eabi-g++` (GCC 10.3+ or 12.x/13.x recommended).
- **Build Generator**: `CMake` >= 3.13 (`firmware/CMakeLists.txt`), generating `Ninja` build files.
- **SDK**: Raspberry Pi Pico SDK 2.x (`pico_sdk_import.cmake`, `pico_sdk_init()`).
- **Standard Library**: Newlib C runtime provided by arm-none-eabi toolchain, linked with `pico_stdlib`.
- **Clang Quality Toolchain** (Canonical macOS environment via Homebrew `llvm@22`):
  - `clang-format` version 22.x enforcing `.clang-format`.
  - `clang-tidy` version 22.x enforcing `.clang-tidy` against `compile_commands.json`.
- **Flashing Utilities**:
  - `picotool`: Native CLI for flashing RP2040 via SWD or USB bootloader (`picotool load -f <uf2>`, `picotool reboot`).
  - Mass storage drag-and-drop: RP2040 ROM bootloader mounts as `RPI-RP2` FAT volume when held in BOOTSEL mode or triggered via protocol `BOOT:`.

---

## 3. Stepper Driver & Motion Architecture

FLARE drives two filament lanes (Lane 1 / X, Lane 2 / Y) independently using Trinamic TMC2209 silent stepper motor drivers.

### 3.1 Pinout & Bus Mapping

| Peripheral / Signal | RP2040 GPIO | Net Label on ERB v2.0 | Hardware Description / Mode |
|---|---|---|---|
| **Lane 1 Enable (ENN)** | GPIO 8 | `X-EN` | Active-LOW stepper coil enable (`EN_ACTIVE_LOW 1`). |
| **Lane 1 Direction (DIR)** | GPIO 9 | `X-DIR` | Digital output determining motor rotation direction. |
| **Lane 1 Step (STEP)** | GPIO 10 | `X-STEP` | Hardware PWM slice output generating step pulses. |
| **Lane 1 UART (PDN)** | GPIO 11 | `X-UART` | Single-wire bidirectional UART (half-duplex) @ 40k baud. |
| **Lane 1 Diagnostic (DIAG)** | GPIO 13 | `X-DIAG` | TMC2209 stall/diag output (wired; not used in current sync loop). |
| **Lane 2 Enable (ENN)** | GPIO 14 | `Y-EN` | Active-LOW stepper coil enable. |
| **Lane 2 Direction (DIR)** | GPIO 15 | `Y-DIR` | Digital output determining motor rotation direction. |
| **Lane 2 Step (STEP)** | GPIO 16 | `Y-STEP` | Hardware PWM slice output generating step pulses. |
| **Lane 2 UART (PDN)** | GPIO 17 | `Y-UART` | Single-wire bidirectional UART (half-duplex) @ 40k baud. |
| **Lane 2 Diagnostic (DIAG)** | GPIO 19 | `Y-DIAG` | TMC2209 stall/diag output. |
| **Lane 1 In Sensor** | GPIO 2 | `X-MIN` / `SPI0_SCK` | Optomechanical switch detecting filament insertion at spool inlet. |
| **Lane 1 Out Sensor** | GPIO 3 | `X-MAX` / `SPI0_MISO` | Switch detecting filament entering the drive gear. |
| **Lane 2 In Sensor** | GPIO 4 | `Y-MIN` / `SPI0_MOSI` | Optomechanical switch detecting filament insertion at spool inlet. |
| **Lane 2 Out Sensor** | GPIO 5 | `Y-MAX` / `SPI0_CS` | Switch detecting filament entering the drive gear. |
| **Y-Splitter Sensor** | GPIO 6 | `I2C1_SDA` | Switch detecting filament merging into common bowden guide. |
| **Buffer Tension** | GPIO 18 | Header Pin | Microswitch triggered when buffer spring trolley is pulled taut. |
| **Buffer Compression** | GPIO 12 | `PRE_GATE_0` | Microswitch triggered when buffer spring trolley is compressed. |
| **Buffer Analog (Type-P)** | GPIO 29 (ADC3) | Header Pin | 12-bit analog input for continuous Hall-effect / potentiometer buffer. |
| **Cutter Servo PWM** | GPIO 23 | `SERVO` | 50 Hz PWM channel for mechanical blade actuator. |
| **Status NeoPixel** | GPIO 21 | `NEO_PIXEL` | Single WS2812B RGB indicator on `pio1`. |

### 3.2 TMC2209 UART Topology & Electrical Invariants

The ERB v2.0 hardware has direct copper traces between the RP2040 GPIOs and the TMC2209 `PDN_UART` pins without isolation resistors or external pull-ups:

- **Baud Rate**: 40,000 baud (`TMC_BAUD 40000u` in `firmware/src/tmc2209.c`).
- **Addressing**: Both drivers use slave address `0` (`MS1`/`MS2` pulled LOW). They are addressed independently because they reside on physically separate GPIOs (GPIO 11 for U7, GPIO 17 for U8).
- **Half-Duplex Bus Contention Guard**:
  - The RP2040 pin drive strength is constrained to 2 mA (`GPIO_DRIVE_STRENGTH_2MA`) in `firmware/src/tmc_uart.pio`.
  - While idle, the RP2040 drives the pin HIGH at 2 mA. When the TMC2209 replies, its open-drain driver pulls the line LOW to overpowering the 2 mA pull-up.
  - Internal pull-up is maintained on the RP2040 (`gpio_pull_up`) to keep the bus high when switching to input mode.
- **Standby Avoidance**: Holding `PDN_UART` HIGH for $\ge 1\text{ ms}$ while `pdn_disable = 0` forces the TMC2209 into low-power standby mode, breaking serial communications. The driver pre-conditioning sequence maintains pulse timing below 100 µs (`TMC_STEALTHCHOP_WRITE_SETTLE_US`) to prevent accidental standby entry.
- **Register Configuration**: Configured over UART for microstepping (`1` to `256`), chopper timing (`tbl`, `toff`, `hstrt`, `hend`), run/hold current via `IHOLD_IRUN`, and `TPWMTHRS` velocity threshold for StealthChop vs SpreadCycle switching.

### 3.3 Hardware PWM Step Generation

Instead of consuming timer interrupts per step, FLARE offloads pulse generation directly to RP2040 hardware PWM slices (`firmware/src/motion.c`):
- One PWM period corresponds exactly to one motor step.
- Step frequency (Steps Per Second, SPS) is calculated from system clock:
  $$\text{freq} = \frac{\text{clk\_sys}}{\text{div} \times (\text{wrap} + 1)}$$
- `wrap` counter is centered at 50% duty cycle (`wrap / 2`) for clean symmetrical step pulses.
- Distance tracking is computed in software by integrating active SPS over the discrete ramp tick interval (`RAMP_TICK_MS = 5` ms).

---

## 4. Software Dependencies

### 4.1 Embedded Dependencies (`firmware/`)

- **Pico SDK 2.x Libraries**:
  - `pico_stdlib`: Standard runtime, timing functions (`to_ms_since_boot`, `get_absolute_time`), stdio routing.
  - `pico_stdio_usb`: TinyUSB CDC serial stack for host communication over USB (`PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000`).
  - `hardware_gpio`: Pin direction, pull-up/down, digital I/O.
  - `hardware_pwm`: Dual-channel hardware counters for step generation and servo control.
  - `hardware_pio`: State-machine control for TMC UART and NeoPixel drivers.
  - `hardware_adc`: 12-bit round-robin or one-shot analog conversions for Type-P buffer sensors.
  - `hardware_flash`: Safe sector erasure and page programming for persistent configuration.
  - `hardware_watchdog`: Hardware timer rebooting the MCU if the cooperative superloop stalls.
  - `hardware_sync`: Critical section protection (`save_and_disable_interrupts`, `restore_interrupts`).

### 4.2 Host Runtime & Python Environment (`scripts/`, `klipper/`)

The host side runs on Linux (e.g. Raspberry Pi OS running Klipper) or macOS for testing.

- **Python Version**: Standard CPython 3.8, 3.9, 3.10, 3.11, or 3.12.
- **Standard Library Modules Utilized**: `argparse`, `configparser`, `glob`, `json`, `math`, `os`, `queue`, `re`, `socket`, `sqlite3`, `sys`, `threading`, `time`, `urllib.request`, `urllib.error`.
- **External Python Packages**:
  - `pyserial` >= 3.5: Low-level serial communication with exclusive port locking (`serial.Serial(port, 115200, timeout=0.5, exclusive=True)`).
- **Development & Formatting Tools** (`pyproject.toml`):
  - `ruff` >= 0.1.0: Fast linter and formatter for all Python files.
  - `pytest` >= 7.0.0: Automated unit and regression test runner.

---

## 5. Configuration Architecture

FLARE employs a three-tier configuration model to separate user-facing mechanical adjustments, flash-persisted runtime tunables, and safety-critical control-loop algorithms.

```
+-------------------------------------------------------------------------+
|                                 TIER 1                                  |
|        config.ini (User / Board Properties & Mechanical Dimensions)     |
|      - microsteps, rotation_distance, run_current, sensor types         |
+-------------------------------------------------------------------------+
                                    │
                                    ▼ python3 scripts/gen_config.py
+-------------------------------------------------------------------------+
|                       firmware/include/tune.h                           |
|       Compile-time motor conversion factors, macro defaults, scales     |
+-------------------------------------------------------------------------+
                                    │
                                    ▼ Compiled into Flash
+-------------------------------------------------------------------------+
|                                 TIER 2                                  |
|     Flash-Persisted Runtime Settings (firmware/src/settings_store.c)     |
|       - settings_t struct (256-byte aligned, CRC32 guarded)             |
|       - Live tunable via SET: / GET:, persisted via SV:, reset via RS:  |
+-------------------------------------------------------------------------+
                                    │
                                    ▼ Guarded by Algorithms
+-------------------------------------------------------------------------+
|                                 TIER 3                                  |
|       Internal Control-Loop Constants (firmware/include/tune_internal.h)|
|       - EWMA alphas, estimator caps, drift observer tau, relay timings  |
|       - Release build: compiled static constants (not tunable)          |
|       - Dev build (-DFLARE_DEV_TUNING=ON): ephemeral SET: overrides     |
+-------------------------------------------------------------------------+
```

### 5.1 Tier 1: User Configuration (`config.ini`)

`config.ini` contains all physical and mechanical machine constants. A pre-build step executed by `scripts/gen_config.py` converts these values into C header definitions in `firmware/include/tune.h`:

- **Mandatory Keys**:
  - `microsteps`: Microstepping resolution (`16`, `32`, etc.).
  - `rotation_distance`: Filament travel per full motor rotation in mm (e.g. `23.0`).
  - `run_current`: Stepper driver RMS current in Amperes (e.g. `0.8`).
- **Physical Model Geometry**:
  - `dist_in_out`: Distance between IN and OUT sensors (default: 150 mm).
  - `dist_out_y`: Distance between OUT sensor and Y-splitter (default: 100 mm).
  - `dist_y_buf`: Distance between Y-splitter and buffer entrance (default: 300 mm).
  - `buf_switch_span_mm`: Physical distance between tension and compression switches (default: 10 mm).
  - `buf_max_travel_mm`: Total physical mechanical travel of the buffer carriage (default: 25 mm).

### 5.2 Tier 2: Flash-Persisted Runtime Settings (`settings_store.c`)

Parameters that require live calibration without recompilation are stored in the RP2040's top NOR flash sector (`0x101FF000`, 4096 bytes).

- **Structure Contract (`settings_t`)**:
  - Header with magic number (`0x464C5231` = "FLR1") and `version`.
  - CRC32 checksum protecting against incomplete writes or power loss.
  - Flash-wear protection: Cumulative erase counter (`FLASH_ERASE_COUNT`). When erase cycles reach `FLASH_WEAR_WARN_THRESHOLD` (80,000), `EV:FLASH:WEAR_WARNING` is raised.
- **Protocol Access**:
  - `SET:<PARAM>:<VALUE>`: Modifies parameter in runtime RAM.
  - `GET:<PARAM>`: Reads parameter from runtime RAM.
  - `SV:`: Serializes RAM values into `settings_t` and writes to flash.
  - `LD:`: Reads flash into RAM, validating CRC32.
  - `RS:`: Restores compiled factory defaults from `tune.h` and saves to flash.
- **Parity Test**: `scripts/test_settings_parity.py` statically validates that every field in `settings_t` has matching save, load, and default assignment logic.

### 5.3 Tier 3: Internal Control-Loop Constants (`tune_internal.h`)

Tier-3 constants define the inner dynamics of the buffer estimation filters, drift observers, and relay laws. To prevent operational misconfiguration, these are compiled directly into the binary from `firmware/include/tune_internal.h`:

- **Estimator Confidence**: `FLARE_INT_EST_LOW_CF_WARN_THRESHOLD` (0.5), `FLARE_INT_EST_FALLBACK_CF_THRESHOLD` (0.2).
- **Drift Observer**: `FLARE_INT_BUF_DRIFT_EWMA_TAU_MS` (60,000 ms), `FLARE_INT_BUF_DRIFT_APPLY_THR_MM` (2.0 mm), `FLARE_INT_BUF_DRIFT_CLAMP_MM` (3.0 mm).
- **Safety Dwell Limits**: `FLARE_INT_SYNC_TENSION_DWELL_STOP_MS` (6,000 ms).
- **Relay Laws**: `FLARE_INT_RELAY_COLLAPSE_DELAY_MS` (250 ms), `FLARE_INT_RELAY_COLLAPSE_RAMP_MULT` (3).
- **Dev-Build Surface**: Building with `-DFLARE_DEV_TUNING=ON` enables ephemeral runtime `SET:` overrides for these constants, allowing bench exploration without modifying persistent flash schemas.

---

*Pre-commit compliance: Run `clang-format -i firmware/src/*.c firmware/include/*.h` and `ruff check scripts/` before submitting pull requests.*
<!-- refreshed: 2026-09-11 -->
