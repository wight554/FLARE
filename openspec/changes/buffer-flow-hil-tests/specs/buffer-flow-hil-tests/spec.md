## ADDED Requirements

### Requirement: Daemon-Backed Event Capture
The HIL harness SHALL drive the board through the running `flare_daemon` over
HTTP — sending commands via `POST /cmd` and capturing board events from the
`/telemetry` SSE stream — and SHALL NOT open the serial port directly, so it
does not conflict with the daemon that owns the port.

#### Scenario: Command sent via daemon returns the board reply
- **WHEN** the harness sends a command
- **THEN** it issues `POST /cmd` to the daemon
- **AND** returns the board's `OK:` / `ER:` reply string

#### Scenario: Events captured from the telemetry stream
- **WHEN** the firmware emits an `EV:` event and the daemon re-broadcasts it on `/telemetry`
- **THEN** the harness records it as a matchable event without reading the serial port

### Requirement: Event Wait and Refute Assertions
The harness SHALL provide a positive assertion that blocks until an event
payload matches a substring or regex within a timeout, and a negative assertion
that confirms no matching event appears within a window. Matching SHALL operate
on the reconstructed `type[:data]` payload so it is correct for event types that
themselves contain colons.

#### Scenario: Positive match within timeout
- **WHEN** an event matching the needle arrives before the timeout
- **THEN** `wait_event` / `expect` returns the event
- **AND** `expect` raises `AssertionError` if none arrives in time

#### Scenario: Negative match (refute)
- **WHEN** no matching event appears within the window
- **THEN** `refute` passes (returns no event)
- **AND** `refute` raises `AssertionError` if a matching event does appear

#### Scenario: Stale events ignored via since
- **WHEN** a caller passes a `since` timestamp captured before an action
- **THEN** only events at or after that time are considered

### Requirement: Buffer-Flow Case Coverage
The suite SHALL provide operator-assisted cases for all five buffer flows —
sync, stab, buflock, load, unload — selectable by flow and by buffer sensor
type. Type-P cases are primary; type-D parity cases SHALL be tagged so they run
only under the digital sensor type. Load coverage SHALL exercise the `FL`
flow-load (buffer-relevant) and SHALL NOT treat preload/autoload as buffer
flows.

#### Scenario: Type-P unload over-tension
- **WHEN** filament is present and the buffer is held pinned at the tension rail during `UL`
- **THEN** the suite asserts `UNLOAD_BLOCKED`
- **AND** a healthy off-rail unload asserts that `UNLOAD_BLOCKED` does NOT fire

#### Scenario: Type-P idle stabilize gating
- **WHEN** filament is present and the buffer is off goal and `BS` is sent
- **THEN** the suite asserts `BUF_STAB:START` then `BUF_STAB:DONE`
- **AND** when no filament is present, it asserts `BUF_STAB:START` does NOT fire

#### Scenario: Auto-sync toggle under the sync flow
- **WHEN** `AUTO_MODE` is on and the buffer transitions across the tension threshold
- **THEN** the suite asserts `SYNC:AUTO_START`
- **AND** when the buffer merely rests at the home rail, it asserts `AUTO_START` does NOT fire

#### Scenario: Sensor-type selection
- **WHEN** the runner is invoked with `--type p`
- **THEN** only cases tagged for the analog sensor run
- **AND** type-D parity cases are excluded

### Requirement: Harness Unit Tests Without Hardware
The harness's pure parsing and matching logic SHALL be unit-testable with no
board or daemon, by injecting events directly, so it can run in CI.

#### Scenario: Pure logic tested without hardware
- **WHEN** the unit tests run
- **THEN** `parse_event`, `event_from_sse`, and `wait`/`refute`/`expect` are exercised by injecting events directly
- **AND** no serial port or daemon connection is required
