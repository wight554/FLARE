## ADDED Requirements

### Requirement: Type-P Fault Timers Scoped to Active Sync

The firmware SHALL scope the type-P tension-dwell and saturation fault timers to
the active-sync window so idle-accumulated state cannot fire a spurious fault on
engagement or deadlock fault recovery. On every type-P sync activation (normal
auto-start, relief-pause re-arm, fault-hold recovery) the firmware SHALL restart
`sync_tension_pin_since_ms` to `now` when the buffer is in `BUF_TENSION` and to `0`
otherwise. On `FAULT_HOLD_RECOVERY` the firmware SHALL reset
`g_buf_analog_saturated_since_ms` so the recovered active state gets a fresh
saturation window. Type-D fault handling is unchanged.

#### Scenario: Normal extrude does not fault on engagement

- **WHEN** `BUF_SENSOR_TYPE == 1`, the buffer has rested idle in the control
  tension zone (goal compression-side), and a normal extrude triggers `AUTO_START`
- **THEN** the tension-dwell fault does not fire from idle-accumulated dwell
- **AND** sync engages and the refill snap recovers the buffer without `FAULT_HOLD`

#### Scenario: Fault recovery does not instantly re-fault

- **WHEN** `BUF_SENSOR_TYPE == 1`, sync enters `FAULT_HOLD_RECOVERY` with the buffer
  still pinned at the tension rail
- **THEN** `g_buf_analog_saturated_since_ms` is cleared so the saturation timer
  restarts
- **AND** the recovered active state gets a full `PSF_WALL_SAT_MS` window for the
  refill snap before any re-fault — no infinite `FAULT_HOLD ↔ RECOVERY` loop

#### Scenario: Genuine sustained starve still faults

- **WHEN** `BUF_SENSOR_TYPE == 1` and the buffer stays pinned at tension during
  active sync past the dwell/saturation window despite max refill
- **THEN** the terminal `fault_hold` still fires (gear protection preserved)
