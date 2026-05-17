## ADDED Requirements

### Requirement: Relief-effort counters are accumulated and exposed

The firmware SHALL accumulate relief effort in commanded-MMU mm:
`g_sync_refill_effort_mm` while the buffer is in ADVANCE and
`g_sync_relieve_effort_mm` while in TRAILING, derived from the existing
commanded-MMU mm integration. Both counters SHALL reset on sync state change
and buffer-state change (sustained-since-entry semantics). Both SHALL be
exposed in the `protocol.c` status line and as GET parameters. No control
behavior SHALL derive from these counters.

#### Scenario: Refill effort accrues during sustained ADVANCE

- **WHEN** the buffer stays in ADVANCE while filament is commanded
- **THEN** `g_sync_refill_effort_mm` increases by the commanded-MMU mm and
  is readable via status and GET

#### Scenario: Counters reset on state change

- **WHEN** the sync state or buffer state changes
- **THEN** both effort counters reset to zero

### Requirement: Warn only effort threshold events

The firmware SHALL emit warn-only diagnostic events when sustained relief
effort exceeds a configured threshold. A `SYNC cannot_refill` event MUST be
emitted once per episode when refill effort crosses
`CONF_SYNC_CANNOT_REFILL_MM` while still in ADVANCE. A `SYNC cannot_relieve`
event MUST be emitted once per episode when relieve effort crosses
`CONF_SYNC_CANNOT_RELIEVE_MM` while still in TRAILING. These events MUST be
diagnostic only and MUST NOT alter control output.

#### Scenario: cannot_refill warns once per episode

- **WHEN** refill effort exceeds `CONF_SYNC_CANNOT_REFILL_MM` while still in
  ADVANCE
- **THEN** exactly one `SYNC cannot_refill` event is emitted until the
  counter resets, and control output is unchanged

#### Scenario: cannot_relieve warns once per episode

- **WHEN** relieve effort exceeds `CONF_SYNC_CANNOT_RELIEVE_MM` while still
  in TRAILING
- **THEN** exactly one `SYNC cannot_relieve` event is emitted until the
  counter resets, and control output is unchanged
