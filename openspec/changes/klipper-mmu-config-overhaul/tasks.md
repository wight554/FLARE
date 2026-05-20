## 1. flare_mmu.cfg — variables and boot

- [x] 1.1 Create `klipper/flare_mmu.cfg`. Write `[gcode_macro _FLARE_VARS]` with all variables: `dist_sensor_to_extruder` (20.0), `dist_filament_park` (35.0), `dist_extruder_to_meltzone` (40.0), `tip_length_below_cut` (3.0), `load_temp` (230), `unload_temp` (185), `min_extrude_temp` (180), `purge_len` (30.0), `purge_spd` (450.0), `enable_reload` (1). Include comment that `dist_filament_park` MUST be < `dist_extruder_to_meltzone`.
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

- [x] 7.1 `python3 -m py_compile klipper/flare_mmu.cfg 2>/dev/null || true` — Klipper cfg is not Python; instead manually review `flare_mmu.cfg` for Jinja2 syntax: all `{% %}` blocks balanced, all variable names consistent with `_FLARE_VARS` / `_FLARE_TIP_FORMING_DEFAULTS`.
- [x] 7.2 Verify `python3 -m py_compile scripts/*.py` still green (no scripts changed, but confirm).
- [x] 7.3 Confirm `ninja -C build_local` still green (no firmware changed).
- [x] 7.4 Commit and push.

2026-05-20 validation:
- `klipper/flare_mmu.cfg` Jinja scan: `{%` count 45, `%}` count 45; `_FLARE_VARS` and `_FLARE_TIP_FORMING_DEFAULTS` references present; `FLARE_PRELOAD`, `FLARE_CUT_BARE`, `FLARE_CUT_TEST`, and `SV:` absent.
- `python3 -m py_compile scripts/*.py` passed.
- `ninja -C build_local` passed (`ninja: no work to do.`).
- Implementation commit pushed: `cd0f07b` (`klipper: add single-include mmu macros`).

2026-05-20 follow-up:
- Moved `flare_mmu.cfg` from `scripts/` to `klipper/` because it is a Klipper
  config include, not a host script.
- Refreshed `KLIPPER.md` and OpenSpec artifact references to
  `klipper/flare_mmu.cfg`.
- Revalidated Jinja brace balance, `python3 -m py_compile scripts/*.py`, and
  `ninja -C build_local`.

2026-05-20 purge follow-up:
- Split purge extrusion into `_FLARE_PURGE`; `_FLARE_LOAD_HOTEND` now calls
  `_FLARE_PURGE PURGE={v.purge_len}` after the meltzone approach.
- Added Mini Purge Shute-compatible optional park hook and brush logic to
  `_FLARE_PURGE`.
- Added the LH-Stinger toolhead distance calibration link to `KLIPPER.md` and
  documented the `_FLARE_PURGE` tuning variables.
- Revalidated `klipper/flare_mmu.cfg` Jinja brace balance (`73/73`),
  `python3 -m py_compile scripts/*.py`, and `ninja -C build_local`.

2026-05-20 chute park hook follow-up:
- Replaced generic Y/Z chute parking variables with comments telling operators
  to include their own purge park macro or explicit safe moves.
- Kept shared implementation to purge/blob/brush X-sweep logic only.
- Revalidated `klipper/flare_mmu.cfg` Jinja brace balance (`71/71`),
  `python3 -m py_compile scripts/*.py`, and `ninja -C build_local`.

2026-05-20 TC unload log follow-up:
- Added `M400` after `_FLARE_TIP_FORMING` before `HD:0` so all tip-forming
  moves finish inside HOLD.
- Added `M400` after `G1 E-{gear_retract}` before `TC:{lane}` so firmware
  unload does not begin until Klipper has physically retracted filament from
  the extruder gears.

2026-05-20 firmware TC clear follow-up:
- Fixed `tc_start()` so `TC:` no longer clears `toolhead_has_filament` at the
  start of unload. `TC_UNLOAD_WAIT_TH` can now wait for a real `TS:0` event
  from the toolhead sensor, or for `TC_TH_MS` timeout, before target lane load.

2026-05-20 firmware TC ordering correction:
- Reordered `TC_UNLOAD_WAIT_TH` so `TS:0` means old filament has left the
  printer toolhead and FLARE may start the old-lane unload. Target-lane loading
  now remains gated by old-lane OUT clear, optional cut, post-cut clear, and
  Y-splitter clear.

2026-05-20 cutter timeout correction:
- Made `TC_CUT_MS` an outer watchdog floor that expands to fit configured
  cutter feed distance, repeat count, servo settles, and slack.
- Added cutter failure tracking so toolchange reports cutter-side failures
  instead of treating an idle cutter as a successful cut.

2026-05-20 Klipper command wait correction:
- Raised shared Klipper/helper long-command timeout guidance to 300s and made
  `flare_mmu.cfg` pass `--timeout 300` for long FLARE commands.
