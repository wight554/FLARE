## ADDED Requirements

### Requirement: Type-P Stabilize Rail Breakaway

The firmware SHALL allow type-P idle/boot buffer-stabilize to drive the buffer
off a saturated rail to goal. While the analog signal is saturated
(`g_buf_analog_saturated_since_ms != 0`), the stagnation guard SHALL NOT abort on
the short-window position-change test; it SHALL keep driving and re-baseline the
stagnation reference position, aborting only if the buffer remains saturated past
`PSF_STAB_RAIL_BREAK_MS` measured from stabilize start. Once the signal
desaturates, the firmware SHALL apply the standard dry-spin stagnation check
(`PSF_STAB_STAGNANT_MS` / `PSF_STAB_STAGNANT_NORM`) with its window measured from
desaturation, not from stabilize start. Type-D stabilize is unchanged.

#### Scenario: Loaded buffer breaks off the tension rail

- **WHEN** `BUF_SENSOR_TYPE == 1`, filament present, the buffer rests saturated at
  the home/tension rail, and stabilize (`BS` or boot) starts
- **THEN** the motor drives toward goal until the buffer leaves saturation and
  reaches goal, emitting `BUF_STAB:DONE`
- **AND** no `BUF_STAB:STAGNANT_TIMEOUT` is emitted while still within
  `PSF_STAB_RAIL_BREAK_MS`

#### Scenario: Stuck/uncoupled buffer aborts at the breakaway cap

- **WHEN** `BUF_SENSOR_TYPE == 1` and the buffer stays saturated at the rail past
  `PSF_STAB_RAIL_BREAK_MS` (jammed or not coupled)
- **THEN** stabilize emits `BUF_STAB:STAGNANT_TIMEOUT` and stops
- **AND** it does not run to the 10 s deadline

#### Scenario: Off-rail dry-spin still aborts fast

- **WHEN** `BUF_SENSOR_TYPE == 1`, the signal is not saturated, and the buffer
  position changes less than `PSF_STAB_STAGNANT_NORM` within
  `PSF_STAB_STAGNANT_MS` after desaturation
- **THEN** stabilize emits `BUF_STAB:STAGNANT_TIMEOUT` and stops
