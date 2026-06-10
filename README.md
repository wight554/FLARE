# FLARE

**Filament Lane Automation and Reload Engine**

FLARE is standalone, dual-lane MMU and filament reload controller firmware for RP2040 boards (specifically the FYSETC ERB v2.0). 

It is designed to give you autonomous filament runout redundancy (auto-switching to a standby spool) and two-lane multi-material capability without requiring complex host plugins or Klipper macros to run the core state machines.

---

## Key Features

- **Autonomous RELOAD Mode**: Pair two spools as a redundant supply. If the active lane runs out, the controller automatically unloads, swaps lanes, and preloads the standby lane without host-side intervention.
- **Standalone Operation**: The RP2040 firmware executes all motion, sensor debouncing, cutter control, and buffer sync logic locally. If USB disconnects, the print keeps running safely.
- **Sync-Feedback Sensor Support**: Built-in support for Type-D (dual-endstop/switch-based) and Type-P (analog/Hall-effect) spring-trolley buffers to match filament feed rate dynamically.
- **Easy Host Integration**: Communicates via standard USB serial. A simple command utility (`scripts/flare_cmd.py`) bridges to Klipper macros, and a lightweight background service (`flare_daemon.py`) updates Mainsail/Fluidd dashboards natively.

---

## Operating Modes

FLARE is governed by two independent settings:

1. **AUTO_MODE (Flow Control)**: 
   - `AUTO_MODE:1` (Default): Automates filament loading. Inserting filament triggers auto-preload to the drive gears. Reaching the buffer tension trigger automatically starts motor sync.
   - `AUTO_MODE:0`: Host-controlled. No unsolicited movements. The firmware waits for direct serial commands (`LO:`, `FL:`, `UL:`, etc.) and only reports status events.
2. **RELOAD_MODE (Redundancy)**:
   - `RELOAD_MODE:1`: If the active lane runs out during a print, the controller automatically performs a toolchange to the standby lane.
   - `RELOAD_MODE:0` (Default): Standard MMU behavior. Reports runout to Klipper but does not trigger autonomous swaps.

---

## Quick Start for Operators

### 1. Flash the Firmware
Prepare your configuration and flash the controller:
```bash
# 1. Copy the example configuration template
cp config.ini.example config.ini

# 2. Edit config.ini to match your motor currents, steps, and sensor type
# 3. Generate the config header and build the firmware
python3 scripts/gen_config.py
cmake -S firmware -B build_local -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build_local

# 4. Connect your ERB board and flash it
python3 scripts/flash_flare.py
```
*For detailed step-by-step flashing instructions, see [BUILD_FLASH.md](BUILD_FLASH.md).*

### 2. Physical Setup & Wiring
Wire your stepper motors, endstops, and sensors to the ERB board.
*For pin diagrams and wiring guide, see [HARDWARE.md](HARDWARE.md).*

### 3. Connect to Klipper
Install the background helper daemon and configure Klipper macros to integrate FLARE with Mainsail or Fluidd.
*For macro integration and setup, see [KLIPPER.md](KLIPPER.md).*

### 4. Basic Operation Commands
You can interact with the board directly using the command script:
```bash
# Check status of both lanes (sensors, active lane, tasks)
python3 scripts/flare_cmd.py "?:"

# Select active lane (1 or 2)
python3 scripts/flare_cmd.py "T:1"

# Unload the active filament back to the gate
python3 scripts/flare_cmd.py "UL:"

# Stop all motors immediately
python3 scripts/flare_cmd.py "ST:"
```

---

## Documentation Guide

Where to find details for your project phase:

- 🛠️ **Assembly & Wiring**: [HARDWARE.md](HARDWARE.md) — Connect motors, endstops, and buffer sensors.
- ⚡ **Flashing & Building**: [BUILD_FLASH.md](BUILD_FLASH.md) — Compile and load the firmware onto the RP2040.
- ⚙️ **Klipper Macros & Dashboard**: [KLIPPER.md](KLIPPER.md) — Copy-paste configs and set up toolchange macros.
- ⚖️ **Calibration & Tuning**: [TUNING.md](TUNING.md) — Step-by-step guide to tune filament feed rates and buffer responsiveness.
- 📖 **API Command Reference**: [MANUAL.md](MANUAL.md) — Detailed documentation of the USB serial protocol.
- 🧪 **Hardware Test Cases**: [TEST_CASES.md](TEST_CASES.md) — Manual validation checks to perform on first startup.

---

## Safety Guidelines

> [!WARNING]
> - Always perform first-motion tests with reduced speeds to prevent motor stalls or damage.
> - Keep filament clear of the toolhead path until your basic autoload and unload commands are verified.
> - Be ready to run the Stop command (`scripts/flare_cmd.py "ST:"`) if any motor behaves unexpectedly.
> - Do not run autonomous RELOAD mode prints completely unattended until you have verified sensor triggers and path clearance.

---

## AI-Assisted Development

FLARE is implemented using AI coding agents, but is strictly **human-directed and spec-driven**, not "AI slop."

Every change proposal, requirements specification, and design decision is written and reviewed by the human maintainer before code is executed. For AI developer settings and rules, see [AI.md](AI.md) and [AGENTS.md](AGENTS.md).
