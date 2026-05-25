## ADDED Requirements

### Requirement: Common Normalized Scale for PD Math
The sync PD loop SHALL operate on a common normalized position and target for
both type-D and type-P sensors, eliminating type-specific branches in shared
control math. `buf_pos_norm()` SHALL return normalized [-1,1] position for
both types; `buf_target_norm()` SHALL return normalized [-1,1] target.

#### Scenario: Type-D normalized position
- **WHEN** `BUF_SENSOR_TYPE == 0` and `g_buf_pos` is in mm
- **THEN** `buf_pos_norm()` returns `g_buf_pos / buf_threshold_mm()`

#### Scenario: Type-P normalized position
- **WHEN** `BUF_SENSOR_TYPE == 1` and `g_buf_pos` is already normalized [-1,1]
- **THEN** `buf_pos_norm()` returns `g_buf_pos` unchanged

#### Scenario: Same PD correction formula for both types
- **WHEN** sync_tick computes `reserve_correction`
- **THEN** the formula `(int)(error_norm * kp_window)` is used without
  sensor-type branching

### Requirement: Isolated Control Laws
Type-D relay bangbang and type-P continuous PD SHALL be implemented as
isolated static functions. No control law logic SHALL appear inline in
`sync_tick()`.

#### Scenario: Type-D relay law
- **WHEN** `BUF_SENSOR_TYPE == 0` and sync is active
- **THEN** `relay_control_law(s)` is called to produce `target_sps`
- **AND** TENSION → `relay_base * RELAY_CATCHUP_FRAC`, COMPRESSION → 0,
  NEUTRAL → demand-tracked value

#### Scenario: Type-P PD law
- **WHEN** `BUF_SENSOR_TYPE == 1` and sync is active
- **THEN** `psf_control_law(error_norm)` is called to produce `target_sps`
- **AND** target_sps is proportional to `error_norm` relative to baseline

### Requirement: sync_apply_scaling Unified Path
`sync_apply_scaling()` SHALL use a single code path operating on normalized
position and target for both sensor types. The type-P early-return branch
SHALL be removed.

#### Scenario: No early return for type-P
- **WHEN** `BUF_SENSOR_TYPE == 1` and `sync_apply_scaling()` is called
- **THEN** the same taper/deadband logic runs as for type-D
- **AND** no early return or separate scaling formula is applied

### Requirement: compression_floor Removed for Type-P
The firmware SHALL NOT force-raise the feed floor during `BUF_COMPRESSION` for
type-P (the L1750 block is removed). For type-P, COMPRESSION means buffer full;
forcing a feed floor fights drain and is incorrect.

#### Scenario: No feed floor during compression for type-P
- **WHEN** `BUF_SENSOR_TYPE == 1` and buffer state is `BUF_COMPRESSION`
- **THEN** `sync_current_sps` is NOT force-raised by a compression floor
- **AND** the PD law naturally reduces target_sps toward zero as error_norm
  shows buffer is at or past goal
