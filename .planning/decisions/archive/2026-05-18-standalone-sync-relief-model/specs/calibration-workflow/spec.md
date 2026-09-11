## ADDED Requirements

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
