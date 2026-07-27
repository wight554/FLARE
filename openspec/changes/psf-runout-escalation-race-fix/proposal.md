## Why

Confirmed on real type-P rig (2026-07-27, `/telemetry` capture, `reload_mode=1`):
a genuine complete runout never escalates to RELOAD. Instead
`SYNC:FAULT_HOLD_RECOVERY -> SYNC:AUTO_START -> (re-saturate in ~1s) -> SYNC:FAULT_HOLD`
loops forever (10+ observed cycles, ~6.4s period). `audit-reliability-fixes`
H6's RELOAD escalation (`sync_check_tension_dwell_and_ramp`, gated on
`FLARE_INT_SYNC_TENSION_DWELL_STOP_MS`=6000ms) never fires because
`sync_tick_type_p_rail_guard`'s analog-rail-saturation fault-hold
(`CONF_PSF_WALL_SAT_MS`=1000ms) runs first in `sync_tick`'s gated-checks
pass and short-circuits the tick before the dwell-based path is ever
reached — the dwell timer resets to 0 on every rearm and never accumulates
past ~1s. This exact risk was flagged (not confirmed) in the host-sync-sim
work on `reload_genuine_runout_escalation`'s scenario comment: "a full/fast
jam saturates the rail and trips the wall-timeout before the dwell timer
ever completes — worth confirming on rig at realistic print speeds." It is
now confirmed, reproducing every cycle.

## What Changes

- Extract the runout/RELOAD-escalation check already in
  `sync_check_tension_dwell_and_ramp` (lane present, `g_reload_mode`,
  `TASK_FEED`, `tc_state()==TC_IDLE`, both lane sensors clear) into a
  shared helper.
- Call that helper from `sync_tick_type_p_rail_guard`'s tension-rail
  fault-hold branch too, before `sync_fault_hold()`, so a genuine runout
  escalates to RELOAD regardless of which fault-hold timer (1s rail
  saturation or 6s tension dwell) fires first.
- Add a host-sim scenario reproducing the race (fast/complete runout,
  saturates within ~1s) that fails against current firmware and passes
  after the fix — TDD per the debugging process that found this.

## Capabilities

### Modified Capabilities
- `psf-type-p-sensor`: add a requirement that genuine-runout RELOAD
  escalation is reachable from both type-P fault-hold entry paths (rail
  saturation and tension dwell), not just the slower one.

## Impact

- `firmware/src/sync.c`: `sync_tick_type_p_rail_guard`,
  `sync_check_tension_dwell_and_ramp` (extract shared helper).
- `tests/host/sim_scenario.c`, `scripts/test_sync_sim.py`: new scenario +
  regression test reproducing the race.
- No protocol/config changes.
