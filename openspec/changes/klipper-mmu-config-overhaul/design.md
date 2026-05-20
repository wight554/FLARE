## Context

`KLIPPER.md` is the only integration reference for FLARE+Klipper users.
It currently includes sync tuning, calibration print workflows, and
gcode_marker usage that belong in `TUNING.md`. There is no ready-to-use
macro file — users must assemble snippets manually. The tip-forming
sequence is a two-line placeholder. Toolhead sensor "Option B" is
presented as a user choice when it is actually an automatic firmware
fallback.

Explored `scripts/` of the LH-Stinger Pico MMU project (sp_mmu_code.cfg)
for prior art on tip forming, load hotend, boot init, and test macros.
Variable names are reused directly so users following the SP wiki at
https://github.com/lhndo/LH-Stinger/wiki/Pico-MMU#toolhead-distance-calibration
can port their measurements with zero translation.

## Goals / Non-Goals

**Goals:**
- Single copyable `klipper/flare_mmu.cfg` covering all Klipper-side MMU
  integration (variables, tip forming, load hotend, toolchange, boot init,
  test macro)
- Variable names match SP wiki (`dist_sensor_to_extruder`,
  `dist_filament_park`, `dist_extruder_to_meltzone`) for portability
- `KLIPPER.md` becomes an integration-only doc (serial, sensor wiring,
  config reference, troubleshooting); tuning/calibration entirely in
  `TUNING.md`
- `RELOAD_MODE` set transiently on every Klipper boot via `[delayed_gcode]`
  — no `SV:` so FLARE reverts to persisted firmware default when running
  standalone

**Non-Goals:**
- No firmware changes
- No `config.ini` / `tune.h` changes
- No TUNING.md changes (tuning content already there)
- Not covering multi-lane (>2) or non-MMU-mode-only setups

## Decisions

### D1 — Variable naming: reuse SP wiki names

Use `dist_sensor_to_extruder`, `dist_filament_park`,
`dist_extruder_to_meltzone` (with `dist_` prefix) matching the SP wiki
diagram. Users following LH-Stinger guide can copy values directly.

Skip SP variables that are MMU-motor-side only (`dist_mmu_to_hub`,
`dist_hub_to_sensor`, `dist_sensor_to_synced_move`) — FLARE firmware
handles those internally via `dist_out_y`, `dist_y_buf` in `config.ini`;
Klipper macros never drive the FLARE motor directly.

### D2 — Gear retract: derived, not a separate variable

After tip forming, filament tip is `dist_filament_park` mm below extruder
gears (measured from gear center downward toward meltzone). To clear the
toolhead sensor before `TC:`:

```
gear_retract = dist_filament_park + dist_sensor_to_extruder + 5
```

Computed in `_FLARE_CHANGE_LANE` from existing vars. Prevents variables
from getting out of sync.

### D3 — Purge: synchronous helper, no polling

`flare_cmd.py` blocks until `EV:TC:DONE`. After TC: returns, filament is
at the toolhead sensor. Sequence is purely sequential:

```
TC: (blocks)
→ PICKUP: G1 E{dist_sensor_to_extruder * 1.2}
→ _FLARE_LOAD_HOTEND: 3-stage meltzone approach
→ _FLARE_PURGE: optional plain purge or chute hook/brush purge
```

No `[delayed_gcode]` polling needed. SP's approach is the same
(synchronous Klipper scheduling).

### D4 — HD:1 / HD:0 scope: in _FLARE_CHANGE_LANE, not tip forming

`_FLARE_TIP_FORMING` handles only extrusion moves. `_FLARE_CHANGE_LANE`
wraps it with `HD:1` / `HD:0`. Keeps concerns separate; tip forming
macro is independently testable via `FLARE_TEST_TIP_FORMING`.

### D5 — Option B: note only, not a parallel option

`TS_BUF_MS` fallback is automatic firmware behavior when no physical
sensor fires. KLIPPER.md collapses to a note: *"Without a physical
sensor, set `dist_sensor_to_extruder: 0` — FLARE detects load completion
via buffer geometry (TS_BUF_MS, default 2000 ms)."*

### D6 — RELOAD_MODE: transient SET: on boot, no SV:

`[delayed_gcode _FLARE_BOOT]` runs `SET:RELOAD_MODE:{v.enable_reload}`
on every Klipper start. No `SV:` — FLARE firmware reverts to last
persisted value on standalone reboot. This lets users test RELOAD
behavior changes without permanently flashing a new value, and avoids
the boot sequence overwriting manually-set persisted settings when Klipper
is not in use.

