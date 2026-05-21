# FLARE

FLARE (Filament Lane Automation and Reload Engine) is standalone firmware for
dual-lane filament control on ERB v2.0 (RP2040). It can run without a host
plugin and handles lane switching, buffer-driven feed, and TMC2209 configuration
and diagnostics over USB serial.

## Naming

**FLARE** names the firmware control layer, not the whole physical MMU module.
The hardware may be a NightOwl-style dual-lane setup, a QuattroSync-buffered
reload system, or another compatible ERB v2.0 build; FLARE is the logic that
drives lanes, watches sensors, coordinates swaps, and keeps buffer motion
bounded.

The acronym is:

```text
Filament Lane Automation and Reload Engine
```

- **Filament**: the controlled medium and safety boundary.
- **Lane**: lane selection, preload, unload, and toolchange handoff.
- **Automation**: sensor-triggered preload, sync start/stop, cutter sequencing,
  guarded moves, and command completion events.
- **Reload**: autonomous runout/failover behavior where a standby lane can join
  and follow a disconnected tail.
- **Engine**: firmware and host tooling, not a mechanical product name.

The project is still in active development, so the FLARE namespace may make
breaking changes. Scripts, marker names, sidecar suffixes, state directories,
and Klipper examples use FLARE names directly instead of preserving pre-rebrand
aliases.

