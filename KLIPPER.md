# FLARE — Klipper Integration

This document covers connecting Klipper to FLARE: serial setup, the shell
command helper, toolhead sensor wiring, `flare_mmu.cfg`, and troubleshooting.

For the FLARE command reference see `MANUAL.md`; for behavioral details see
`BEHAVIOR.md`.

FLARE is the firmware/tooling namespace. The examples below intentionally use
`flare` command names because the project has not yet shipped a stable external
API.

---

## Serial port setup

A FLARE controller appears as a USB CDC serial device on the Raspberry Pi
(`/dev/ttyACM0` or `/dev/ttyACM1`).

Confirm the port:
```bash
ls /dev/ttyACM*
# identify which is which if more than one device:
dmesg | grep ttyACM
```

The Pi user must be in the `dialout` group:
```bash
sudo usermod -a -G dialout pi   # substitute your username if not 'pi'
# log out and back in for the change to take effect
```

---

## Shell command helper — flare_cmd.py

`scripts/flare_cmd.py` sends a single FLARE command and blocks until the
response arrives. Simple commands (SET:, GET:, T:, SM:, TS:, FD:, ST:, TC:,
MV:, …) return on the first `OK:`/`ER:`. Long-running commands (`FL:`, `UL:`,
`UM:`, `CU:`, `CX:`) wait for their completion event (`EV:LOADED`,
`EV:UNLOADED`, `EV:CUT:DONE`, …) or the corresponding error/timeout event.
Exit code is 0 on success, 1 on error or timeout. All received lines are
printed so Klipper's `VERBOSE` output shows them in the Mainsail / Fluidd
console.

The helper filename and sample `gcode_shell_command flare` name are the active
development interface.

Install the Klipper `gcode_shell_command` extension if not already present
(available via KIAUH → Advanced, or copy `gcode_shell_command.py` to
`~/klipper/klippy/extras/`).

Add to `printer.cfg`:
```ini
[gcode_shell_command flare]
command: python3 /home/pi/FLARE/scripts/flare_cmd.py
timeout: 300.0
verbose: True
```

Adjust the path to match your Pi home directory. Test it with:
```
RUN_SHELL_COMMAND CMD=flare PARAMS="?:"
```

---

## Toolhead filament sensor — TS:

FLARE can use a toolhead filament sensor, but `TC:` does not require one.
`TASK_LOAD_FULL` also completes from sane buffer geometry after OUT, so `TS:1`
and `TS_BUF_MS` are accelerators rather than hard gates.

### Option A — Physical sensor (recommended)

Wire a microswitch or optical sensor to a free GPIO on the printer MCU. Add to
`printer.cfg`:

```ini
[filament_switch_sensor toolhead_sensor]
switch_pin: ^!toolhead:PA0   ; adjust pin and MCU name
pause_on_runout: False
insert_gcode:
    _FLARE_ON_TOOLHEAD_INSERT
runout_gcode:
    _FLARE_ON_TOOLHEAD_RUNOUT
```

Set `toolhead_sensor: "toolhead_sensor"` in `_FLARE_VARS` to match the sensor
name after `filament_switch_sensor `. FLARE firmware can still detect load via
buffer geometry without a physical sensor, but the shared `flare_mmu.cfg`
post-TC hotend-load gate uses this Klipper sensor state.

---

## Toolchange macros — TC:

`TC:<lane>` unloads the current lane (cuts when `UNLOAD_CUT=1` and the cutter
is enabled), swaps, loads the new lane, and completes when the lane load task
reports loaded (`TS:1`, `TS_BUF_MS`, or sane buffer geometry).
`flare_cmd.py` returns after FLARE accepts `TC:`. The shared macro relies on
the delayed toolhead-sensor gate below for post-TC hotend loading instead of
holding Klipper inside the shell command.
The shared `_FLARE_CHANGE_LANE` macro does not place hotend loading after
`RUN_SHELL_COMMAND` directly. Instead, it arms `_FLARE_TC_STATE`; when the
toolhead sensor reports runout, `_FLARE_ON_TOOLHEAD_RUNOUT` starts a
delayed-gcode poll. After the same physical toolhead sensor reports filament
detected again while TC is pending, the macro waits 2 seconds before
`_FLARE_POST_TC_LOAD` starts. This gives the MMU time to approach the extruder
gears. If the firmware-side TC reports an error, the sensor gate eventually
fails rather than starting hotend feed on timeout.
Keep `gcode_shell_command flare.timeout` high enough for blocking helper calls
such as `FL:`, `UL:`, and `CU:`. `TC:` and `MV:` return after command
acceptance; tune firmware travel/timeouts (`LOAD_MAX`, `UNLOAD_MAX`,
`TC_TH_MS`, `TC_Y_MS`) for the actual toolchange path.

