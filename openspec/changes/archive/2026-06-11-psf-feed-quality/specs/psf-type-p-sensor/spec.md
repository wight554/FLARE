## ADDED Requirements

### Requirement: Type-P Feed Quality and Reliable Stabilize

Type-P feed control SHALL track extruder demand on a real print without sustained
buffer hunting or end-of-move overshoot that produces print artifacts, and a manual
`BS` SHALL drive the buffer to goal in a single invocation from any non-saturated
position. Acceptance is measured against a real print, not isolated bench bursts.

#### Scenario: Steady print feed stays near goal

- **WHEN** `BUF_SENSOR_TYPE == 1` and the printer extrudes continuously
- **THEN** the buffer holds a band near goal without a sustained tension↔compression
  limit cycle, and no `SYNC:FAULT_HOLD` / `SYNC:cannot_refill` fires mid-print

#### Scenario: Single-shot BS recovers from mid-tension

- **WHEN** `BUF_SENSOR_TYPE == 1`, the buffer rests in the control tension zone but
  is not saturated (`CF == 1.0`), and a manual `BS` is issued
- **THEN** the buffer is driven to goal on that single `BS` (no silent no-op
  requiring a second invocation)

#### Scenario: BS stabilizes the filament-bearing lane when the active lane is empty

- **WHEN** `BUF_SENSOR_TYPE == 1`, the active lane has no filament on its IN or OUT
  sensor, the other lane has filament present, and a manual `BS` is issued
- **THEN** stabilize drives the filament-bearing lane to goal instead of silently
  replying `OK` without motion

#### Scenario: BS breaks away from a deep saturated rail in one shot

- **WHEN** `BUF_SENSOR_TYPE == 1`, the buffer is saturated at a rail with the piston
  driven past the sensing range, and a manual `BS` is issued
- **THEN** stabilize keeps driving through the sensor-flat breakaway up to
  `PSF_STAB_RAIL_BREAK_MS` (default 3000 ms) and parks at goal in that single `BS`

#### Scenario: MV takes over cleanly from an in-flight stabilize

- **WHEN** a buffer stabilize or relief move is in flight and an `MV:` command is
  issued
- **THEN** the stabilize is cancelled before the move starts, so exactly one
  controller drives the lane motor (no stomped move, no zombie stabilize stop)