## Tested Configuration
- **Motors**: [FYSETC G36HSY4405-6D-1200](https://github.com/FYSETC/FYSETC-MOTORS/blob/main/G36HSY4405-6D-1200/G36HSY4405-6D-1200.pdf) (included in Fysetc NightOwl kits)
- **Buffer**: [QuattroSync](https://github.com/Batalhoti/QuattroSync) (spring-managed dual-lane buffer with more consistent RELOAD behavior than gravity-based TurtleNeck-style designs)

## What Is In This Repo

- `firmware/`: RP2040 firmware (C, Pico SDK)
- `scripts/`: serial/config/flash helpers
- `config.ini`: user tuning source for motor and TMC defaults
- `config.ini.example`: template for new setups
- `MOTOR_PARAMS.md`: known motor/TMC parameter profiles

## Why FLARE Exists

[Happy Hare](https://github.com/moggieuk/Happy-Hare/) is the mature,
host-integrated path for Klipper MMUs. It is implemented as a Klipper
extension, a Moonraker component, and a set of macros and configuration files.
It supports a wide range of MMU designs, including ERCF, Tradrack, Box Turtle,
Night Owl, QuattroBox, 3MS, KMS, ViViD, and custom machines. It also provides
runtime gate/tool mapping, Spoolman integration, native Mainsail/Fluidd MMU
panels, and KlipperScreen support.

FLARE was developed for a narrower goal: allow a dual-lane ERB v2.0 controller
to own the reload/MMU state machine in firmware. Klipper can still command the
controller, but the normal integration surface is a small USB serial protocol
(`T:`, `TC:`, `LO:`, `UL:`, `UM:1`, `SET:`, `GET:`, `?:`). This keeps host
integration simple and avoids routing every lane decision through Klipper macros
or API callbacks.

The main reason to use FLARE instead of Happy Hare is autonomous RELOAD. Two
spools can be treated as a redundant paired supply, and the controller can
switch to the standby lane on runout without a Klipper plugin or host-side
recovery flow. This is useful when the priority is continuous feed and runout
redundancy rather than a universal multi-material ecosystem.

FLARE is also a strong open-source alternative to commercial automatic
filament reloaders such as [Infinity Flow S1 Plus](https://infinityflow3d.com/).
The practical value is similar: keep printing when one spool runs out by
feeding from another spool. FLARE takes the DIY route instead of the appliance
route, so the firmware is open source, the controller logic is inspectable and
tunable, and the hardware target uses inexpensive, off-the-shelf parts. For
builders who are comfortable assembling and tuning the mechanism themselves,
the total system can be less expensive than a closed commercial reloader while
remaining more flexible.

The extra benefit is that FLARE is not only a reloader. Because the runtime
interface is plain USB serial, the same controller can also behave as a minimal
two-lane MMU. A host can select lanes, preload, unload, or request a toolchange
with a few serial commands instead of relying on a proprietary app, cloud
service, or a full Klipper MMU plugin.

The tradeoff is scope. Happy Hare has a richer UI and recovery workflow,
broader MMU and board support, Spoolman/gate mapping, and stronger host
context. Its sync logic can observe Klipper extruder movement directly. FLARE
does not know the printer's exact commanded extruder baseline; it estimates
motion from buffer behavior and compensates with firmware-side control. That
keeps the design standalone, but it makes FLARE less flexible and currently
limits the supported configuration to two lanes on the tested RP2040 board.

Compared with a commercial reloader, FLARE also requires more operator effort:
the mechanical assembly, wiring, firmware flashing, serial setup, and runtime
configuration are part of the project rather than a finished appliance.

A future web UI is possible, but serial ownership needs care. A second process
reading the FLARE serial port would block Klipper's helper, so a UI would
likely need to act as the serial mediator, with Klipper sending commands through
that mediator instead of opening the port independently.

## Quick Start

1. Copy and edit config:

```bash
cp config.ini.example config.ini
```

2. Generate compile-time tuning header from `config.ini`:

```bash
python3 scripts/gen_config.py
```

3. Build firmware:

```bash
cmake -S firmware -B build_local -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build_local
```

4. Flash firmware (auto-detect serial, trigger BOOTSEL when possible):

```bash
bash scripts/flash_flare.sh
```

## Configuration Model

`config.ini` is the source of compile-time motor/TMC defaults.
`scripts/gen_config.py` generates `firmware/include/tune.h`.

Mandatory keys:

- `microsteps`
- `rotation_distance`
- `run_current`

Typical workflow:

```bash
python3 scripts/gen_config.py
cmake --build build_local
```

Runtime changes (serial protocol `SET:/GET:/CW:/TR:`) can be saved to flash via `SV:`.

## Serial Runtime Commands

Common control commands:

- `T:<lane>`: set active lane (`1` or `2`)
- `LO:`: autoload active lane until output sensor or timeout
- `UL:`: reverse/unload active lane
- `TC:<lane>`: toolchange to target lane
- `ST:`: stop motors and abort active operations
- `?:` status snapshot (`I1/O1/I2/O2/YS`, tasks, sync state, `AP`)

Active lane behavior:

- `LN` is active lane (`1` or `2`), or `0` when unknown.
- Boot initialization uses OUT sensors: only `O1` active -> `LN:1`, only `O2` active -> `LN:2`, both/none -> `LN:0`.
- During preload/autoload, when a lane reaches OUT it becomes active automatically.
- If `LN:0`, `LO`, `UL`, `CU`, and `TC` return `ER:NO_ACTIVE_LANE` until you select with `T:1`/`T:2` or preload reaches OUT.

Runtime toggles (`SET:/GET:`):

- `SM` (`0/1`): sync mode enable
- `BI` (`0/1`): buffer sensor invert
- `AUTO_PRELOAD` (`0/1`): auto-start preload on IN sensor rising edge

Other runtime state:

- `TS:<0|1>`: host-reported toolhead filament presence

Examples:

```bash
python3 scripts/flare_cmd.py "SET:AUTO_PRELOAD:1" "GET:AUTO_PRELOAD"
python3 scripts/flare_cmd.py "SET:SM:1" "GET:SM"
python3 scripts/flare_cmd.py "SET:BI:0" "GET:BI"
```

Persist runtime values to flash:

```bash
python3 scripts/flare_cmd.py "SV:"
```

## Helper Scripts

Helper script filenames use the FLARE namespace because the firmware has not
shipped as a stable external interface yet.

- `scripts/flare_cmd.py`: Serial helper — send commands and dump live config
- `scripts/flare_analyze.py`: Offline calibration analyzer with LOCKED-bucket floor, contributor diagnostics, seven-tunable review patch, deterministic flow-schedule emission, and recommendation-parity acceptance gate
- `scripts/flare_live_tuner.py`: Observe-only calibration bucket learner; emits reviewable patches, with live writes reserved for explicit debug flags
  and residual-aware lock hysteresis so noisy buckets stay isolated instead of chattering
- `scripts/gcode_marker.py`: G-code metadata injector and sidecar generator for Klipper API motion tracking
- `scripts/gen_config.py`: Generate `tune.h` from `config.ini`
- `scripts/validate_regression.sh`: One-command static regression gate before flashing hardware

All scripts support `--port`; if omitted they auto-detect the serial device.

Examples:

```bash
# Send commands
python3 scripts/flare_cmd.py "VR:" "?:"
python3 scripts/flare_cmd.py "SET:JOIN_RATE:1600" "SV:"

# Read a full live settings snapshot as config-style key/value output
python3 scripts/flare_cmd.py --dump

# Terse key: value dump
python3 scripts/flare_cmd.py --dump --raw

# Static regression gate before hardware testing
bash scripts/validate_regression.sh
```

Calibration workflow:

```text
sidecar + Klipper UDS calibration prints -> analyze CSV/state -> review config.patch.ini
-> copy accepted values to config.ini -> regenerate/build/flash -> detach host
```

## Build Notes

- Build output directory used in this repo is `build_local/`.
- Flash script uses `picotool` if available in PATH, otherwise checks local build outputs.
- For detailed flash/build troubleshooting, see `BUILD_FLASH.md`.

## Hardware and Operation Docs

- `HARDWARE.md`: board wiring and hardware assumptions
- `MANUAL.md`: runtime behavior and operator guidance
- `TUNING.md`: end-to-end operator tuning guide
- `TEST_CASES.md`: bring-up and regression checklist for real hardware
- `WORKFLOW.md`: current Git workflow for `main` and optional short-lived branches

---

## Development

- `main`: primary branch, expected to stay buildable and flashable
- `feature/*`, `fix/*`, `hw/*`: optional short-lived branches for risky or long-running work

---

## Safety

Always test firmware changes at low speed.

Verify sensor polarity before enabling automatic swap.
