# Change: mmu-mode (Klipper-driven non-automated MMU)

## Status
Draft — exploration captured, decisions converged, BUF travel numbers pending.

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

### Out of scope (deferred, decision pending)
- Explicit sync HOLD command for tip-forming isolation. Needed only if buffer
  travel cannot low-pass-filter the tip-forming amplitude. Decision gated on
  measured `BUF_TRAVEL` vs intended max ramming retract.

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

## Open Questions
1. `BUF_TRAVEL` half-travel value (`GET:BUF_TRAVEL`) and intended max
   tip-forming/ramming retract — decides whether the deferred HOLD command is
   required.

## Regression Surface
- Every host `UL:`/`UM:` Flow-1 now cuts when cutter enabled (manual spool
  swap, error recovery). Accepted: spool change moves to `change_lane` macro.
- `RELOAD` unload path (`toolchange.c:170` `TASK_UNLOAD`) must keep current
  behavior — RELOAD unload must not gain a cut.
- `TC_AUTO_CUT` rename touches: `controller_shared.h`, `tune.h`/gen_config.py,
  `config.ini(.example)`, protocol SET/GET, `settings_store.c` field +
  `SETTINGS_VERSION` bump, `main.c`, `toolchange.c`, all docs.
