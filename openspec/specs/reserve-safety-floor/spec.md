# reserve-safety-floor Specification

## Purpose
TBD - created by archiving change fix-flow-schedule-reserve-regression. Update Purpose after archive.
## Requirements
### Requirement: Reserve bias is floored by the configured scalar

The effective trailing-bias used to compute the reserve target SHALL be
`max(SYNC_TRAILING_BIAS_FRAC, schedule_bias)`. The flow schedule MAY
deepen the reserve (bias above the configured scalar) but SHALL NOT
reduce it below `SYNC_TRAILING_BIAS_FRAC` for any flow, segment, or
clamped endpoint.

#### Scenario: Weak schedule endpoint at low/startup flow

- **WHEN** live flow is below the first schedule breakpoint and that
  breakpoint's bias is less than `SYNC_TRAILING_BIAS_FRAC`
- **THEN** the reserve target uses `SYNC_TRAILING_BIAS_FRAC`, not the
  weaker schedule value

#### Scenario: Schedule may still deepen reserve

- **WHEN** a schedule breakpoint's bias exceeds `SYNC_TRAILING_BIAS_FRAC`
- **THEN** the larger schedule bias is used (reserve is deepened, never
  shallowed)

### Requirement: Baseline control floor never below configured baseline

`baseline_control_floor_sps()` SHALL return
`max(flow_param(extruder_est_sps).baseline_sps, g_baseline_target_sps)`,
restoring the guarantee that the control floor — and the ADVANCE recovery
gain derived from it — never drops below the configured persistent
baseline.

#### Scenario: Schedule baseline below config at clamped low flow

- **WHEN** the schedule-interpolated baseline at the current flow is below
  `g_baseline_target_sps`
- **THEN** the control floor is `g_baseline_target_sps`, so ADVANCE
  recovery gain does not collapse

### Requirement: Degenerate single-point parity preserved

The floored bias and baseline SHALL equal the configured scalars when the
schedule has a single point equal to those scalars, so behavior MUST be
byte-identical to the pre-flow-keyed scalar controller.

#### Scenario: One-point schedule equals scalars

- **WHEN** the schedule has one point equal to the config scalars
- **THEN** `max()` returns the scalars and reserve/baseline behavior
  matches the pre-flow-keyed controller exactly

### Requirement: Schedule and live learning are monotone-strengthening

The flow schedule and the live per-segment learner SHALL only ever
strengthen reserve depth and the baseline floor relative to the
configured scalars; neither SHALL reduce reserve depth or baseline floor
below config. This is the controller's full-bias safety invariant.

#### Scenario: No schedule/flow/segment weakens the safety floor

- **WHEN** any schedule, flow value, segment, or live ratchet state is
  active
- **THEN** the effective reserve bias is ≥ `SYNC_TRAILING_BIAS_FRAC` and
  the baseline control floor is ≥ `g_baseline_target_sps`

