# Delta: cutter-feed-timeout

## ADDED Requirements

### Requirement: Servo test command rejected while cut active

`CP` (servo pulse test) SHALL be rejected with `ER:BUSY` unless the cutter
state machine is idle (`CUT_IDLE` or `CUT_BOOT_PARK`). A `CP` mid-sequence
otherwise overwrites cutter state, clears no failure flag, and lets toolchange
proceed on possibly-uncut filament.

#### Scenario: CP during toolchange cut

- **WHEN** host sends `CP:1500` while a cut sequence is in progress
- **THEN** firmware replies `ER:BUSY`
- **AND** the in-flight cut sequence continues unmodified

#### Scenario: CP while idle

- **WHEN** host sends valid `CP:<us>` and cutter is idle
- **THEN** servo moves to the test position and `OK` is replied
