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
response arrives. Simple commands (SET:, GET:, T:, SM:, TS:, FD:, ST:,
…) return on the first `OK:`/`ER:`. Long-running commands (`TC:`, `FL:`,
`UL:`, `UM:`) wait for their completion event (`EV:TC:DONE`, `EV:LOADED`,
`EV:UNLOADED`, …) or the corresponding error/timeout event. Exit code is 0 on
success, 1 on error or timeout. All received lines are printed so Klipper's
`VERBOSE` output shows them in the Mainsail / Fluidd console.

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
    RUN_SHELL_COMMAND CMD=flare PARAMS="TS:1"
    _FLARE_ON_TOOLHEAD_INSERT
runout_gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="TS:0"
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
`flare_cmd.py` blocks until `EV:TC:DONE` or
`EV:TC:ERROR`, so Klipper naturally pauses printing during the change.
The shared `_FLARE_CHANGE_LANE` macro does not place hotend loading after
`RUN_SHELL_COMMAND` directly. Instead, it arms `_FLARE_TC_STATE`; when the
toolhead sensor reports runout, `_FLARE_ON_TOOLHEAD_RUNOUT` starts a
delayed-gcode poll. `_FLARE_POST_TC_LOAD` runs only after the same physical
toolhead sensor reports filament detected again while TC is pending. If the
shell command times out but FLARE is still loading, hotend feed waits for the
sensor rather than starting on timeout.
If a long unload/cut/load path needs more headroom, raise both the
`gcode_shell_command flare` timeout and the `--timeout` values in
`klipper/flare_mmu.cfg`.

Copy `klipper/flare_mmu.cfg` into your Klipper config directory and include it:

```ini
[include flare_mmu.cfg]
```

The include provides `_FLARE_VARS`, `_FLARE_TIP_FORMING_DEFAULTS`,
`_FLARE_TIP_FORMING`, `_FLARE_LOAD_HOTEND`, `_FLARE_PURGE`,
`_FLARE_TC_STATE`, `_FLARE_ARM_TC_LOAD`, `_FLARE_ON_TOOLHEAD_RUNOUT`,
`_FLARE_ON_TOOLHEAD_INSERT`, `_FLARE_CHANGE_LANE`, `_FLARE_POST_TC_LOAD`,
`_FLARE_TC_FAILED`, `T1`, `T2`, `FLARE_LOAD`, `FLARE_UNLOAD`, `FLARE_CUT`,
`FLARE_TEST_TIP_FORMING`, and the boot-time `_FLARE_BOOT` delayed gcode that
applies `RELOAD_MODE` from `_FLARE_VARS`.

Tune the distances and temperatures in `_FLARE_VARS`. In particular,
`dist_filament_park` must stay less than `dist_extruder_to_meltzone`, because
the hotend load distance is derived from their difference. The
[LH-Stinger Pico MMU toolhead distance calibration guide](https://github.com/lhndo/LH-Stinger/wiki/Pico-MMU#toolhead-distance-calibration)
uses the same distance variable names, so you can copy those measurements into
`_FLARE_VARS`.

Keep tip forming inside `RA:1` / `RA:0` retract-assist gate. During that gated
window FLARE suppresses normal sync and post-print negative sync and does not
react to buffer changes, so cooldown/dip/park moves stay printer-local. After
tip forming drains with `M400`, `_FLARE_CHANGE_LANE` sends `RA:0`; firmware
immediately checks whether the buffer is already compressed and may start the
existing reverse buffer service. For the explicit fast gear-clear retract,
`_FLARE_CHANGE_LANE` also starts `MV:-gear_retract:gear_retract_spd` before the
printer `G1 E-...` move so FLARE follows that known long retract. Tune
`gear_retract_spd` in `_FLARE_VARS`, and make sure `GLOBAL_MAX_RATE` is high
enough for `MV:` to reach the requested feed.

> **Temperature management:** `gcode_shell_command` holds the Klipper scheduler
> while the shell process runs — heaters stay regulated, but no additional G-code
> is processed until the command returns. Keep `LOAD_MAX` / `UNLOAD_MAX`
> conservative enough that a jam cannot hold Klipper indefinitely, and tune
> `TC_TH_MS` / `TC_Y_MS` only for the host-facing wait phases.

If `TC:` returns an error, `flare_cmd.py` exits with code 1.
`gcode_shell_command` logs the failure; add printer-specific pause/error
handling around your slicer flow if you want automatic intervention.

### Purge chute example

The [LH-Stinger Mini Purge Shute](https://github.com/lhndo/LH-Stinger/tree/main/User_Mods/Other/Mini%20Purge%20Shute%20-%20%40LH)
is a good reference for a compact passive purge chute with a brush. FLARE's
`_FLARE_PURGE` macro includes the compatible implementation: by default it
performs a plain relative extruder purge; when `use_chute` is enabled it parks
through your printer-specific hook, splits large purges into smaller blobs,
retracts between blobs, runs brush wipes, and restores fan/G-code state.

Edit these variables in `_FLARE_PURGE` after installing the chute:

```ini
variable_use_chute: 1
variable_park_x: 0.0          # nozzle center over chute/PTFE tube
variable_brush_left_x: 0.0    # left edge of brush stroke
variable_brush_speed: 150.0
variable_brush_cycles: 4
variable_brush_pause_ms: 1000
variable_min_purge: 30.0       # chute mode only
variable_max_blob_size: 80.0   # chute mode only
variable_fan_speed: -1        # -1 keeps current fan speed; 0-255 overrides
variable_retract: 0.4
```

Inside `_FLARE_PURGE`, replace the commented `# _FLARE_PURGE_PARK` hook with
your own safe park macro or explicit Z/XY moves. The shared example only moves
X between `park_x` and `brush_left_x` because Y/Z parking is printer-specific.

Keep `purge_len` and `purge_spd` in `_FLARE_VARS` as the normal post-load flush
volume and extrusion feedrate. `_FLARE_LOAD_HOTEND` calls
`_FLARE_PURGE PURGE={v.purge_len}` after the three-stage meltzone approach, so
the purge chute flow is part of `T1` / `T2` toolchanges without adding slicer
commands.

---

## Manual load / unload

```ini
[gcode_macro FLARE_LOAD]
description: Full load active lane to toolhead
gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="FL:"

[gcode_macro FLARE_UNLOAD]
description: Unload from extruder (tip past OUT sensor)
gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="UL:"

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
