# Design: mmu-mode

## Research Findings (code-grounded)

### Mode landscape
- `AUTO_MODE` (`main.c:141`, settings-backed): `1`=automated, `0`=host-controlled.
  `set_toolhead_filament()` ties `sync_enabled = TS` when `AUTO_MODE=0`
  (`main.c:251`).
- `RELOAD_MODE` (`main.c:43`): `0`=MMU, `1`=auto runout switch.
- `AUTO_MODE` and `RELOAD_MODE` are already independent persisted ints.

### Safety brakes are gated on `sync_enabled`, not `AUTO_MODE`
Only `AUTO_MODE` use in `sync.c` is line 916 (auto-start sync on `BUF_ADVANCE`).
Whole sync controller guarded by `if (!sync_enabled) return;` (`sync.c:927`):
trailing soft/hard wall (`SYNC_TRAILING_*`), advance pin/retract, mid-creep,
buffer stabilization all run whenever `sync_enabled`, regardless of `AUTO_MODE`.
=> Keeping safety brakes requires no new mode; keep `AUTO_MODE` on.

### Negative sync (handles Klipper extruder retraction)
- `buffer_negative_sync_eligible()` = active lane + OUT present (`sync.c:458`).
- TRAILING + eligible → `BUFFER_SERVICE_NEG_SYNC` reverses lane at `BUF_STAB_SPS`
  to follow extruder backward (`sync.c:473`, `:512`).
- `sync_recent_negative_until_ms` + `SYNC_RECENT_NEGATIVE_HOLD_MS` damps
  positive relaunch after a back move (`sync.c:1097`) — anti-oscillation.
- `g_idle_trailing_since_ms` + `POST_PRINT_STAB_DELAY_MS` gate prevents short
  TRAILING blips from kicking neg-sync (`sync.c:512`).

### Cutter (`CU:`) sequence (`cutter.c:101`+)
Open → feed filament FORWARD (`CUT_FEED_MM`/`CUT_LENGTH_MM`) → Close (snip) →
Reopen → repeat `CUT_AMOUNT` → Block → `EV:CUT:DONE`. Because feed is forward,
post-cut tail sits forward of OUT, so a second retract is required to re-park
behind OUT — this is why UL cutter-enabled spec is clear→cut→clear.

### Current command behavior
- `UL:` (`protocol.c:226`): `TASK_UNLOAD` reverse until OUT clears. No cut.
- `UM:` (`protocol.c:237`): `unload_to_in=true`, reverse until IN clears.
- `TC:` (`toolchange.c`): `TC_UNLOAD_CUT` (if `ENABLE_CUTTER && TC_AUTO_CUT`) →
  `TC_UNLOAD_REVERSE` → wait OUT → wait Y → swap → `TC_LOAD_*` → `EV:TC:DONE`.
- `TC_AUTO_CUT` wiring: `controller_shared.h:237` extern, `tune.h:101`
  `CONF_TC_AUTO_CUT`, `gen_config.py:382` (`tc_auto_cut`), `settings_store.c`
  `s.tc_auto_cut` (309/491), protocol SET 591 / GET 726, `main.c:145`.

## Decisions

1. **No new mode flag.** Klipper-driven MMU = `RELOAD_MODE=0` + `AUTO_MODE`
   left on + new unload-cut semantics + Klipper macros.
2. **`UL:` always cuts when cutter enabled.** Implement clear-OUT → CU →
   clear-OUT sequence. Cutter disabled keeps old single-retract behavior.
3. **Rename `TC_AUTO_CUT` → `UNLOAD_CUT`.** Now governs cut-on-unload for both
   `UL:`/`UM:` and `TC:`. Gated by `ENABLE_CUTTER`.
4. **`UM:` cut by entry condition.** OUT/YS present at entry → full UL cycle
   (cut included) + extend reverse to clear IN. OUT clear at entry → no cut.
5. **`TC:` ≡ UL+swap+FL.** TC unload phase reuses the new UL semantics so
   `TC:` and host `UL:`+`T:`+`FL:` are behaviorally equivalent.
6. **Klipper owns toolhead.** `change_lane` macro: form tip + retract out of
   gears via extruder (NOSF follows via negative sync while `sync_enabled`),
   then `TC:`; nosf_cmd.py blocks until `EV:TC:DONE`/`EV:TC:ERROR`.
7. **HOLD command deferred.** Adopt only if measured buffer half-travel cannot
   absorb tip-forming amplitude.

## Implementation Plan (per file, when approved)

### firmware/src/protocol.c
- `UL:` handler: if `ENABLE_CUTTER && UNLOAD_CUT`, drive multi-phase
  clear→cut→clear; else current single retract. Likely needs a small state
  machine (reuse cutter_busy gating) rather than one `lane_start`.
- `UM:` handler: branch on OUT/YS presence at entry per decision 4.
- Rename SET/GET token `TC_AUTO_CUT` → `UNLOAD_CUT`.
- Risk: `UL:`/`UM:` are currently fire-and-forget; cut sequencing makes them
  multi-phase — must emit `EV:UNLOADED` only after final clear, keep
  nosf_cmd.py blocking contract intact.

### firmware/src/toolchange.c
- Gate cut on `UNLOAD_CUT` (renamed). Confirm TC unload phase matches new UL
  ordering. Ensure RELOAD `TASK_UNLOAD` path (`:170`) does NOT cut.

### firmware/include/controller_shared.h, firmware/src/main.c, settings_store.c, scripts/gen_config.py, config.ini(.example), firmware/include/tune.h
- Rename `TC_AUTO_CUT`/`tc_auto_cut`/`CONF_TC_AUTO_CUT` → `UNLOAD_CUT` family.
- Bump `SETTINGS_VERSION` (settings_t field rename).
- Risk: keep config.ini, gen_config.py, tune.h, SET/GET, docs in lockstep
  (AGENTS.md rules 7 & 8).

### Docs
- MANUAL.md (`UL:`/`UM:`/`UNLOAD_CUT`), KLIPPER.md (`change_lane` macro, remove
  `NOSF_UNLOAD` from spool-change path), BEHAVIOR.md (unload state machine).

## Validation
- `ninja -C build_local` green.
- `python3 -m py_compile scripts/*.py`.
- Regression review: preload, autoload, RELOAD unload (no cut), TC:, manual
  UL:/UM: both flows, nosf_cmd.py blocking contract, persistence migration.