Copy `klipper/flare_mmu.cfg` into your Klipper config directory and include it:

```ini
[include flare_mmu.cfg]
```

The include provides `_FLARE_VARS`, `_FLARE_TIP_FORMING_DEFAULTS`,
`_FLARE_TIP_FORMING`, `_FLARE_LOAD_HOTEND`, `_FLARE_HEAT_HOTEND`,
`_FLARE_PARK`, `_FLARE_PURGE`,
`_FLARE_TC_STATE`, `_FLARE_ARM_TC_LOAD`, `_FLARE_ON_TOOLHEAD_RUNOUT`,
`_FLARE_ON_TOOLHEAD_INSERT`, `_FLARE_CHANGE_LANE`, `_FLARE_POST_TC_LOAD`,
`_FLARE_TC_FAILED`, `T1`, `T2`, `FLARE_LOAD`, `FLARE_UNLOAD`,
`FLARE_UNLOAD_TOOLHEAD`, `FLARE_CUT`,
`FLARE_TEST_TIP_FORMING`, and the boot-time `_FLARE_BOOT` delayed gcode that
applies `RELOAD_MODE` from `_FLARE_VARS`.

Tune the distances and temperatures in `_FLARE_VARS`. In particular,
`dist_filament_park` (defaulting to `11` mm to park filament closer to the gears than the hotend) must stay less than `dist_extruder_to_meltzone`, because the hotend load distance is derived from their difference. 
`dist_sensor_to_synced_move` (default `8.0` mm) is the distance from the filament sensor towards the extruder gears. When loading, the MMU drives the filament alone past the sensor by this distance using the ignore-buffer command:
`MV:<dist_sensor_to_synced_move>:<speed_hub_to_extruder * 60>:I`. 
Then, a synchronized G1 E move is performed by Klipper to grab and park the filament:
`G1 E{load_park_dist} F{v.purge_speed * 60}`, where `load_park_dist` is derived as `(dist_filament_park + dist_sensor_to_synced_move) * 1.1` (with a 10% buffer for slippage). Because sync mode is enabled, the MMU automatically follows this G1 E move, completing a seamless handoff.

