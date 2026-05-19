## ADDED Requirements

### Requirement: Type-P inverted-polarity items are tracked, not blind-fixed
The deferred type-P inverted-polarity items SHALL be recorded in this
change and SHALL NOT be modified without validation on an analog buffer
rig. They are type-P-only (`BUF_SENSOR_TYPE != 0`); the type-D relay path
remains unaffected because it already bypasses them via relay
COMPRESSION → `SYNC_MIN`.

#### Scenario: Carried items enumerated
- **WHEN** a polarity audit reviews `sync.c`
- **THEN** items #6 (`sync_compression_floor_sps`), #7
  (`compression_recovery`/collapse), and the H2 feed-trim comment are found
  already recorded here as deferred
- **AND** they are not re-derived as new findings

#### Scenario: No blind fix
- **WHEN** a change proposes modifying the type-P feed-floor / recovery
  behavior
- **THEN** it MUST be developed and validated on an analog rig
  (`BUF_SENSOR_TYPE != 0`)
- **AND** MUST NOT be inferred solely from the type-D relay path

### Requirement: Relay path remains free of these assumptions
This deferral SHALL NOT reintroduce any inverted assumption into the type-D
relay path; the relay law's correctness (validated by
`relay-buffer-control-2switch`) is unchanged.

#### Scenario: Relay path unchanged
- **WHEN** type-D firmware runs the relay law
- **THEN** COMPRESSION → `SYNC_MIN` (drain) with no feed-floor force-raise
- **AND** the deferred type-P items are inert (never reached)
