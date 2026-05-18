# FLARE — Klipper Integration

This document covers connecting Klipper to FLARE: serial setup, the shell
command helper, Klipper API motion tracking for calibration, toolhead sensor
and toolchange macros, and buffer/sync tuning.

For the FLARE command reference see `MANUAL.md`; for behavioral details see
`BEHAVIOR.md`.

FLARE is the firmware/tooling namespace. The examples below intentionally use
`flare` command names, `FLARE_TUNE` markers, and `.flare` sidecars because the
project has not yet shipped a stable external API.

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

### Option B — Buffer fallback / geometry-only load

When filament presses against the extruder gears, the buffer arm holds COMPRESSION
for `TS_BUF_MS` milliseconds and FLARE self-triggers the loaded state.
Tune to your bowden length:

```
SET:TS_BUF_MS:2000    ; default 2000 ms
SV:
```

Even with `TS_BUF_MS=0`, `TC:` can complete from the distance-checked buffer
TENSION/COMPRESSION geometry path. Unload commands clear toolhead state
internally, so change macros do not need `TS:0` or `SM:` commands.

---

## Toolchange macros — TC:

`TC:<lane>` unloads the current lane (cuts when `UNLOAD_CUT=1` and the cutter
is enabled), swaps, loads the new lane, and completes when the lane load task
reports loaded (`TS:1`, `TS_BUF_MS`, or sane buffer geometry).
`flare_cmd.py` blocks until `EV:TC:DONE` or
`EV:TC:ERROR`, so Klipper naturally pauses printing during the change.

```ini
[gcode_macro _FLARE_CHANGE_LANE]
gcode:
    {% set LANE = params.LANE|int %}
    {% set TIP_RETRACT = params.TIP_RETRACT|default(1.0)|float %}
    {% set TIP_PUSH = params.TIP_PUSH|default(0.5)|float %}
    {% set GEAR_RETRACT = params.GEAR_RETRACT|default(30.0)|float %}
    {% set PICKUP = params.PICKUP|default(20.0)|float %}
    M400
    SAVE_GCODE_STATE NAME=_flare_change_state
    M83
    RUN_SHELL_COMMAND CMD=flare PARAMS="HD:1"
    G1 E-{TIP_RETRACT} F1800
    G1 E{TIP_PUSH} F900
    RUN_SHELL_COMMAND CMD=flare PARAMS="HD:0"
    G1 E-{GEAR_RETRACT} F7800
    RUN_SHELL_COMMAND CMD=flare PARAMS="TC:{LANE}"
    G1 E{PICKUP} F900
    RESTORE_GCODE_STATE NAME=_flare_change_state

[gcode_macro T1]
gcode:
    _FLARE_CHANGE_LANE LANE=1

[gcode_macro T2]
gcode:
    _FLARE_CHANGE_LANE LANE=2
```

Keep the tip-forming wiggle section inside `HD:1` / `HD:0` HOLD. Small wiggles
interact with the buffer travel (`BUF_HALF_TRAVEL`, measured 7.8 mm on the
reference build); HOLD suppresses sync and negative-sync following while leaving
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
`gcode_shell_command` logs the failure; add a PAUSE if you want automatic
handling:

```ini
[gcode_macro T1]
gcode:
    M400
    SAVE_GCODE_STATE NAME=_tc_state
    RUN_SHELL_COMMAND CMD=flare PARAMS="TC:1"
    {% if printer['gcode_shell_command flare'].return_code != 0 %}
        PAUSE
        { action_respond_info("FLARE TC:1 failed") }
    {% endif %}
    RESTORE_GCODE_STATE NAME=_tc_state
```

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

[gcode_macro FLARE_PRELOAD]
description: Pre-load active lane to parked position (OUT sensor)
gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="LO:"

[gcode_macro FLARE_CUT]
description: Perform full filament cut cycle
gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="CU:"

[gcode_macro FLARE_CUT_BARE]
description: Perform cutter servo cycle without filament movement
gcode:
    RUN_SHELL_COMMAND CMD=flare PARAMS="CX:"

