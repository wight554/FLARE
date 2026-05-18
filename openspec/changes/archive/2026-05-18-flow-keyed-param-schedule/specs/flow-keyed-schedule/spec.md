## ADDED Requirements

### Requirement: Versioned bounded flow-keyed schedule format

The system SHALL define a versioned schedule table mapping estimated flow
to `{baseline_sps, trailing_bias_frac}`. The table SHALL be a strictly
increasing-in-flow, sorted array bounded by a config-tunable maximum
breakpoint count. The format SHALL be additive: existing scalar
`baseline_sps` / `trailing_bias_frac` config keys SHALL remain valid.

#### Scenario: Bounded sorted table generated

- **WHEN** `gen_config.py` processes a config containing a schedule table
- **THEN** the generated `tune.h` exposes the breakpoints sorted by flow
  and a length not exceeding the configured maximum

#### Scenario: Scalar-only config still valid

- **WHEN** a config has no schedule table, only scalar baseline/bias keys
- **THEN** generation succeeds and yields a length-1 schedule from those
  scalars

### Requirement: Degenerate single-point equivalence

A length-1 schedule SHALL produce, for every flow value, exactly the
scalar `baseline_sps` and the milli-resolution `trailing_bias_frac` it
was synthesized from. Bias fractions that are already aligned to integer
milli SHALL be exact; other bias fractions SHALL differ by no more than
0.0005 absolute bias after milli quantization. Firmware behavior with a
length-1 schedule SHALL match pre-change scalar behavior within that
milli-resolution bound.

#### Scenario: One-point schedule is flat

- **WHEN** the firmware evaluates a length-1 schedule at any
  `extruder_est_sps`
- **THEN** it returns the single point's baseline and milli-resolution bias unchanged

#### Scenario: Flow-sweep parity with scalar build

- **WHEN** a scalar-only config is run through a flow sweep on the new
  build and compared to the pre-change build
- **THEN** commanded sync output is identical for milli-aligned bias
  configs and differs by no more than 0.0005 absolute bias otherwise

### Requirement: Firmware interpolates baseline and bias on live flow

The firmware SHALL derive the active baseline and trailing-bias by
clamped linear interpolation of the schedule against the live
`extruder_est_sps`, with no extrapolation beyond the first/last
breakpoint. The flow key SHALL be the firmware's own `extruder_est_sps`
only; no host, Klipper, or encoder input SHALL be used. Interpolation
SHALL be float-light and bounded by the breakpoint count.

#### Scenario: Interpolated between breakpoints

- **WHEN** live flow falls between two breakpoints
- **THEN** baseline and bias are linearly interpolated from the two
  bracketing points

#### Scenario: Clamped outside range

- **WHEN** live flow is below the first or above the last breakpoint
- **THEN** the first or last point's values are used (no extrapolation)

### Requirement: Schedule emission is deterministic

Identical bucket inputs SHALL produce a byte-identical schedule table,
independent of analysis wall-clock time or machine. The emission SHALL
reuse the deterministic dual-profile reducer and `BIAS_SAFE_MIN/MAX`
clamps and SHALL NOT use wall-clock recency weighting.

#### Scenario: Same buckets, same table

- **WHEN** the analyzer emits a schedule twice from the identical bucket
  inputs at different times
- **THEN** both emitted schedule tables are byte-identical
