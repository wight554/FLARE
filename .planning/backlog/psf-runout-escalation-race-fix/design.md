## Context

`sync_tick()` runs `sync_tick_gated_checks()` first each pass; if it
returns true the tick returns immediately. `sync_tick_gated_checks` calls
`sync_tick_type_p_rail_guard()` first, which fault-holds on
`CONF_PSF_WALL_SAT_MS` (1000ms) rail saturation. H6's RELOAD escalation
lives in `sync_check_tension_dwell_and_ramp()`, only reached from
`sync_tick_calculate_target()` — called later, and never if gated_checks
already returned true. Its dwell threshold
(`FLARE_INT_SYNC_TENSION_DWELL_STOP_MS`, 6000ms) resets to 0 on every
`sync_rearm_active()` call (the psf-stale-fault-timers fix), so a fast/
complete runout that saturates the rail in ~1s re-triggers the rail-guard
path every cycle before the dwell timer can ever reach 6000ms. Confirmed
on real type-P rig, 2026-07-27 (see `memories/repo/host-sync-sim.md`):
`FAULT_HOLD_RECOVERY -> AUTO_START -> re-saturate(~1s) -> FAULT_HOLD`
looping indefinitely, `RELOAD_MODE=1`, never once reaching
`RUNOUT`/`RELOAD:SWITCHING`.

## Goals / Non-Goals

**Goals:**
- Genuine runout escalates to RELOAD regardless of which fault-hold timer
  fires first.
- Zero behavior change for type-D, for the existing slow-dwell escalation
  path, or for a non-runout (transient) saturation fault-hold.

**Non-Goals:**
- Not touching `CONF_PSF_WALL_SAT_MS`/`FLARE_INT_SYNC_TENSION_DWELL_STOP_MS`
  values themselves — the fix is reachability, not retuning the timers.
- Not addressing `RELOAD_MODE=0` behavior (unaffected either way — no
  escalation is expected there, fault-hold looping is the documented,
  intentional behavior for that mode).

## Decisions

- Extract the runout-escalation check (`lane && g_reload_mode &&
  lane->task == TASK_FEED && tc_state() == TC_IDLE && !lane_in_present(lane)
  && !lane_out_present(lane)`) plus its escalation action (`RUNOUT` event,
  `set_toolhead_filament(false)`, `lane_stop`, `reload_trigger`) from
  `sync_check_tension_dwell_and_ramp` into a shared static helper,
  `sync_try_runout_escalation(lane_t *lane, uint32_t now_ms)`, returning
  `bool` (true if it escalated).
- Call it from `sync_tick_type_p_rail_guard`'s tension-rail fault-hold
  branch (`g_buf_pos <= -TYPE_P_RAIL_NORM`) before calling
  `sync_fault_hold()` — same pattern as the existing call site.
- No change to the compression-rail branch (relief_pause) — runout is a
  tension-side (starvation) condition only, matches H6's original scope.

## Risks / Trade-offs

- The rail-guard runs even when `tc_state() != TC_IDLE` is NOT already
  excluded by an earlier guard (need to verify at implementation time
  whether `sync_tick`'s early-return on `tc_state() != TC_IDLE` already
  covers this — if so the helper's own `tc_state()==TC_IDLE` check is
  redundant-but-harmless, not a behavior change either way).
- Minimal blast radius: one new call site, no new global state, no timer
  value changes.
