## ADDED Requirements

### Requirement: Tilde and glob expansion for input paths

Host scripts SHALL expand `~` and full glob syntax (`*`, `?`, `[...]`,
recursive `**`) for input/read path arguments. Resolution SHALL be
deterministic and sorted. The affected input arguments are
`gcode_marker.py` positional `input`, `flare_baseline_recommender.py
--file`, `flare_analyze.py --in` / `--profile-fast` / `--profile-slow`,
and `flare_live_tuner.py --sidecar`. List-valued arguments accept
multiple matches; single-valued arguments require exactly one match.

#### Scenario: Quoted glob is expanded

- **WHEN** an input argument is given a quoted glob pattern that the
  shell did not expand
- **THEN** the script expands `~` and the glob itself and operates on the
  sorted matched files

#### Scenario: Single-value argument with multiple matches

- **WHEN** a glob on a single-value input argument matches more than one
  file
- **THEN** the script reports an ambiguous-match error and exits non-zero
  without processing

### Requirement: Output paths are never globbed

Write/output path arguments SHALL be `~`-expanded only and SHALL NOT be
glob-expanded. The affected arguments include `gcode_marker.py
--output`, `flare_analyze.py --out`, `flare_live_tuner.py --state` and
`--csv-out`.

#### Scenario: Glob characters in an output path are literal

- **WHEN** an output path argument contains glob characters
- **THEN** they are treated literally after `~` expansion and no glob
  matching occurs

### Requirement: Path errors produce clean messages without tracebacks

The scripts SHALL report path-argument failures as a single stderr line
of the form `Error: <path>: <reason>` and SHALL exit non-zero with no
Python traceback. This MUST cover missing files, no-glob-match,
not-a-regular-file, and permission-denied. Unrelated exceptions MUST
still propagate.

#### Scenario: Missing input file

- **WHEN** an input path argument refers to a file that does not exist
- **THEN** the script prints `Error: <path>: <reason>` to stderr and
  exits non-zero without a traceback

#### Scenario: Glob matches nothing

- **WHEN** a glob pattern on an input argument matches no files
- **THEN** the script prints a no-match error and exits non-zero

#### Scenario: Real bugs still surface

- **WHEN** an error unrelated to path resolution occurs
- **THEN** it is not suppressed by the path-error handling

### Requirement: Existing analyzer --in behavior preserved

The shared resolution helper SHALL preserve `flare_analyze.py --in`
behavior: the set and sorted order of resolved runs for a given glob or
explicit file list SHALL match the pre-change behavior.

#### Scenario: Analyzer glob parity

- **WHEN** `flare_analyze.py --in` is given the same glob or file list
  before and after this change
- **THEN** the resolved run set and order are identical
