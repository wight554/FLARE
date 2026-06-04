# Build and Flash Guide

This guide describes how to get FLARE controller firmware running on your FYSETC ERB v2.0 board.

---

## ⚡ Fast Path: Flashing an Existing UF2

If you have a pre-compiled `flare_controller.uf2` file, you can flash it immediately using physical boot buttons or `picotool`.

### Option A: Physical Button Method (BOOTSEL)
Use this for the very first flash, or if the board is unresponsive.
1. Connect a 24V power supply to the ERB board to power it on.
2. Connect a USB-C cable from the ERB board to your computer or Klipper host (Raspberry Pi).
3. Press and hold the physical **BOOT** button on the ERB board.
4. Press the **RST** (Reset) button and hold for 0.5 seconds.
5. Release the **RST** button, wait 3 seconds, and then release the **BOOT** button.
6. The board will mount on your computer as a USB mass storage drive named `RPI-RP2`.
7. Drag and drop the `flare_controller.uf2` file onto the drive. The board will automatically reboot and start running FLARE.

### Option B: Command Line with picotool
If `picotool` is installed on your host:
```bash
picotool load flare_controller.uf2 -f
picotool reboot
```

---

## 🚀 Easy Path: Build & Flash with Script

If you want to configure motor settings and automatically build and flash in one step, use the provided helper script:

1. **Configure settings**:
   Copy the example file to `config.ini` and edit it to set your motor currents, steps, and sensor type:
   ```bash
   cp config.ini.example config.ini
   ```
2. **Run the script**:
   ```bash
   bash scripts/flash_flare.sh
   ```
   *The script automatically generates the config, compiles the firmware, detects the serial port, puts the board in bootloader mode, and flashes it using `picotool`.*

---

## 🛠️ Advanced Path: Manual Compile & Build

Follow these steps if you want to modify the C source code or build the project manually.

### 1) Prerequisites
Install the required compile tools on your system:
- `cmake`
- `ninja`
- `arm-none-eabi-gcc` (Cross-compiler)
- Pico SDK source directory

### 2) Generate Config Header
Compile-time configurations are generated from `config.ini` into `firmware/include/tune.h`:
```bash
python3 scripts/gen_config.py
```

### 3) Configure CMake & Compile
Setup the build directory and run the compiler:
```bash
# Configure the project (set PICO_SDK_PATH to your Pico SDK directory)
cmake -S firmware -B build_local -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk

# Compile the firmware
cmake --build build_local
```
Outputs are located in `build_local/`:
- `flare_controller.uf2` (For bootloader drag-and-drop or picotool)
- `flare_controller.elf` (For debuggers)

---

## Troubleshooting

### CMake cannot find Pico SDK
Provide the path explicitly when running `cmake` for the first time:
```bash
cmake -S firmware -B build_local -G Ninja -DPICO_SDK_PATH=/abs/path/to/pico-sdk
```

### Flashing script says picotool not found
If `picotool` is not in your system PATH, you can pass the path to the script:
```bash
PICOTOOL=/path/to/picotool bash scripts/flash_flare.sh
```

### Board does not show as a serial port after flashing
1. Unplug and replug the USB cable.
2. Ensure you have 24V power connected to the board (USB-only power might not start the firmware properly on some setups).
3. Verify the firmware compiled without errors. You can check communications by sending a version request command:
   ```bash
   python3 scripts/flare_cmd.py "VR:"
   ```
