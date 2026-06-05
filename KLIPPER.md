# FLARE — Klipper Integration Guide

This guide describes how to connect Klipper to your FLARE controller, set up the communication daemon, copy-paste the required macro configurations, and integrate status tracking into your Mainsail or Fluidd dashboard.

---

## 📋 Step 1: Install the Background Daemon

To allow Klipper macros and WebUIs (Mainsail/Fluidd) to talk to FLARE at the same time without serial port conflicts, you must install the background daemon.

1. SSH into your Klipper host (usually a Raspberry Pi).
2. Clone the repository (if you haven't already):
   ```bash
   cd ~
   git clone https://github.com/wight554/FLARE.git
   ```
3. Run the installer script:
   ```bash
   cd ~/FLARE
   bash scripts/install_daemon.sh
   ```
   *The installer copies the required Klipper python mock files (`mmu.py`, `mmu_sensors.py`) into your Klipper installation, registers a `systemd` background service (`flare_daemon.service`), and creates the serial symlink `/dev/ttyACM0` proxy.*

---

## 🔌 Step 2: Configure the Serial Connection in Klipper

FLARE uses a persistent local helper script `scripts/flare_cmd.py` to route macro commands through the daemon to the controller.

1. Ensure the Klipper `gcode_shell_command` extension is installed (you can install this via **KIAUH** -> **Advanced** -> **gcode_shell_command**).
2. Add the shell command definition to your `printer.cfg`:
   ```ini
   [gcode_shell_command flare]
   command: python3 /home/pi/FLARE/scripts/flare_cmd.py
   timeout: 300.0
   verbose: True
   ```
   *(Adjust `/home/pi/` to match your home directory if your username is not `pi`)*

3. Test the shell command by running this in the Mainsail/Fluidd console:
   ```
   RUN_SHELL_COMMAND CMD=flare PARAMS="?:"
   ```
   It should return a status reply (e.g. `OK:LN:0:I1:0:O1:0...`).

---

## 📝 Step 3: Copy-Paste the Macros Config

FLARE provides a pre-configured macros file `klipper/flare_mmu.cfg` that contains the toolchange commands (`T1`, `T2`), load/unload scripts, tip forming routines, and dashboard gates.

1. Copy the file `klipper/flare_mmu.cfg` into your printer configuration folder (where `printer.cfg` resides).
2. Add the include statement at the top of your `printer.cfg`:
   ```ini
   [include flare_mmu.cfg]
   ```

---

## 🎛️ Step 4: Configure Your Toolhead Sensor (Optional but Recommended)

While FLARE can load and unload filament without a toolhead sensor, having one allows for much safer and faster toolchanges.

1. Add your toolhead switch sensor config to your `printer.cfg`:
   ```ini
   [filament_switch_sensor toolhead_sensor]
   switch_pin: ^!toolhead:PA0   ; Adjust this pin to match your actual toolhead board pin
   pause_on_runout: False
   insert_gcode:
       _FLARE_ON_TOOLHEAD_INSERT
   runout_gcode:
       _FLARE_ON_TOOLHEAD_RUNOUT
   ```
2. Open `flare_mmu.cfg` and configure the sensor name in `_FLARE_VARS` so Klipper knows which sensor to watch:
   ```ini
   [gcode_macro _FLARE_VARS]
   variable_toolhead_sensor: "toolhead_sensor"
   ```

3. Reconcile the board's toolhead state from the physical sensor at print
   lifecycle edges by calling `_FLARE_SYNC_TOOLHEAD` from your own macros. This
   recovers from desyncs where the board's view drifts from reality — e.g. a
   print cancelled mid-toolchange can leave the board flagged empty while
   filament is still loaded, which otherwise makes the next `T0` unload a
   correctly-loaded toolhead.

   ```ini
   # in your PRINT_START and PRINT_END
   _FLARE_SYNC_TOOLHEAD

   # in your CANCEL_PRINT (and any error/abort path)
   _FLARE_SYNC_TOOLHEAD RESET=1   ; also clears a stuck mmu_active
   ```

   `_FLARE_SYNC_TOOLHEAD` reads the physical sensor, pushes the truth to the
   board (`TS:1`/`TS:0`), and issues `BS` for buffer safety. Pass `RESET=1` only
   on cancel/error (never while a toolchange is actually running).

---

## 🏎️ Step 5: Calibrate Distances & Tip Forming

Inside `flare_mmu.cfg`, look for the `_FLARE_VARS` section to tune physical distances:

- `dist_sensor_to_synced_move`: The distance (in mm) from your toolhead sensor to the drive gears.
- `dist_filament_park`: The distance (in mm) from the nozzle where the filament parks when unloaded.
- `use_buffer_lock` (`0` or `1`): Set to `1` to enable **Buffer Lock** mode during tip-forming. This pulls the filament into tension before the extruder cuts/retracts, giving you much cleaner tips.

Test your tip forming by running the command:
```
FLARE_TEST_TIP_FORMING
```
This loads filament, performs the tip forming shape, and unloads it for your physical inspection.

---

## 📊 Mainsail / Fluidd Dashboard Integration

Because the install daemon registers mock `[mmu]` and `[mmu_sensors]` modules, Mainsail and Fluidd will automatically discover the FLARE controller. You will see:
1. **Gate Track Indicators**: Filament status dots for both lanes (preloaded, loaded, or empty).
2. **Buffer Piston Visualizer**: A sliding block representing the live position of your spring-trolley buffer.
3. **Control Buttons**: `MMU_LOAD` and `MMU_EJECT` buttons on the dashboard will function correctly, selecting and loading the chosen gate automatically.

---

## 🔍 Troubleshooting Connection Problems

| Symptom | Likely Cause | How to Fix |
|---|---|---|
| Console says `no serial port found` | The daemon is stopped or USB is unplugged. | Run `sudo systemctl status flare_daemon` to verify the background service is running. |
| Toolhead sensor trigger does not load hotend | Sensor state is not reaching Klipper. | Verify by pushing filament into the toolhead and running `QUERY_FILAMENT_SENSOR SENSOR=toolhead_sensor` in console. |
| Toolchange `TC:` command times out | Filament travel is blocked or bowden is too long. | Verify path clearance. You can temporarily increase travel timeouts (`LOAD_MAX` / `UNLOAD_MAX`) in `config.ini`. |
| Buffer piston stays frozen in WebUI | Sync feedback is disabled or type mismatch. | Check `buf_sensor_type` in `config.ini` matches your hardware (`0` for switches, `1` for Hall sensor/analog). |
