## ADDED Requirements

### Requirement: Two-profile bracketing procedure

The tuning workflow SHALL be: run the same model in two profiles — fastest
cubic flow and slowest cubic flow — using the existing tuner and marker
scripts, then run the analyzer's deterministic two-profile mode to derive
one baseline, then write that baseline to config and re-flash. The procedure
SHALL be documented as a fixed, ordered sequence with no run-to-run
variability in the resulting baseline.

#### Scenario: Operator follows the bracketing procedure

- **WHEN** an operator captures a fast-profile run and a slow-profile run
  with the marker scripts and runs the deterministic analyzer mode
- **THEN** a single baseline value is produced and is the same for the same
  captures on any machine or time

### Requirement: Live-tuner recommendation export script

`scripts/flare_baseline_recommender.py` SHALL be a host-only script
(stdlib + pyserial only) that reads the device tty, tracks the live tuner's
drift signal across a print, and at end-of-print reports a suggested
persistent `baseline_sps` with a supporting drift summary. It SHALL be
observe-only: no `SET` or `SV` writes. Given an identical captured input
stream it SHALL produce an identical recommendation (replayable for test).

#### Scenario: Recommendation at end of print

- **WHEN** the script has read a full print's status/marker/SYNC stream
- **THEN** it prints a suggested `baseline_sps` and the drift summary that
  justifies it, and performs no serial writes

#### Scenario: Deterministic from captured stream

- **WHEN** the script is replayed against the same recorded input stream
  twice
- **THEN** it produces the identical recommendation both times

### Requirement: Tuning workflow is simple and deterministic

The documented workflow SHALL NOT require interpreting different results
across repeated runs. Any value an operator is asked to commit SHALL be
reproducible from the captured inputs alone, independent of analysis time
or machine.

#### Scenario: No confusing variance

- **WHEN** an operator repeats the analyzer or recommender on the same
  captured data
- **THEN** the committed baseline value does not change between repetitions
