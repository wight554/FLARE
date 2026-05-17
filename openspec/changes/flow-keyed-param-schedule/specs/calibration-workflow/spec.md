## ADDED Requirements

### Requirement: Analyzer emits a deterministic flow-keyed schedule

The offline analyzer SHALL be able to emit a flow-keyed schedule (multiple
flow→{baseline, bias} breakpoints) from the existing per-`(feature,
v_fil_bin)` velocity buckets, in addition to the scalar baseline. The
schedule derivation SHALL reuse the deterministic dual-profile reducer and
existing maturity gates, SHALL be bounded by the configured breakpoint
cap, and SHALL remain the sole persistent authority for baseline/bias.

#### Scenario: Schedule derived from mature bins

- **WHEN** the analyzer is run in schedule-emit mode over buckets with
  multiple mature flow bins
- **THEN** it emits a sorted, bounded schedule with one reduced point set
  derived deterministically from those bins

#### Scenario: Falls back to scalar when sparse

- **WHEN** too few flow bins pass the maturity gates
- **THEN** the analyzer emits a length-1 (scalar-equivalent) schedule
  rather than a noisy multi-point table
