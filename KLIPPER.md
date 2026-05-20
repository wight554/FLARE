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
timeout: 130.0
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
runout_gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="TS:0"
```

Without a physical sensor: set `dist_sensor_to_extruder: 0` in `_FLARE_VARS`.
FLARE detects load completion via buffer geometry (`TS_BUF_MS`, default 2000 ms;
tune to your bowden length with `SET:TS_BUF_MS:<ms>`).

---

## Toolchange macros — TC:

`TC:<lane>` unloads the current lane (cuts when `UNLOAD_CUT=1` and the cutter
is enabled), swaps, loads the new lane, and completes when the lane load task
reports loaded (`TS:1`, `TS_BUF_MS`, or sane buffer geometry).
`flare_cmd.py` blocks until `EV:TC:DONE` or
`EV:TC:ERROR`, so Klipper naturally pauses printing during the change.

Copy `scripts/flare_mmu.cfg` into your Klipper config directory and include it:

```ini
[include flare_mmu.cfg]
```

The include provides `_FLARE_VARS`, `_FLARE_TIP_FORMING_DEFAULTS`,
`_FLARE_TIP_FORMING`, `_FLARE_LOAD_HOTEND`, `_FLARE_CHANGE_LANE`, `T1`, `T2`,
`FLARE_LOAD`, `FLARE_UNLOAD`, `FLARE_CUT`, `FLARE_TEST_TIP_FORMING`, and the
boot-time `_FLARE_BOOT` delayed gcode that applies `RELOAD_MODE` from
`_FLARE_VARS`.

Tune the distances and temperatures in `_FLARE_VARS`. In particular,
`dist_filament_park` must stay less than `dist_extruder_to_meltzone`, because
the hotend load distance is derived from their difference.

Keep the tip-forming wiggle section inside `HD:1` / `HD:0` HOLD. Small wiggles
interact with the buffer sensing span (`BUF_SWITCH_SPAN`, default 10 mm full
range); HOLD suppresses sync and negative-sync following while leaving
basic buffer stabilization available. The final `GEAR_RETRACT` is intentionally
outside HOLD so negative sync can follow it. With that split,
`POST_PRINT_STAB_DELAY_MS=0` is acceptable because the long retract should be
followed immediately.

> **Temperature management:** `gcode_shell_command` holds the Klipper scheduler
> while the shell process runs — heaters stay regulated, but no additional G-code
> is processed until the command returns. Keep `LOAD_MAX` / `UNLOAD_MAX`
> conservative enough that a jam cannot hold Klipper indefinitely, and tune
> `TC_TH_MS` / `TC_Y_MS` only for the host-facing wait phases.

If `TC:` returns an error, `flare_cmd.py` exits with code 1.
`gcode_shell_command` logs the failure; add printer-specific pause/error
handling around your slicer flow if you want automatic intervention.

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
