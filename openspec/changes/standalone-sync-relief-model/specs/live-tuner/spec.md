## ADDED Requirements

### Requirement: Sync State And Relief-Effort Diagnostics
Status and diagnostics output SHALL expose the current sync lifecycle state
and warn-only relief-effort counters (accumulated in commanded-MMU mm) so
operators and the offline analyzer can observe relief/fault behavior.

#### Scenario: State visible in status
- **WHEN** status is queried
- **THEN** the current state (`OFF`/`ACTIVE`/`HOLD`/`RELIEF_PAUSE`/
  `FAULT_HOLD`) is reported

#### Scenario: Cannot-refill warning
- **WHEN** ADVANCE persists past the refill-effort threshold
- **THEN** a warn-only `SYNC cannot_refill` event is emitted
- **AND** no control behavior changes solely due to the counter

#### Scenario: Cannot-relieve warning
- **WHEN** TRAILING persists past the relief-effort threshold
- **THEN** a warn-only `SYNC cannot_relieve` event is emitted
- **AND** no control behavior changes solely due to the counter
