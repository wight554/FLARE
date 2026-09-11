# Delta: project-architecture

## MODIFIED Requirements

### Requirement: Persistence shall remain activity-gated

Flash persistence commands SHALL be rejected while motion, toolchange, cutter
activity, or boot stabilization could make persistence unsafe. Motor motion
SHALL include task-less drives: buffer-lock PRIME and FOLLOW move the motor
while `lane->task` stays `TASK_IDLE` and MUST count as activity. A static
buffer-lock hold (`BL_LOCKED`, motor energized at zero feed) MAY allow
persistence.

#### Scenario: Operator sends `SV:` while motion is active

- **WHEN** persistence is requested during an unsafe activity window
- **THEN** firmware rejects the request with the busy persistence error
- **AND** settings flash is not modified

#### Scenario: Operator sends `SV:` while buffer-lock is moving the motor

- **WHEN** `SV:`, `LD:`, `RS:`, or `CAL:` arrives while the buffer-lock state machine is in PRIME or FOLLOW
- **THEN** firmware rejects the request with `ER:PERSIST_BUSY`
- **AND** the buffer-lock motion continues undisturbed

### Requirement: Serial protocol changes shall preserve reply semantics

USB serial commands SHALL continue using `CMD:params\n` input and `OK:` / `ER:`
reply semantics, with best-effort `EV:` events where applicable. Terminal
events for lane operations SHALL carry the lane id, and a host-initiated
unload sequence SHALL conclude with exactly one terminal success or failure
event so hosts never wait on a silently-reset state machine.

#### Scenario: A new serial command is added

- **WHEN** `firmware/src/protocol.c` handles a new command
- **THEN** successful outcomes reply with `OK`
- **AND** failures reply with `ER`
- **AND** command behavior is documented in `MANUAL.md`

#### Scenario: Unload travel limit identifies the lane

- **WHEN** an unload task ends at its travel limit
- **THEN** the emitted event is `EV:UNLOAD_TIMEOUT:<lane>` (lane id payload, consistent with the rest of the lane-event family)

#### Scenario: Manual unload sequence emits a terminal event on failure

- **WHEN** a `UL:`/`UM:` sequence aborts (cutter failure, or OUT still blocked after the retract)
- **THEN** firmware emits a fault-class terminal event identifying the unload failure and its reason
- **AND** the event bypasses the best-effort event budget
