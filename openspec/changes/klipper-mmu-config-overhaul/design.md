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

### D3 — Purge: synchronous inline, no polling

`flare_cmd.py` blocks until `EV:TC:DONE`. After TC: returns, filament is
at the toolhead sensor. Sequence is purely sequential:

```
TC: (blocks)
→ PICKUP: G1 E{dist_sensor_to_extruder * 1.2}
→ _FLARE_LOAD_HOTEND: 3-stage meltzone approach + optional purge
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

### D8 — _FLARE_LOAD_HOTEND: 3-stage meltzone approach

Mirrors SP `_SP_LOAD_HOTEND` approach distances (50% fast / 25% normal /
25% slow) for consistent meltzone priming. Push distance =
`dist_extruder_to_meltzone - dist_filament_park - tip_length_below_cut`
which is the remaining gap from park position to meltzone — same formula
as SP so ported values work immediately.

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