### D7 — Removed macros: FLARE_PRELOAD, FLARE_CUT_BARE, FLARE_CUT_TEST

Development/testing macros not needed for end-user MMU operation.
Dropping them reduces surface area and confusion.

### D8 — _FLARE_LOAD_HOTEND: 3-stage meltzone approach + _FLARE_PURGE

Mirrors SP `_SP_LOAD_HOTEND` approach distances (50% fast / 25% normal /
25% slow) for consistent meltzone priming. Push distance =
`dist_extruder_to_meltzone - dist_filament_park - tip_length_below_cut`
which is the remaining gap from park position to meltzone — same formula
as SP so ported values work immediately.

Purge extrusion lives in `_FLARE_PURGE` so operators can use the default
plain purge or enable Mini Purge Shute-style park hook/brush moves without changing
the meltzone approach sequence.

### D9 — FLARE_TEST_TIP_FORMING

Port of `SP_TEST_MANUAL_TIP_FORMING`. Simplified: no `FORCE_MOVE` or
synced MMU motor call (FLARE drives its own motor via TC:; test is
extruder-only). Sequence: load hotend → simulate print → tip form →
retract to clear gears → RESPOND to inspect.

## Risks / Trade-offs

- Defaults for tip forming (`cooldown_dist`, `dip_melt_gap`, etc.) are
  starting points; users must tune per their hotend. → Mitigated by
  `FLARE_TEST_TIP_FORMING` macro and reference to SP wiki tuning guide.
- `dist_filament_park` must be < `dist_extruder_to_meltzone` or load
  distance becomes zero/negative. → Document the constraint with a comment
  in the vars block; gear_retract formula is always positive as long as
  dist_filament_park > 0.
- KLIPPER.md restructure removes content some users may rely on. →
  `TUNING.md` already contains the tuning information; KLIPPER.md will
  reference it explicitly.

## Open Questions

None — scope fully decided in exploration phase.

## Implementation Plan

### klipper/flare_mmu.cfg
- Add a single Klipper include file with `_FLARE_VARS`, `_FLARE_TIP_FORMING_DEFAULTS`,
  `_FLARE_BOOT`, `_FLARE_TIP_FORMING`, `_FLARE_LOAD_HOTEND`,
  `_FLARE_CHANGE_LANE`, `T1`, `T2`, `FLARE_LOAD`, `FLARE_UNLOAD`,
  `FLARE_CUT`, and `FLARE_TEST_TIP_FORMING`.
- Keep all motion distances derived from the SP-compatible variables so users
  do not tune duplicate values.
- Risk: Jinja expressions must stay balanced and use only variables from
  `_FLARE_VARS` / `_FLARE_TIP_FORMING_DEFAULTS`.

### KLIPPER.md
- Replace inline macro examples with `klipper/flare_mmu.cfg` include guidance
  and list the provided macros.
- Collapse the no-physical-toolhead-sensor path into a brief fallback note.
- Remove sync tuning, telemetry, calibration print, and removed development
  macro content.
- Risk: retain serial setup, shell command helper, HD hold rationale,
  temperature warning, manual wrappers, and troubleshooting table.

### openspec/changes/klipper-mmu-config-overhaul/tasks.md
- Mark tasks complete immediately after each durable unit lands.
- Add validation notes after cfg review, Python compile, build, commit, and push.

## Follow-up Implementation Plan

### klipper/flare_mmu.cfg
- Split purge extrusion out of `_FLARE_LOAD_HOTEND` into `_FLARE_PURGE`.
- Add Mini Purge Shute-compatible optional park hook/brush settings to
  `_FLARE_PURGE` while keeping default behavior equivalent to the old inline
  purge when chute parking is disabled.
- Risk: `_FLARE_CHANGE_LANE` already saves/restores G-code state; `_FLARE_PURGE`
  must preserve relative extrusion and avoid requiring chute coordinates unless
  enabled.

### KLIPPER.md
- Add the LH-Stinger toolhead distance calibration link near `_FLARE_VARS`
  distance guidance.
- Replace purge-chute tips with a concrete `_FLARE_PURGE` implementation note
  and list the variables operators need to tune.

## Runtime Log Follow-up

Observed Klipper log showed `TC:` starting firmware unload immediately after
tip forming and gear retract were queued, then failing with
`EV:UNLOAD_BLOCKED` / `EV:TC:ERROR:UNLOAD_TIMEOUT`. Root cause: the macro did
not force Klipper's motion queue to drain before `HD:0` or `TC:` shell calls.

Fix: add `M400` after `_FLARE_TIP_FORMING` before `HD:0`, and after
`G1 E-{gear_retract}` before `TC:{lane}`. This keeps the tip-forming moves
inside HOLD and ensures the extruder has released the old filament before
firmware starts lane unload.

