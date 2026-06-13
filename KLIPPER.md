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
   sudo python3 scripts/install_daemon.py
   ```
   *The installer copies the required Klipper python mock files (`mmu.py`, `mmu_sensors.py`) into your Klipper installation, and registers a `systemd` background service (`flare_daemon.service`).*

   > [!IMPORTANT]
   > Klipper updates will overwrite Klipper's `klippy/extras` directory and delete the FLARE mock files. You must re-run `sudo python3 scripts/install_daemon.py` (or manually copy the mock files back) after updating Klipper.

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

If your WebUI console still shows historical `SET_MMU` status traffic, add
`^SET_MMU` to the Mainsail/Fluidd hidden-command filter. This only hides console
echo; it does not change daemon behavior or MMU dashboard updates.

---

## 🌐 Daemon HTTP API

The FLARE daemon runs an HTTP & SSE server on port `8088`. By default, it binds to loopback (`127.0.0.1`) for security. To expose the API to your local network (e.g., for viewing the dashboard from another device), you can run the daemon with `--host 0.0.0.0` (opt-in).

### Endpoints:
- `GET /` or `GET /index.html` - Returns the interactive HTML dashboard/status visualizer.
- `GET /status` - Returns a JSON object with the current controller state.
- `GET /telemetry` - Server-Sent Events (SSE) stream of real-time event logs.
- `GET /config` - Returns JSON configuration.
- `GET /gatemap` - Returns JSON representation of the gate mapping.
- `POST /gatemap` - Update gate configuration details.
- `POST /cmd` - Accepts a JSON payload containing a serial command to execute directly on the controller, e.g. `{"cmd": "LO:1"}`.

---

## 🔍 Troubleshooting Connection Problems

| Symptom | Likely Cause | How to Fix |
|---|---|---|
| Console says `no serial port found` | The daemon is stopped or USB is unplugged. | Run `sudo systemctl status flare_daemon` to verify the background service is running. |
| Toolhead sensor trigger does not load hotend | Sensor state is not reaching Klipper. | Verify by pushing filament into the toolhead and running `QUERY_FILAMENT_SENSOR SENSOR=toolhead_sensor` in console. |
| Toolchange `TC:` command times out | Filament travel is blocked or bowden is too long. | Verify path clearance. You can temporarily increase travel timeouts (`LOAD_MAX` / `UNLOAD_MAX`) in `config.ini`. |
| Buffer piston stays frozen in WebUI | Sync feedback is disabled or type mismatch. | Check `buf_sensor_type` in `config.ini` matches your hardware (`0` for switches, `1` for Hall sensor/analog). |
| FLARE telemetry (gate status, buffer, SPS) freezes in Mainsail/Fluidd during a long command | Expected: the daemon pauses mirror pushes while the Klipper gcode lock is held by a blocking command (e.g. `MPC_CALIBRATE`, which can run 10–15 minutes). The UI will be stale for the duration. Once the command finishes, the daemon detects the idle state and resumes with a full resync — all fields will be up to date within one loop tick. This is not a fault; no action is needed. |
