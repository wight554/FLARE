## Why

KLIPPER.md mixes integration reference, sync tuning, and calibration
workflows into one file — operators cannot copy a working Klipper config
in one step, and the tip-forming / load sequence is undocumented. The
goal is a single ready-to-use `klipper/flare_mmu.cfg` that users drop
into their printer config, and a slimmed KLIPPER.md that covers only
integration.

## What Changes

- **NEW** `klipper/flare_mmu.cfg` — single-include Klipper config covering:
  - `[gcode_macro _FLARE_VARS]` variables block (hardware measurements + temps + purge + reload flag)
  - `[gcode_macro _FLARE_TIP_FORMING_DEFAULTS]` tuning block (cooldown / dip / park params)
  - `[gcode_macro _FLARE_TIP_FORMING]` — full tip-forming sequence (cooldown, dip, park)
  - `[gcode_macro _FLARE_LOAD_HOTEND]` — 3-stage meltzone approach after TC:
  - `[gcode_macro _FLARE_PURGE]` — separate purge helper with optional Mini Purge Shute-style park hook and brush moves
  - `[gcode_macro _FLARE_CHANGE_LANE]` — HD:1 → tip forming → HD:0 → gear retract → TC: → pickup → load hotend
  - `[gcode_macro T1]` / `[gcode_macro T2]`
  - `[gcode_macro FLARE_LOAD]` / `[gcode_macro FLARE_UNLOAD]` / `[gcode_macro FLARE_CUT]`
  - `[gcode_macro FLARE_TEST_TIP_FORMING]` — manual tip inspection test
  - `[delayed_gcode _FLARE_BOOT]` — sets `RELOAD_MODE` from variable on every Klipper start (no SV:)
- **REMOVED** from Klipper config examples: `FLARE_PRELOAD`, `FLARE_CUT_BARE`, `FLARE_CUT_TEST`
- **REMOVED** from `KLIPPER.md`: buffer sync tuning section, calibration prints section, telemetry/tuning section, gcode_marker reference — all replaced with a pointer to `TUNING.md`
- **MODIFIED** `KLIPPER.md` toolhead sensor section: Option A kept (sensor wiring), Option B collapsed to a brief note (it is automatic, not a user choice)
- **MODIFIED** `KLIPPER.md` toolchange macros section: points to `flare_mmu.cfg` instead of inline snippets, references LH-Stinger distance calibration, and documents the Mini Purge Shute-compatible `_FLARE_PURGE` setup

## Capabilities

### New Capabilities

- `klipper-mmu-config`: Single-file Klipper MMU integration with tip forming, load sequence, purge helper, toolchange, and boot-time RELOAD_MODE configuration

### Modified Capabilities

- `klipper-integration`: KLIPPER.md scope narrows to integration-only (serial setup, sensor wiring, config reference, troubleshooting); tuning/calibration content removed to TUNING.md

## Impact

- `klipper/flare_mmu.cfg` — new file (no firmware changes)
- `KLIPPER.md` — significant restructure (docs only)
- No firmware, `config.ini`, `tune.h`, or host script changes