## Firmware TC Ordering Follow-up

User feedback clarified the semantic boundary for `TS:0`: it means the old
filament left the printer toolhead and FLARE may begin the lane unload. It is
not permission to swap lanes or feed the target lane.

Implementation plan:

### firmware/src/toolchange.c
- Move the `TC_UNLOAD_WAIT_TH` gate to the start of a different-lane
  toolchange when `TC_TH_MS > 0` and `TH:1` is latched.
- Make `TC_UNLOAD_WAIT_TH` transition into `TC_UNLOAD_REVERSE` after `TS:0`
  or timeout, so the old lane still performs OUT clear, optional cut, post-cut
  clear, and hub clear before `TC_SWAP`.
- Remove the post-unload `TC_UNLOAD_WAIT_TH` transition from
  `tc_unload_next_after_clear()`.
- Risk: no-sensor configurations with stale `TH:1` still rely on the existing
  `TC_TH_MS` timeout fallback before unload starts.

### BEHAVIOR.md + openspec/specs/toolchange-orchestration/spec.md
- Update the documented TC flow so toolhead clear is an unload-start gate, and
  target-lane load is only after old-lane unload/cut completion.

## Cutter Timeout Follow-up

New runtime log showed `TC:CUTTING` followed by `CUT:FEEDING`, then
`TC:ERROR:CUT_TIMEOUT` a few seconds later. This points at the outer TC cut
watchdog expiring while the cutter state machine is still legitimately inside
its feed/servo sequence. With larger `CUT_FEED` distances or repeat cuts, the
fixed default `TC_CUT_MS=5000` can be shorter than the configured cutter cycle.

Implementation plan:

### firmware/src/cutter.c + firmware/include/cutter.h
- Track whether the last cutter run failed, so TC can distinguish `CUT:DONE`
  from a cutter-side abort.
- Add a helper that computes expected cutter duration from runtime settings:
  open settle, feed distance(s), close/reopen settle per cut, final block
  settle, and slack.
- Emit specific cutter error reasons for internal cutter timeouts.

### firmware/src/toolchange.c
- In `TC_UNLOAD_WAIT_CUT`, treat `TC_CUT_MS` as a floor watchdog by comparing
  against `max(TC_CUT_MS, expected cutter duration)`.
- If the cutter has stopped due to failure, enter `TC_ERROR` instead of
  treating idle cutter as successful cut completion.

### Documentation
- Clarify `TC_CUT_MS` as an outer watchdog minimum; normal sizing follows
  cutter settings.

## Klipper Command Timeout / Phase Logging Follow-up

Runtime log showed `EV:TC:DONE:2` and Klipper's `Command {flare} finished` at
the same timestamp, which means `flare_cmd.py` did receive and accept the TC
success event. The confusing part is that `_FLARE_LOAD_HOTEND` performs only
local extruder/purge moves after TC, so the console has no explicit boundary
between "firmware TC complete" and "printer hotend loading/purging".

Implementation plan:

### scripts/flare_cmd.py
- Raise the default long-command timeout from 130s to 300s so long cutter,
  unload, and load cycles have headroom.
- Add completion-event waits for `FL:`, `UL:`, and `UM:` to match `KLIPPER.md`
  and prevent manual wrappers from returning on the initial `OK`.

### klipper/flare_mmu.cfg
- Pass `--timeout 300` for long FLARE commands from the shared macros.
- Add `RESPOND` markers before/after `TC:` and before hotend load/purge so logs
  make the phase boundary obvious.
- Add `M400` after TC before local extruder moves.

### KLIPPER.md
- Document `timeout: 300.0` for the shell command and explain that
  `_FLARE_LOAD_HOTEND` starts only after `EV:TC:DONE`.

## Safe Post-TC Callback Follow-up

User clarified the real failure: `_FLARE_LOAD_HOTEND` was placed after
`RUN_SHELL_COMMAND` in the same Klipper macro, so a shell timeout/error still
allowed Klipper to continue into hotend loading because the KIAUH
`gcode_shell_command` extension prints subprocess output but does not abort the
macro on a non-zero exit status.

Implementation plan:

### scripts/flare_cmd.py
- Keep completion-event waits for `TC:`, `FL:`, `UL:`, and `UM:`, but do not
  use transport callbacks. Klipper macro state and the physical toolhead sensor
  decide when local hotend loading begins.

