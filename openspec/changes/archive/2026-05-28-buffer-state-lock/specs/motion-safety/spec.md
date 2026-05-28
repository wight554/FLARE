## ADDED Requirements

### Requirement: BL Prime Respects Travel Cap
The `BL` prime move SHALL terminate no later than `BUF_MAX_TRAVEL_MM / 2` mm
of MMU travel, regardless of whether the target buffer raw state has been
reached. The prime MUST NOT escalate to an unbounded drive on a stuck or
mis-wired buffer switch.

#### Scenario: Stuck switch does not produce unbounded retract
- **WHEN** `BL:T` is armed
- **AND** the `BUF_TENSION` switch never asserts
- **THEN** the lane stops after `BUF_MAX_TRAVEL_MM / 2` of MMU travel
- **AND** `EV:BL:PRIME_BOUND` is emitted
- **AND** no `FAULT:DRY_SPIN` or other guard escalation is required to stop
  the motor

### Requirement: BL Catch Bypasses MV Buffer Fault Guards
The instant-slam catch driven by `BL` lock-break SHALL run as a sync-owned
drive and is exempt from the `TASK_MOVE` buffer-fault guards
(`FAULT:MOVE_TENSION` on retract-into-tension and `FAULT:MOVE_COMPRESSION`
on forward-into-compression). The exemption SHALL apply only while the
controller is in `SYNC_RETRACT_ASSIST` and the catch sub-state is active.

#### Scenario: Catch retract through tension does not fault
- **WHEN** the lane was locked at `BUF_TENSION`
- **AND** lock-break triggers the catch
- **AND** the catch over-drives back into `BUF_TENSION` transiently
- **THEN** no `FAULT:MOVE_TENSION` is emitted
- **AND** the lane continues mirroring

#### Scenario: MV guards remain active outside the catch
- **WHEN** the controller is not in `SYNC_RETRACT_ASSIST`
- **AND** a `TASK_MOVE` is running
- **THEN** `FAULT:MOVE_TENSION` and `FAULT:MOVE_COMPRESSION` apply
  unchanged
