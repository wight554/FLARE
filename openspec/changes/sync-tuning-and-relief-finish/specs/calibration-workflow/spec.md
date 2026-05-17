## ADDED Requirements

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