### klipper/flare_mmu.cfg
- Remove hotend loading from the direct path after `RUN_SHELL_COMMAND`.
- Add `_FLARE_TC_STATE`, `_FLARE_ARM_TC_LOAD`,
  `_FLARE_ON_TOOLHEAD_RUNOUT`, `_FLARE_ON_TOOLHEAD_INSERT`, and delayed
  `_FLARE_TC_POLL`.
- Arm pending MMU state before gear retract. When the toolhead sensor runout
  fires, start a delayed poll like the tangle-detection pattern. The poll checks
  the physical `filament_switch_sensor` state until it sees filament detected
  again, then runs `_FLARE_POST_TC_LOAD`.
- Add `_FLARE_TC_FAILED` so sensor timeout clears the pending state and reports
  that hotend loading was skipped.
- Restore G-code state before the long shell command so failure leaves the
  printer in a sane local state.

### KLIPPER.md
- Document that post-TC hotend loading is gated by the physical toolhead sensor
  through Klipper delayed-gcode polling, not by shell-command timeout.

## Fast Retract Assist Follow-up

User feedback identified the physical failure during tip-forming handoff: after
the tip is parked, Klipper performs a long fast extruder retract to clear the
gears. The buffer cannot react quickly enough, so it can pin at the compression
side and the following firmware unload sees a blocked path (`UNLOAD_BLOCKED` /
`UNLOAD_TIMEOUT`).

Implementation plan:

### klipper/flare_mmu.cfg
- Add configurable retract-assist variables to `_FLARE_VARS`:
  `gear_retract_spd`, `retract_assist`, `retract_assist_dist_factor`, and
  `retract_assist_speed_factor`.
- Before the Klipper `G1 E-{gear_retract}` move, start FLARE active-lane reverse
  motion with `MV:-<distance>:<feed>` so the MMU follows the printer retract
  proactively instead of waiting for passive buffer compression recovery.
- Keep the TC call after the `M400`; if the assist is still active, TC's unload
  lane start will take ownership of the active lane task.
- Risk: users need `GLOBAL_MAX_RATE` high enough for the assist feedrate; the
  default assist is opt-in/on in the shared macro but tunable.

### KLIPPER.md
- Document the retract assist and the relationship between
  `gear_retract_spd`, `GLOBAL_MAX_RATE`, and fast gear-clear retracts.

## Retract Assist Mode Follow-up

User feedback identified a flaw in the one-shot `MV:` assist: the
tip-forming macro itself performs a long fast retract to the park position,
before the explicit gear-clear retract. Assisting only the final gear-clear
move still lets the buffer compress and jam during tip-forming.

Implementation plan:

### firmware/include/controller_shared.h + firmware/src/toolchange.c
- Add `TASK_RETRACT_ASSIST` so status can distinguish fast buffer-assist
  motion from finite `TASK_MOVE`.
- Update task-name reporting.

### firmware/include/sync.h + firmware/src/sync.c
- Replace `SYNC_HOLD` with `SYNC_RETRACT_ASSIST`; no compatibility alias.
- Rename the exported predicate from hold semantics to retract-assist
  semantics.
- Add `sync_retract_assist_set()` and `sync_retract_assist_enabled()`.
- During retract-assist, poll the raw buffer state each `buf_sensor_tick`:
  - `BUF_COMPRESSION`: reverse the active lane immediately at `GLOBAL_MAX_SPS`
  - `BUF_TENSION`: feed the active lane immediately at `REV_SPS`
  - `BUF_NEUTRAL` / `BUF_FAULT`: stop only `TASK_RETRACT_ASSIST`
- Start/adjust this task with no normal lane ramp so the response is limited
  by sensor debounce/main-loop latency rather than sync tick/ramp timing.
- Do not interfere with other lane tasks except stopping sync-owned
  `TASK_FEED` when entering retract-assist.

### firmware/src/protocol.c
- Remove `HD:<0|1>` and `GET:HOLD`.
- Add `RA:<0|1>` and `GET:RA`.
- Change status dump field from `HD` to `RA`.
- Make `TS:1`, `TC:`, `UL:`, `UM:`, `FL:`, `RL:`, `FD:`, `MV:`, and `ST:`
  clear retract-assist through the new helper before starting their own motion.

### klipper/flare_mmu.cfg
- Remove one-shot `MV:-distance:feed` assist variables and call.
- Keep only `gear_retract_spd`.
- Enable `RA:1` before `_FLARE_TIP_FORMING`; disable `RA:0` only after the
  post-tip gear-clear retract and its `M400`, right before `TC:`.

### Documentation and specs
- Update `KLIPPER.md`, `MANUAL.md`, `BEHAVIOR.md`, and relevant OpenSpec specs
  to describe `RA:` retract assist rather than `HD:` hold/negative sync.