[gcode_macro FLARE_CUT_TEST]
description: Set cutter servo to a static pulse width (tuning)
gcode:
    {% set US = params.US|default(950)|int %}
    RUN_SHELL_COMMAND CMD=flare PARAMS="CP:{US}"
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

---

## Buffer sync tuning

### BASELINE_RATE and SYNC_KP_RATE

The current sync controller is estimator-driven. `BASELINE_RATE` seeds and
stabilizes the controller around your expected steady-state print speed, while
`SYNC_KP_RATE` adds bounded correction when the buffer keeps leaning away from
NEUTRAL.

Monitor buffer state during a print:
```bash
# EV:BS lines print every 500 ms: zone, sync speed, normalised arm position
python3 scripts/flare_cmd.py "?:"
```

A healthy steady-state print:
```
EV:BS:NEUTRAL,2100.0,0.01
EV:BS:NEUTRAL,2100.0,-0.02
EV:BS:TENSION,2500.0,0.43    ← extruder accelerating
EV:BS:NEUTRAL,2250.0,0.11        ← settling back
```

**If the arm stays at TENSION during steady extrusion:** first raise `BASELINE_RATE`, then increase `SYNC_KP_RATE` only if recovery is still too weak:
```bash
python3 scripts/flare_cmd.py "SET:BASELINE_RATE:2300" "SET:SYNC_KP_RATE:1200"
```

**If speed oscillates NEUTRAL ↔ TENSION ↔ NEUTRAL rapidly:** decrease `SYNC_KP_RATE` or lower `BASELINE_RATE` if the whole controller is biased too fast.

Target: NEUTRAL during steady extrusion, with brief TENSION / COMPRESSION excursions
only on real flow changes.

### BUF_ALPHA — EMA weight for arm position

`BUF_ALPHA` (default 0.20) controls how quickly `g_buf_pos` ramps to the new
zone value (endstop sensors only).

| BUF_ALPHA | Time to 86 % correction from NEUTRAL | Character |
|-----------|-----------------------------------|-----------|
| 0.10      | ~400 ms                           | Smooth, slow |
| 0.20      | ~200 ms                           | Default — balanced |
| 0.40      | ~100 ms                           | Fast; some overshoot risk |

Increase if TENSION correction builds too slowly. Decrease if motor speed
oscillates.

---

## Telemetry and Tuning — flare_live_tuner.py

FLARE supports high-speed diagnostic capture. Use `scripts/flare_live_tuner.py --csv-out` to
stream internal state to a CSV file for offline analysis.

1. Start the tuner on the Pi:
   ```bash
   python3 scripts/flare_live_tuner.py --port /dev/ttyACM0 --csv-out run1.csv --observe-daemon
   ```
2. Run your print.
3. Stop the tuner (Ctrl+C) or leave it running across prints.
4. Analyze the results with `scripts/flare_analyze.py`:
   ```bash
   python3 scripts/flare_analyze.py --in run1.csv --out patch.ini
   ```

## Calibration Prints

FLARE uses calibration prints to bake standalone defaults without pausing
Klipper on every feature marker. The normal flow is observe-only: generate a
sidecar, gather telemetry through the Klipper API socket, run the analyzer,
review a patch, merge chosen values into `config.ini`, rebuild, flash, then
detach the host.

Confirm the Klipper API socket path on the Pi:

```bash
ps -ef | grep '[k]lippy.py'
```

Use the `-a` argument from that command. Common modern installs use
`/home/pi/printer_data/comms/klippy.sock`; older examples may show
`/tmp/klippy_uds`.

Generate a sidecar next to the calibration G-code:

```bash
python3 scripts/gcode_marker.py input.gcode --output input.flare.gcode \
    --emit sidecar
```

By default, layer changes are recognized (both `;LAYER:<n>` and OrcaSlicer
`;LAYER_CHANGE` comments). Use `--no-layer-markers` to disable.

Upload/print the generated `input.flare.gcode`, and run the observe-only tuner:

```bash
python3 scripts/flare_live_tuner.py --port /dev/ttyACM0 \
    --machine-id myprinter \
    --observe-daemon \
    --csv-out ~/flare-runs/run1.csv \
    --klipper-uds /home/pi/printer_data/comms/klippy.sock \
    --sidecar /home/pi/printer_data/gcodes/input.flare.json &
```

