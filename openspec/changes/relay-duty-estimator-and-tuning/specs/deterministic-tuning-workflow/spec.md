## ADDED Requirements

### Requirement: Deterministic offline relay duty analysis

`flare_analyze` SHALL compute switch-flip duty statistics from captured
relay data and emit recommended relay baseline and estimator `[lo, hi]`
bounds into the existing `config.ini`/flow-schedule emit mechanism. The
computation SHALL be a pure deterministic function of the input
captures — the same input files SHALL always produce the same recommended
relay values.

#### Scenario: Same inputs produce same relay recommendation

- **WHEN** `flare_analyze` is run twice on the same capture files
- **THEN** the emitted relay baseline and `[lo, hi]` bounds are identical

#### Scenario: Relay recommendation uses the existing emit mechanism

- **WHEN** the analyzer emits relay recommendations
- **THEN** they are written into the same `config.ini`/schedule format the
  operator already copies, not a separate ad-hoc path

### Requirement: Offline analyzer remains the persistent authority

The runtime relay estimator SHALL only adapt within the offline-emitted
`[lo, hi]` bounds. Persistence of recommended values SHALL be solely via
the deterministic offline analyzer. Happy Hare's online autotune
flash-save SHALL NOT be adopted.

#### Scenario: Runtime never overrides offline bounds

- **WHEN** the runtime estimate would exceed the offline-recommended
  bounds
- **THEN** it is clamped; the offline-recommended values remain the
  persistent authority

### Requirement: Acceptance-gate parity for non-relay inputs

The relay analyzer extension SHALL be additive. For existing non-relay
inputs, analyzer outputs and acceptance-gate verdicts SHALL be unchanged
versus the pre-change analyzer.

#### Scenario: Non-relay outputs unchanged

- **WHEN** `flare_analyze` runs on existing analog/flow-schedule captures
  used before this change
- **THEN** the emitted schedule and FAIL/WARN/PASS verdict are identical
  to the pre-change behavior
