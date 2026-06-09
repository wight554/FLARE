# Delta: project-architecture

## ADDED Requirements

### Requirement: Event lines carry single EV prefix

`cmd_event(type, data)` callers SHALL pass bare event type; `EV:` prefix is added once
by the protocol writer. Wire format is `EV:TYPE:DATA` (colon-separated), never
`EV:EV:...`.

#### Scenario: BL watchdog timeout emits

- **WHEN** firmware emits BL `TIMEOUT`, `PRIME_BOUND`, or `FOLLOW_GATED`
- **THEN** wire line reads `EV:BL:TIMEOUT` (etc.), single `EV:` prefix
- **AND** host daemon classifies type as `BL:TIMEOUT`, not `EV`

### Requirement: Fault-class events bypass best-effort budget

Firmware SHALL deliver fault and safety-abort events (`FAULT:*`, `CUT:ERROR`,
`TC:ERROR`, `RELOAD:FAULT`, BL `TIMEOUT`) regardless of the per-window
best-effort event budget; the budget MUST apply only to informational events.

#### Scenario: Event burst during fault

- **WHEN** event budget for the current window is exhausted and a lane faults
- **THEN** the `FAULT:*` event is still written to the host
- **AND** chatty informational events remain droppable

#### Scenario: TC error during event chatter

- **WHEN** a toolchange enters error while informational events saturate the window
- **THEN** `TC:ERROR` with its reason still reaches the host

### Requirement: Toolchange owns lane state against host commands

Firmware SHALL reject lane-mutating host commands (`T:`, `TC:`, `RL:`, `UL:`,
`UM:` on the active lane, `LO:`, `FL:`, `FD:`) with `ER:BUSY` while the
toolchange context is active (not `TC_IDLE`/`TC_ERROR`). A command that cannot
act MUST NOT reply `OK` (no silent no-op). `MV:` stays unguarded by design for
raw recovery.

#### Scenario: Lane select mid-toolchange

- **WHEN** host sends `T:2` while the TC state machine is unloading lane 1
- **THEN** firmware replies `ER:BUSY`
- **AND** `g_active_lane` and the TC sequence are unchanged

#### Scenario: TC while TC busy

- **WHEN** host sends `TC:1` while a toolchange is already running
- **THEN** firmware replies `ER:BUSY` (today: silent no-op with misleading `OK`)

#### Scenario: Raw recovery move stays available

- **WHEN** host sends `MV:` during a stuck toolchange for manual recovery
- **THEN** the move is accepted per existing semantics

## MODIFIED Requirements

### Requirement: Persistence shall remain activity-gated

Flash persistence commands SHALL be rejected while motion, toolchange, cutter
activity, or boot stabilization could make persistence unsafe. This gate covers
every command path that calls `settings_save()`, including calibration commands
(`CAL:PSF_*`), not only `SV`/`LD`/`RS`.

#### Scenario: Operator sends `SV:` while motion is active

- **WHEN** persistence is requested during an unsafe activity window
- **THEN** firmware rejects the request with the busy persistence error
- **AND** settings flash is not modified

#### Scenario: Operator sends `CAL:PSF_COMP` while motion is active

- **WHEN** a calibration save is requested during an unsafe activity window
- **THEN** firmware replies `ER:PERSIST_BUSY`
- **AND** neither calibration value nor flash is modified
