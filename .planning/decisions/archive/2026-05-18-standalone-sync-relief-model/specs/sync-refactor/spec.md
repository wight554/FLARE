## ADDED Requirements

### Requirement: Disciplined Live Baseline Learner
The firmware live baseline learner SHALL update only in `SYNC_ACTIVE`, require
multi-cycle agreement, reject high-variance observations, and enforce a
time-and-distance cooldown. It SHALL remain non-persistent and up-only; the
offline analyzer remains the sole persistent baseline/bias authority.

#### Scenario: No learning in non-active states
- **WHEN** the controller is in `SYNC_OFF`, `SYNC_HOLD`,
  `SYNC_RELIEF_PAUSE`, or `SYNC_FAULT_HOLD`, or trailing-recovery / fast-brake
  is active
- **THEN** the live baseline value is not updated

#### Scenario: Multi-cycle and variance gating
- **WHEN** a single settle into MID occurs
- **THEN** the baseline does not move on that observation alone
- **AND** it moves only after N comparable settles within the variance
  threshold and after the cooldown (time AND commanded-MMU distance) elapses

#### Scenario: Live drift self-heals
- **WHEN** the firmware reboots or reloads settings
- **THEN** the live baseline resets to the offline/config authority value
- **AND** no live-learned value is persisted

### Requirement: Non-Destructive Lifecycle Preserves Standalone Operation
Replacing destructive disable with explicit non-destructive states SHALL NOT
require host involvement and SHALL keep standalone post-flash operation
intact.

#### Scenario: Recovery without host
- **WHEN** the controller enters `SYNC_RELIEF_PAUSE` or `SYNC_FAULT_HOLD`
- **THEN** resumption/recovery occurs from firmware logic alone
- **AND** no host tuner or command is required during print
