# Calibration Workflow Specification

## Purpose

Capture the OpenSpec-native contract for Phase 2.9 observe-only calibration.
Old planning prose is available through git history when needed.

## Requirements

### Requirement: Calibration shall be observe-only by default

The calibration workflow SHALL collect evidence without mutating firmware
settings unless the operator passes explicit write flags.

#### Scenario: Tuner starts with default flags

- **WHEN** the operator starts `scripts/flare_live_tuner.py` for calibration
- **THEN** the tuner records state and optional CSV telemetry
- **AND** it does not send firmware setting writes
- **AND** it does not save firmware settings at print finish

### Requirement: State schema migrations shall be chained

Bucket state migrations SHALL be registered in a migration table and applied in
sequence without rewriting the migration loop for each new schema.

#### Scenario: A schema 1 state file is loaded by a newer tuner

- **WHEN** `migrate_state_data` receives an old state file
- **THEN** every registered migration is applied in order until the current
  schema is reached
- **AND** existing bucket data and `_meta` data are preserved

### Requirement: Bucket locking shall require cumulative evidence

A bucket SHALL lock only after satisfying cumulative evidence requirements for
samples, runs, layers, stability, and motion time.

#### Scenario: A bucket has many samples from one short run

- **WHEN** the bucket has stable estimates but insufficient run or motion-time
  evidence
- **THEN** the bucket remains STABLE
- **AND** `state-info` reports the unmet requirement

### Requirement: Analyzer patches shall be review-only outputs

`scripts/flare_analyze.py` SHALL emit review patches that preserve current values
for unavailable recommendations and label recommendation confidence.

#### Scenario: Evidence is insufficient for a tunable

- **WHEN** the analyzer cannot make a supported recommendation
- **THEN** the emitted patch shows the current value as the suggested value
- **AND** the confidence is DEFAULT or an explicit non-apply status

### Requirement: Long-running daemon calibration shall survive stale data

Daemon-mode calibration SHALL tolerate stale buckets and repeated runs without
allowing stale evidence to dominate current recommendations.

#### Scenario: Old buckets exist in state

- **WHEN** the analyzer or tuner reports current calibration status
- **THEN** stale data is either excluded or clearly identified according to the
  current workflow rules
- **AND** fresh evidence can continue accumulating in the same state file

### Requirement: Firmware live learning is bounded and subordinate to offline
The firmware live baseline tier SHALL be ephemeral, up-only, and gated to
`SYNC_ACTIVE`, and SHALL never write persistent state. Persistent baseline
and trailing-bias values SHALL change only through the reviewed offline
analyzer + config flash path.

#### Scenario: Live tier cannot persist
- **WHEN** the live learner adjusts the in-RAM baseline
- **THEN** no persistent state file or config value is written
- **AND** the persistent authority remains the offline-reviewed config

#### Scenario: Offline authority unchanged
- **WHEN** the operator applies a reviewed analyzer patch
- **THEN** the persisted baseline/bias updates as before
- **AND** firmware live behavior does not override the flashed authority below
  its configured value

### Requirement: Deterministic dual-profile baseline derivation

`flare_analyze.py` SHALL provide an explicit two-profile baseline mode that
takes a fastest-cubic-flow capture and a slowest-cubic-flow capture and
derives exactly one baseline value. The derivation SHALL be a pure function
of the input rows: stable bucket ordering, sample-count weighting only, no
wall-clock recency weighting, and a fixed rounding rule. Identical input
captures SHALL produce a byte-identical baseline regardless of when the
analyzer runs. The existing recency-weighted config-patch path SHALL remain
unchanged and selected separately.

#### Scenario: Same inputs produce same baseline

- **WHEN** the two-profile baseline mode is run twice on the identical fast
  and slow captures, at different wall-clock times
- **THEN** both runs emit byte-identical baseline output

#### Scenario: Existing patch path unaffected

- **WHEN** the analyzer is run in its existing recency-weighted
  config-patch mode
- **THEN** its output is unchanged from before this change

### Requirement: Offline analyzer remains sole persistent authority

The deterministic two-profile baseline SHALL be the only value written to
persistent memory for the baseline. The live tuner and the recommendation
script SHALL NOT write the persistent baseline; the live firmware baseline
SHALL remain ephemeral, up-only, and non-persistent.

#### Scenario: Recommender does not persist

- **WHEN** the recommendation script produces a suggested baseline
- **THEN** it only reports the value; no `SET`/`SV` write occurs and
  persistent memory is unchanged until an operator applies it via config

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