- Added completion-event waits for `FL:`, `UL:`, and `UM:` in
  `scripts/flare_cmd.py` to match documented behavior.
- Added explicit `_FLARE_CHANGE_LANE` log markers around TC completion and
  hotend loading so console logs show the boundary clearly.

2026-05-20 safe post-TC load correction:
- Removed direct hotend loading after `RUN_SHELL_COMMAND` in `_FLARE_CHANGE_LANE`
  because KIAUH `gcode_shell_command` does not abort the macro on subprocess
  non-zero exit/timeout.
- Added `_FLARE_TC_STATE`, `_FLARE_ARM_TC_LOAD`,
  `_FLARE_ON_TOOLHEAD_RUNOUT`, `_FLARE_ON_TOOLHEAD_INSERT`, and delayed
  `_FLARE_TC_POLL` so Klipper polls the physical toolhead sensor until
  `filament_detected` is true again.
- Added `_FLARE_POST_TC_LOAD` for local extruder feed/purge and
  `_FLARE_TC_FAILED` for sensor-gate timeout.

2026-05-20 fast retract assist:
- Added `_FLARE_VARS` settings for gear retract speed and retract assist.
- `_FLARE_CHANGE_LANE` now starts an asynchronous FLARE `MV:-distance:feed`
  reverse move before Klipper's fast gear-clear retract, so the old lane follows
  the printer-side retract proactively instead of waiting for buffer compression.

2026-05-20 retract assist mode correction:
- Replaced the one-shot `MV:` retract helper with firmware `RA:<0|1>` retract
  assist mode.
- Removed `HD:<0|1>` / `GET:HOLD` compatibility because FLARE is still in
  active development.
- `RA:1` now owns a fast `TASK_RETRACT_ASSIST` response: `BUF_COMPRESSION`
  reverses the active lane at `GLOBAL_MAX_RATE`, `BUF_TENSION` feeds forward
  at `REV_RATE`, and `BUF_NEUTRAL` stops only the assist task.
- `_FLARE_CHANGE_LANE` keeps `RA:1` active across both the tip-forming park
  retract and the post-tip gear-clear retract, then sends `RA:0` before `TC:`.
- Revalidated `klipper/flare_mmu.cfg` Jinja brace balance (`95/95`),
  `python3 -m py_compile scripts/*.py`, and `ninja -C build_local`.

2026-05-20 quiet gate + MV correction:
- Changed `RA:1` from reactive buffer-following into a quiet gate: sync and
  post-print negative sync are suppressed, and firmware does not react to
  buffer changes while RA is active.
- Explicit `RA:0` now immediately attempts the existing
  `BUFFER_SERVICE_NEG_SYNC` path once, so any compression accumulated during
  tip forming can begin reversing without waiting for an idle dwell. Automatic
  RA clears before TC/load/unload/move remain quiet so they do not start
  competing buffer service before those motion commands take ownership.
- Reintroduced explicit `MV:-gear_retract:gear_retract_spd` before the
  post-tip gear-clear printer retract; RA is released before this MV/G1 pair.
- Revalidated `klipper/flare_mmu.cfg` Jinja brace balance (`97/97`),
  `python3 -m py_compile scripts/*.py`, and `ninja -C build_local`.

2026-05-21 MV ignore-buffer + nonblocking TC follow-up:
- Accepted the user-tested `MV:...:I` direction/ignore-buffer shape and made
  finite `MV:` report `EV:MOVE_DONE` to the host helper.
- Removed `RA:1` / `RA:0` from the shared Klipper toolchange macro. Tip
  forming now starts a derived slow `MV:-...:I` old-lane retract before the
  final printer park retract.
- Changed shared Klipper `TC:` shell call to return after command acceptance;
  `_FLARE_TC_STATE` delayed_gcode remains responsible for sensor-gated
  post-TC hotend loading.
- Revalidated `klipper/flare_mmu.cfg` Jinja brace balance (`98/98`),
  `python3 -m py_compile scripts/*.py`, and `ninja -C build_local`.

2026-05-21 purge-speed unit fix:
- Multiplied pickup, meltzone approach, test-load, and purge extrusion
  feedrates by 60 so Klipper receives mm/min `F` values. This prevents
  `purge_spd: 30.0` from becoming `F30` / 0.5 mm/s.

2026-05-21 test macro port correction:
- Added the LH-Stinger toolhead distance calibration URL as a comment in
  `klipper/flare_mmu.cfg`, not only in `KLIPPER.md`.
- Reworked `FLARE_TEST_TIP_FORMING` to follow the upstream
  `SP_TEST_MANUAL_TIP_FORMING` structure: parameter overrides update
  `_FLARE_TIP_FORMING_DEFAULTS`, print simulation uses a separate pause
  retract, and post-tip inspection uses staged extruder unload with a final
  60 mm pull.
