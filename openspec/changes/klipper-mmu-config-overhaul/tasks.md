## 1. flare_mmu.cfg — variables and boot

- [x] 1.1 Create `scripts/flare_mmu.cfg`. Write `[gcode_macro _FLARE_VARS]` with all variables: `dist_sensor_to_extruder` (20.0), `dist_filament_park` (35.0), `dist_extruder_to_meltzone` (40.0), `tip_length_below_cut` (3.0), `load_temp` (230), `unload_temp` (185), `min_extrude_temp` (180), `purge_len` (30.0), `purge_spd` (450.0), `enable_reload` (1). Include comment that `dist_filament_park` MUST be < `dist_extruder_to_meltzone`.
- [x] 1.2 Write `[gcode_macro _FLARE_TIP_FORMING_DEFAULTS]` with: `pause_push_dist` (0.5), `pause_push_speed` (30.0), `cooldown_dist` (5.0), `cooldown_pull_speed` (70.0), `cooldown_pause` (0.0), `cooldown_secondary_moves` (0), `dip_melt_gap` (0.0), `dip_speed` (30.0), `dip_pause` (0.0), `park_speed` (130.0).
- [x] 1.3 Write `[delayed_gcode _FLARE_BOOT]` with `initial_duration: 2`. Body reads `enable_reload` from `_FLARE_VARS`, sends `SET:RELOAD_MODE:{v.enable_reload}` (no `SV:`), logs result via `RESPOND`.

## 2. flare_mmu.cfg — tip forming macro

- [x] 2.1 Write `[gcode_macro _FLARE_TIP_FORMING]` following the `_SP_TIP_FORMING` pattern. Read `_FLARE_VARS` and `_FLARE_TIP_FORMING_DEFAULTS`. Compute `park_distance = dist_extruder_to_meltzone - dist_filament_park - tip_length_below_cut`. Implement: post-pause push → cooldown pull + pause + optional secondary moves → optional dip + crawl → final fast retract to park. Mirror the SP example the user provided exactly, substituting `_FLARE_VARS` / `_FLARE_TIP_FORMING_DEFAULTS` for `_SP_VARS` / `_SP_TIP_FORMING_DEFAULTS`.

## 3. flare_mmu.cfg — load hotend macro

- [x] 3.1 Write `[gcode_macro _FLARE_LOAD_HOTEND]`. Read `_FLARE_VARS`. Compute push distance = `dist_extruder_to_meltzone - dist_filament_park - tip_length_below_cut`. Execute 3-stage approach: 50% at `purge_spd * 2`, 25% at `purge_spd`, 25% at `purge_spd * 0.5`. If `purge_len > 0` extrude `purge_len` at `purge_spd`.

## 4. flare_mmu.cfg — toolchange macros

- [x] 4.1 Write `[gcode_macro _FLARE_CHANGE_LANE]`. Read `_FLARE_VARS`. Compute `gear_retract = dist_filament_park + dist_sensor_to_extruder + 5`. Sequence: `M400` → `SAVE_GCODE_STATE` → `M83` → `HD:1` → `_FLARE_TIP_FORMING` → `HD:0` → `G1 E-{gear_retract} F7800` → `TC:{LANE}` → `G1 E{dist_sensor_to_extruder * 1.2} F{purge_spd}` → `_FLARE_LOAD_HOTEND` → `RESTORE_GCODE_STATE`.
- [x] 4.2 Write `[gcode_macro T1]` calling `_FLARE_CHANGE_LANE LANE=1` and `[gcode_macro T2]` calling `_FLARE_CHANGE_LANE LANE=2`.
- [x] 4.3 Write `[gcode_macro FLARE_LOAD]`, `[gcode_macro FLARE_UNLOAD]`, `[gcode_macro FLARE_CUT]` wrapping `FL:`, `UL:`, `CU:` respectively. Do NOT include `FLARE_PRELOAD`, `FLARE_CUT_BARE`, or `FLARE_CUT_TEST`.

## 5. flare_mmu.cfg — test macro

- [x] 5.1 Write `[gcode_macro FLARE_TEST_TIP_FORMING]`. Guard on `can_extrude`. Read `_FLARE_VARS` and `_FLARE_TIP_FORMING_DEFAULTS`. Sequence: `M83` / `G92 E0` → load hotend (push `dist_extruder_to_meltzone * 1.2` in 2 stages) → simulate print (7 mm at 4 mm/s + G4 P2000 + `G1 E-{pause_push_dist}`) → `_FLARE_TIP_FORMING` → retract `dist_filament_park + dist_sensor_to_extruder + 5` to clear gears → `RESPOND MSG="FLARE: inspect tip"`. No `FORCE_MOVE` or MMU motor calls.

## 6. KLIPPER.md overhaul

- [x] 6.1 Remove the "Buffer sync tuning" section (BASELINE_RATE, SYNC_KP_RATE, BUF_ALPHA content) entirely. Replace with a single line: `For sync tuning see [TUNING.md](TUNING.md).`
- [x] 6.2 Remove the "Telemetry and Tuning — flare_live_tuner.py" section entirely.
- [x] 6.3 Remove the "Calibration Prints" section (gcode_marker, sidecar, accept-gate, flow-schedule workflow) entirely.
- [x] 6.4 Collapse toolhead sensor "Option B" into a brief note inside the Option A section: *"Without a physical sensor: set `dist_sensor_to_extruder: 0` in `_FLARE_VARS`. FLARE detects load completion via buffer geometry (`TS_BUF_MS`, default 2000 ms; tune to your bowden length with `SET:TS_BUF_MS:<ms>`)."* Remove the Option B heading and code block.
- [x] 6.5 Replace the inline macro snippets in the "Toolchange macros" section with a reference to `flare_mmu.cfg`: explain the include line, list what macros are provided, keep the HD:1/HD:0 rationale paragraph and temperature warning. Remove the standalone `T1`/`T2` inline examples.
- [x] 6.6 Remove `FLARE_PRELOAD`, `FLARE_CUT_BARE`, `FLARE_CUT_TEST` from the "Manual load / unload" section.

## 7. Validation and commit

- [x] 7.1 `python3 -m py_compile scripts/flare_mmu.cfg 2>/dev/null || true` — Klipper cfg is not Python; instead manually review `flare_mmu.cfg` for Jinja2 syntax: all `{% %}` blocks balanced, all variable names consistent with `_FLARE_VARS` / `_FLARE_TIP_FORMING_DEFAULTS`.
- [x] 7.2 Verify `python3 -m py_compile scripts/*.py` still green (no scripts changed, but confirm).
- [x] 7.3 Confirm `ninja -C build_local` still green (no firmware changed).
- [ ] 7.4 Commit and push.

2026-05-20 validation:
- `scripts/flare_mmu.cfg` Jinja scan: `{%` count 45, `%}` count 45; `_FLARE_VARS` and `_FLARE_TIP_FORMING_DEFAULTS` references present; `FLARE_PRELOAD`, `FLARE_CUT_BARE`, `FLARE_CUT_TEST`, and `SV:` absent.
- `python3 -m py_compile scripts/*.py` passed.
- `ninja -C build_local` passed (`ninja: no work to do.`).
