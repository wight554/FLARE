## ADDED Requirements

### Requirement: Genuine Runout Escalation Reachable From Either Fault-Hold Path

The firmware SHALL escalate a genuine runout (`g_reload_mode` on, lane task
`TASK_FEED`, toolchange idle, both lane sensors clear) to the RELOAD state
machine regardless of which type-P fault-hold entry path detects the
starved buffer first: the fast (`CONF_PSF_WALL_SAT_MS`, ~1s) analog-rail
saturation path in `sync_tick_type_p_rail_guard` SHALL run the same
runout-escalation check already applied on the slower
(`FLARE_INT_SYNC_TENSION_DWELL_STOP_MS`, ~6s) tension-dwell path in
`sync_check_tension_dwell_and_ramp`, so a fast/complete runout that
saturates the rail well inside the dwell window still reaches RELOAD
instead of looping `FAULT_HOLD`/`FAULT_HOLD_RECOVERY` forever.

#### Scenario: Fast runout escalates via the rail-saturation path

- **WHEN** `BUF_SENSOR_TYPE == 1`, `RELOAD_MODE == 1`, the active lane is
  `TASK_FEED` with both its sensors clear, and the buffer saturates the
  tension rail within `CONF_PSF_WALL_SAT_MS` (faster than
  `FLARE_INT_SYNC_TENSION_DWELL_STOP_MS` can ever accumulate, since
  `sync_rearm_active` resets the dwell timer to 0 on every recovery)
- **THEN** `RUNOUT`/`RELOAD:SWITCHING` fire and the lane escalates to
  RELOAD
- **AND** no `FAULT_HOLD ↔ FAULT_HOLD_RECOVERY` loop occurs

#### Scenario: Slow runout still escalates via the tension-dwell path (unchanged)

- **WHEN** `BUF_SENSOR_TYPE == 1`, `RELOAD_MODE == 1`, and the buffer
  reaches sustained `BUF_TENSION` without ever hitting the analog-rail
  saturation threshold (e.g. a slow/moderate demand rate)
- **THEN** the existing dwell-based escalation
  (`sync_check_tension_dwell_and_ramp`) still fires `RUNOUT`/
  `RELOAD:SWITCHING` after `FLARE_INT_SYNC_TENSION_DWELL_STOP_MS`

#### Scenario: Type-D fault handling unchanged

- **WHEN** `BUF_SENSOR_TYPE == 0`
- **THEN** `sync_tick_type_p_rail_guard` does not run (type-P only) and
  type-D fault-hold behavior is unaffected