`--klipper-mode auto` is the default: the tuner tries the Klipper UDS first and
fails if it is unavailable. Use `--klipper-mode on` when
you want a missing socket to fail fast.

The sidecar stores the source G-code SHA-256. If the G-code is re-sliced or
edited without regenerating the sidecar, the tuner refuses to attach it and
prints a loud warning.

In observe mode the tuner persists its tracking state but sends no `SET:`
commands and no `SV:`.

### Interpreting Noisy Buckets

FLARE keeps noisy buckets in `STABLE` based on relative noise, not an
absolute scatter limit. In `--state-info`, `wait=noise sigma/x=...` means the
bucket has enough samples but the residual scatter is still too high relative to
its learned flow. Use `--state-info --verbose` to see `sigma2`, outlier
`streak`, lock `dwell`, and `last_unlock` reason. If an entire feature family
sits at `wait=noise`, check filament path load, buffer motion, and whether the
model is mixing very different geometry into the same speed bin before relaxing
thresholds.

### Why Analyzer Refuses To Emit

`flare_analyze.py --mode safe` refuses learned values when the state file has
zero `LOCKED` buckets because pre-lock bucket centroids can move hundreds of
steps/s between runs. `--mode aggressive` writes a warning banner and LOW
confidence estimates for bootstrap review. `--force` bypasses the floor only
when you explicitly accept pre-lock estimates. Read `[flare_contributors]` in
the patch to see which buckets carried each tunable: high `w` means high
precision weight, and `[marginal]` means the bucket is noisier than the normal
ratio gate.

### Why the Acceptance Gate Skipped a Run

The acceptance gate differentiates between hardware/math failures (**FAIL**) and
stale-configuration warnings (**WARN**). It compares the state-aware 
recommendation path once per "comparable" run. A run is comparable only if it 
contains at least 50 NEUTRAL rows for at least three contributing buckets. 

- **FAIL (Recommendation Unreliable)**: Triggered by high scatter 
  (sigma_p95 >= 5.0 mm), inconsistent recommendations between runs, 
  very low contributor mass (< 40% after ignoring sparse buckets), or having
  fewer than 2 comparable runs.
- **WARN (Config Stale / Immature)**: Triggered by actual scatter exceeding 
  the current config reference, contributor mass below 65%, low run counts
  (< 3), short print durations (< 30 min total or any run < 10 min), or
  having fewer than 3 LOCKED buckets.

A skipped run is not a failure by itself, but two comparable runs are required before baseline/bias consistency can be judged.

Recommended analyzer pass after three or more runs:

```bash
python3 scripts/flare_analyze.py \
    --in ~/flare-runs/run1.csv ~/flare-runs/run2.csv ~/flare-runs/run3.csv \
    --state ~/flare-state/buckets-myprinter.json \
    --out config.patch.ini \
    --acceptance-gate
```

For the deterministic flow-keyed schedule, capture a slow profile and a fast
profile from the same calibrated setup, then emit the table that belongs in
`config.ini`:

```bash
python3 scripts/flare_analyze.py \
    --profile-fast ~/flare-runs/fast.csv \
    --profile-slow ~/flare-runs/slow.csv \
    --emit-flow-schedule \
    --out flow-schedule.ini
```

Review and copy the `flow_schedule_cap` plus `[flow_schedule.v1]` block into
`config.ini`. If the input is too sparse, the analyzer emits a one-point
schedule equivalent to the scalar `baseline_rate` / `sync_compression_bias_frac`
fallback. The older scalar two-profile output remains available with
`--emit-baseline`.

If the patch is applied to `config.ini` and flashed, update the watermark:
```bash
python3 scripts/flare_analyze.py --commit-watermark --state ~/flare-state/buckets-myprinter.json
```

`flare_live_tuner.py` owns the FLARE USB TTY. Do not run
multiple instances against the same `/dev/ttyACM*` at the same time.

Debug-only live writes still exist for controlled experiments:
`--allow-bias-writes` and `--allow-baseline-writes`.

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
