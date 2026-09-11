# buffer-state-lock Specification

## Purpose
Describes host-controlled buffer lock behavior used to drive type-D buffers to tension or compression for setup, service, and testing.

## Requirements
### Requirement: BL Command Surface
The firmware SHALL accept a host `BL:<state>` command that arms the active
lane to drive the buffer to the requested extreme and lock there, where
`<state>` is `T` (tension) or `C` (compression). `BL` with no argument
SHALL be treated as `BL:T`. The firmware SHALL expose the current lock arm
in status as `BL:T`, `BL:C`, or `BL:0` (disarmed).

#### Scenario: Host arms tension lock
- **WHEN** the host sends `BL:T`
- **AND** the controller is in `SYNC_OFF`
- **THEN** the controller acknowledges with `OK`
- **AND** status reports `BL:T` until the lock is broken or released

#### Scenario: Host arms compression lock
- **WHEN** the host sends `BL:C`
- **AND** the controller is in `SYNC_OFF`
- **THEN** the controller acknowledges with `OK`
- **AND** status reports `BL:C` until the lock is broken or released

#### Scenario: BL takes over from active sync
- **WHEN** the host sends `BL:T`
- **AND** the controller is in `SYNC_ACTIVE`
- **AND** no non-sync task (FL/UL/MV/AUTOLOAD/TC/cutter/manual_unload) is
  running on either lane
- **THEN** the controller calls `sync_disable(false)` (non-destructive;
  estimator/drift/sigma/integrator preserved)
- **AND** acknowledges with `OK`
- **AND** enters the prime sub-state of the buffer-lock lifecycle

#### Scenario: BL rejected while non-sync task running
- **WHEN** the host sends `BL:T`
- **AND** a non-sync task is running on either lane, or the toolchange
  orchestrator is mid-flight, or the cutter is busy, or a manual unload
  state machine is active, or boot stabilize is still in progress
- **THEN** the controller rejects with `ER:BUSY`
- **AND** no motor motion is started

### Requirement: Bounded Half-Travel Prime
On `BL` the firmware SHALL drive the active lane toward the requested extreme
and stop as soon as either the corresponding raw buffer state
(`BUF_TENSION` or `BUF_COMPRESSION`) is reached or `BUF_MAX_TRAVEL_MM / 2`
mm of MMU travel is completed, whichever comes first. The prime MUST NOT
exceed the half-travel cap.

#### Scenario: Prime hits target switch first
- **WHEN** `BL:T` is armed
- **AND** the buffer raw state transitions to `BUF_TENSION` before
  `BUF_MAX_TRAVEL_MM / 2` of travel
- **THEN** the lane stops at that point
- **AND** the lifecycle enters the locked sub-state

#### Scenario: Prime hits travel bound first
- **WHEN** `BL:T` is armed
- **AND** the MMU has traveled `BUF_MAX_TRAVEL_MM / 2` without reaching
  `BUF_TENSION`
- **THEN** the lane stops
- **AND** `EV:BL:PRIME_BOUND` is emitted
- **AND** the lifecycle enters the locked sub-state at the bounded endpoint

### Requirement: Locked Hold Contract
While locked the firmware SHALL energize the active lane motor with zero
commanded velocity, MUST NOT issue any closed-loop feed corrections from
the buffer state, and SHALL preserve estimator, drift observer, sigma,
confidence, and reserve integrator state.

#### Scenario: Lock holds against buffer spring
- **WHEN** the lane is locked at `BUF_TENSION`
- **AND** the printer/extruder is idle
- **THEN** the MMU motor remains energized at zero feed
- **AND** the buffer state remains `BUF_TENSION`

#### Scenario: Lock preserves controller learning
- **WHEN** the lane is locked
- **THEN** estimator, drift, sigma, confidence, and reserve integrator
  values are not reset

### Requirement: Lock-Break On External Force
The firmware SHALL treat any departure of the raw buffer state from the
locked extreme as a non-MMU (external) force lock-break and MUST transition
to the catch sub-state on the first raw edge, without waiting for the
`BUF_HYST_MS` debounce window.

#### Scenario: Extruder retract breaks the lock
- **WHEN** the lane is locked at `BUF_TENSION`
- **AND** the printer-side extruder retracts, advancing the buffer toward
  neutral
- **AND** the raw buffer state leaves `BUF_TENSION`
- **THEN** the lifecycle enters the catch sub-state on the same firmware
  tick
- **AND** `EV:BL:BREAK` is emitted

### Requirement: Instant-Slam Catch With Asymmetric Safety
On lock-break the firmware SHALL drive the active lane in the mirror
direction (retract for `BL:T` break, feed for `BL:C` break) at
`GLOBAL_MAX_SPS` via an instant `current_sps = target`
write, bypassing `SYNC_RAMP_UP_SPS`. The catch MUST tolerate transient
over-drive back toward the armed extreme as a safe recoverable direction
and SHALL NOT throttle the catch to avoid it.

#### Scenario: Tension-armed catch slams retract
- **WHEN** the lane was locked at `BUF_TENSION` and the lock is broken
- **THEN** the active lane drives in the retract direction at
  `GLOBAL_MAX_SPS` on the same tick as the break
- **AND** no PD ramp is applied to the initial step

#### Scenario: Over-drive back to tension is permitted
- **WHEN** the catch is active and the buffer briefly re-enters
  `BUF_TENSION`
- **THEN** the catch does not fault and does not throttle
- **AND** the lane continues mirroring until release

### Requirement: Manual Release Via BS
The host `BS` (buffer stabilize) command SHALL release any active `BL`
lock or catch immediately, run normal buffer stabilization, and return the
controller to `SYNC_OFF`.

#### Scenario: Operator aborts lock
- **WHEN** the lane is locked or catching
- **AND** the host sends `BS`
- **THEN** the lock and catch are released
- **AND** normal buffer stabilization runs
- **AND** the controller returns to `SYNC_OFF`

### Requirement: Locked-State Watchdog
The firmware SHALL emit `EV:BL:TIMEOUT` and auto-release the lock if no
lock-break, no `BS`, and no other release happens within a configurable
timeout (default 30 seconds) of entering the locked sub-state.

#### Scenario: Misordered macro leaves lock armed
- **WHEN** the lane has been locked for longer than the watchdog timeout
- **AND** no external force has broken the lock
- **AND** no `BS` has been received
- **THEN** the firmware auto-releases the lock
- **AND** emits `EV:BL:TIMEOUT`
- **AND** returns to `SYNC_OFF`
