# Change: mmu-mode (Klipper-driven non-automated MMU)

## Status
Draft — exploration captured, decisions converged. Buffer half-travel
measured (7.8 mm); tip-forming analysis resolves HOLD command as REQUIRED.

## Why
We want a Klipper-driven multi-material flow where the printer owns toolhead-side
moves (tip forming, unload from extruder gears) and FLARE owns only the
spool/bowden side (cut, lane unload, swap, load). Today `TC:` does its own
toolhead-side retraction by reversing the lane motor; a Klipper-driven flow needs
FLARE to assume the hotend is already cleared by the extruder, follow the
extruder's retraction via existing negative sync, and then perform a clean
cut + unload + swap + load on command.

## Key Finding (drives scope)
Safety brakes (trailing soft/hard wall, advance retract, mid-creep, buffer
stabilization) are gated on `sync_enabled`, **not** `AUTO_MODE`. Only auto-START
of sync (`sync.c:916`) and empty-MMU autoload depend on `AUTO_MODE`. Therefore
**no new mode flag is needed** and `AUTO_MODE` should stay on. The Klipper-driven
flow is `RELOAD_MODE=0` + existing `AUTO_MODE` + new unload-cut semantics + Klipper
macros. Net new firmware is small.

## Scope

### In scope
1. `UL:` performs a cut as part of its cycle when the cutter is enabled.
2. Rename tunable `TC_AUTO_CUT` → `UNLOAD_CUT` (clearer: "cut on unload").
   When `ENABLE_CUTTER` is off, no cut regardless of `UNLOAD_CUT`.
3. `UM:` cut behavior differentiated by entry condition (see design).
4. `TC:` unload phase aligned so `TC:` ≡ `UL:` + swap + `FL:`.
5. Klipper `change_lane` macro: extruder-side tip forming + unload from gears,
   then issue `TC:` (no host `UL:` for spool change).
6. Docs sync: MANUAL.md, KLIPPER.md, BEHAVIOR.md, config.ini(.example).
7. **Sync HOLD primitive (REQUIRED, partial).** Explicit `HD:1`/`HD:0`
   toggle that suppresses `sync_tick` and `BUFFER_SERVICE_NEG_SYNC` but keeps
   `BUFFER_SERVICE_STABILIZE` (basic re-center), for the tip-forming wiggle
   regime; released before the ~20-30 mm extraction / `TC:`.
8. **Command-light flow (no `TS:`/`SM:` reliance).** `TC:` = UL old +
   swap + FL new, reusing existing `TASK_UNLOAD`/`TASK_LOAD_FULL`
   completion. FL self-completes on buffer geometry (`buf_advance_sane`),
   `AUTO_MODE` sets sync on LOADED. Klipper sends no `TS:1`/`TS:0`/`SM:`.
   `TS_BUF_MS`/physical sensor/`TS:1` are optional accelerators, not gates.

### Out of scope
- None.

## Behavior Contracts

### UL: (active lane unload)
- Cutter disabled (`ENABLE_CUTTER=0`): retract until lane OUT clears. (Old behavior.)
- Cutter enabled (`ENABLE_CUTTER=1` and `UNLOAD_CUT=1`):
  1. retract until lane OUT clears
  2. perform CU action (feed forward + snip, full cutter state machine)
  3. retract until lane OUT clears again

### UM: (unload toward IN)
- Flow 1 — OUT or Y-splitter still present at entry: run full UL cycle
  (including cut when cutter enabled) then continue reverse until IN clears.
- Flow 2 — OUT already clear at entry: no cut; retract until IN clears only.

### TC: (toolchange)
- Equivalent to: UL: (with cut per above) → set active lane → FL:.
- Assumes Klipper already cleared the hotend (tip formed, filament out of gears).
- FLARE does not perform toolhead-side extraction beyond clearing OUT/Y.

## Tip-Forming / Sync Interaction (analysis, no code unless HOLD adopted)
Buffer mechanical travel acts as a natural low-pass filter: tip-forming wiggle
with amplitude << buffer half-travel is absorbed by the buffer arm without
moving the MMU motor. FLARE anti-thrash machinery (positive-relaunch damping,
trailing collapse delay, `POST_PRINT_STAB_DELAY_MS` gate, reserve deadband)
further suppresses chasing. Conflict is tunable, not architectural, provided
ramming retract stays well under buffer half-travel and
`POST_PRINT_STAB_DELAY_MS` exceeds the longest tip-forming move duration.

## Resolved: tip-forming vs buffer geometry
- `GET:BUF_HALF_TRAVEL` = **7.8 mm** (tight).
- LH-Stinger Pico-MMU tip-forming defaults: `cooldown_dist` 5 mm,
  `cooldown_pull_speed` 70 mm/s (~71 ms/move), `cooldown_secondary_moves` 1,
  `pause_retract_dist` 0.5 mm, `dip_melt_gap` 2 mm, `park_speed` 130 mm/s.
- Hotend hot zone ~46 mm of 66 mm + gears-to-hotend length ⇒ Park retract
  must pull ~70 mm+ to clear extruder gears (huge vs 7.8 mm — intended
  neg-sync follow, then `TC:`).
- Operator's actual profile: pause → ~1 mm retract → ~0.5 mm push → small
  retract/push wiggles → one long retract to gears at the very end. The
  "small" wiggle moves are filament/profile dependent and **per some users
  reach 20+ mm** — far past 7.8 mm half-travel. Without HOLD each such move
  pins the buffer rail and fires neg-sync mid-tip-form. Plus cumulative net
  drift and uncentered paused-print entry. HOLD is strongly required, not a
  marginal safeguard.
- Margin math (worst case, LH-Stinger 5 mm): from a centered buffer reaches
  −5 mm (deadband ≈ 0.15·7.8 ≈ 1.17 mm) — reliably flips `BUF_TRAILING`.
  `POST_PRINT_STAB_DELAY_MS` default = **0** ⇒ neg-sync reverses the lane
  motor immediately, mid-cooldown.
- => HOLD retained for **determinism** (not per-move-amplitude luck):
  regardless of profile, the wiggle regime is fully partitioned from the
  final extraction.
- Caller analysis: `buffer_stabilize_tick` runs every loop;
  `buffer_stabilize_controller_idle()` requires `!sync_enabled` + idle lanes.
  So `TS:1` during tip forming → `sync_tick` chases the wiggle; `TS:0` →
  neg-sync chases it instead. **Neither existing `TS:`/`SM:` state suppresses
  both.** ⇒ A genuine HOLD primitive is REQUIRED, not optional.

## Open Questions
- Park retract total distance for this hotend (≈ hot-zone 46 mm + bowden to
  gears) — sizes the post-HOLD-release neg-sync/extraction expectation only;
  does not block design.

## Regression Surface
- Every host `UL:`/`UM:` Flow-1 now cuts when cutter enabled (manual spool
  swap, error recovery). Accepted: spool change moves to `change_lane` macro.
- `RELOAD` unload path (`toolchange.c:170` `TASK_UNLOAD`) must keep current
  behavior — RELOAD unload must not gain a cut.
- `TC_AUTO_CUT` rename touches: `controller_shared.h`, `tune.h`/gen_config.py,
  `config.ini(.example)`, protocol SET/GET, `settings_store.c` field +
  `SETTINGS_VERSION` bump, `main.c`, `toolchange.c`, all docs.