The [LH-Stinger Pico MMU toolhead distance calibration guide](https://github.com/lhndo/LH-Stinger/wiki/Pico-MMU#toolhead-distance-calibration) uses the same distance variable names, so you can copy those measurements into `_FLARE_VARS`. The same link is also included as a comment at the top of `klipper/flare_mmu.cfg` for printer-side tuning.

`FLARE_TEST_TIP_FORMING` follows the LH-Stinger `SP_TEST_MANUAL_TIP_FORMING` flow: it accepts tip-forming override parameters, loads the hotend, simulates a print pause, runs `FLARE_UNLOAD_TOOLHEAD` (which forms the tip via `_FLARE_TIP_FORMING` and retracts past the extruder gears), then retracts the filament further for manual inspection and removal.

Tip forming starts a finite FLARE reverse move before the final park retract:
`MV:-<derived distance>:<slow feed>:I`. `flare_cmd.py` returns after firmware accepts `MV:`, so the printer-side retract can run concurrently with the MMU move. The `I` option tells firmware to ignore buffer state during that exact move, so it can pull the old lane clear of the toolhead gears/bowden exit without reacting to temporary compression or tension. The distance is derived from `_FLARE_VARS` as
`dist_sensor_to_extruder + dist_extruder_to_meltzone +
dist_meltzone_to_nozzle_tip`. Tune `dist_meltzone_to_nozzle_tip` to your hotend geometry; it is 46 mm in the shared example. The later printer-side gear retract is not mirrored by another FLARE `MV:` and runs at
`speed_hub_to_extruder * 60` (`50 mm/s` by default, matching LH-Stinger).

> **Temperature management:** `gcode_shell_command` holds the Klipper scheduler
> while the shell process runs — heaters stay regulated, but no additional G-code
> is processed until the command returns. Keep `LOAD_MAX` / `UNLOAD_MAX`
> conservative enough that a jam cannot hold Klipper indefinitely, and tune
> `TC_TH_MS` / `TC_Y_MS` only for the host-facing wait phases.

`TC:` returns after firmware accepts the command, then the delayed Klipper
toolhead-sensor gate owns post-TC hotend loading. Firmware-side TC errors are
logged as `EV:TC:ERROR:*`; `_FLARE_TC_FAILED` reports sensor-gate timeout if
the new filament never reaches the configured toolhead sensor.

### Purge

`_FLARE_PURGE` follows the simple LH-Stinger `_SP_PURGE` shape: it takes a
`PURGE` amount, extrudes that amount at `purge_speed`, then performs a small
0.4 mm retract. It also calls `_FLARE_HEAT_HOTEND`, which ports the
LH-Stinger heat check: if the current hotend target is below
`min_extrude_temp`, it heats to `load_temp` before purging. Customize
`_FLARE_PARK` if your printer needs purge parking or toolhead-unload parking.
The shared hook is empty by default and contains only
`# Add your printer park macro here`. The shared macro does not include
purge-chute parking or brush logic.

The [LH-Stinger Mini Purge Shute](https://github.com/lhndo/LH-Stinger/tree/main/User_Mods/Other/Mini%20Purge%20Shute%20-%20%40LH)
is still a good manual reference for users who want to add their own chute
parking and brush moves around `_FLARE_PURGE`.

Keep `purge_len` and `purge_speed` in `_FLARE_VARS` as the normal post-load
flush volume and extrusion speed in mm/s. The macro multiplies by 60 for
Klipper `F` feedrates, so `purge_speed: 30.0` means 30 mm/s, not F30.
`_FLARE_LOAD_HOTEND` calls
`_FLARE_PURGE PURGE={v.purge_len}` after the three-stage meltzone approach, so
the plain purge flow is part of `T1` / `T2` toolchanges without adding slicer
commands.

---

## Manual load / unload

`UL:` / `UM:` are unload-only and never run the cutter. Use `CU:` for an
explicit cutter cycle; firmware only accepts `CU:` when both lanes are idle and
preloaded (`IN=1`, `OUT=0`). `UM:1` / `UM:2` can eject a specific inactive
standby lane only when that lane is idle and preloaded (`IN=1`, `OUT=0`);
active lane selection, sync state, and toolhead filament state are preserved.

```ini
[gcode_macro FLARE_LOAD]
description: Full load active lane to toolhead
gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="FL:"

[gcode_macro FLARE_UNLOAD]
description: Unload from extruder (tip past OUT sensor)
gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="UL:"

[gcode_macro FLARE_UNLOAD_TOOLHEAD]
description: Form filament tip and retract filament past extruder gears
gcode:
    FLARE_UNLOAD_TOOLHEAD

[gcode_macro FLARE_CUT]
description: Perform full filament cut cycle
gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="CU:"
```

---

## Sync mode

Buffer sync enables automatically when the load task reports loaded and disables
when unload starts. No explicit `SM:` calls are normally needed.

For manual override, for example before tip-shaping retraction moves:
```ini
[gcode_macro SYNC_OFF]
gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="SM:0"

[gcode_macro SYNC_ON]
gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="SM:1"
```

For sync tuning see [TUNING.md](TUNING.md).

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `flare_cmd.py` exits "no serial port found" | Port not present | `ls /dev/ttyACM*`; check `dialout` group |
| `TS:1` not reaching FLARE | Sensor wiring or config | Test: `RUN_SHELL_COMMAND CMD=flare PARAMS="TS:1"` |
| `TC:` times out | Bowden too long / jam | Increase `LOAD_MAX` / `UNLOAD_MAX` if travel is genuinely too short; otherwise tune `TC_Y_MS` or fix the path |
| Sync not enabling after load | Load task never reached a loaded condition | Check buffer travel/sensor state; optional sensor can be tested with `TS:1` |
| RELOAD approach never detects contact | Buffer sensor never reaches `COMPRESSION` | Verify buffer wiring and travel; reduce `JOIN_RATE` if the path is too aggressive |
| RELOAD approach exits too early | Buffer sensor chatter or preload already compression | Verify hysteresis/sensor state and make sure the standby path starts with real slack |
| RELOAD follow times out neutral-bowden | Drag too high or follow speed too low | Check PTFE routing; reduce `PRESS_RATE` or increase `FOLLOW_TIMEOUT_MS` |
