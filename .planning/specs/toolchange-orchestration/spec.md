# Toolchange Orchestration Specification

## Purpose
Durable contract for FLARE toolchange (TC) and RELOAD orchestration, defining phase boundaries, timeouts, and state expectations.

## Requirements

### Requirement: Full Automated Toolchange
The system SHALL orchestrate an automated sequence to swap active lanes without host intervention.

#### Scenario: Normal Toolchange
- **WHEN** `TC:<lane>` is commanded
- **THEN** the system waits for toolhead clear when `TC_TH_MS` is enabled and `TH:1` is latched, unloads the current lane until `OUT` clears, executes `UNLOAD_CUT` if `UNLOAD_CUT` is enabled, unloads until `OUT` clears again, waits for Y-splitter clear, updates `active_lane`, and starts `LOAD_FULL`
- **AND** emits phase events (`TC:CUTTING`, `TC:UNLOADING`, `TC:SWAPPING`, `TC:LOADING`, `TC:DONE`) at boundaries

#### Scenario: Toolhead clear wait is meaningful
- **WHEN** `TC:<lane>` starts while `TH:1` is latched from the old filament
- **THEN** the toolchange preserves that state during the unload phase
- **AND** does not start the lane unload until `TS:0` clears `TH` or
  `TC_TH_MS` expires
- **AND** does not swap/load the target lane until the old lane finishes OUT
  clear, optional cut, post-cut clear, and hub clear

#### Scenario: Cutter watchdog follows cutter configuration
- **WHEN** `TC:<lane>` runs the unload cutter sequence
- **THEN** the toolchange cut watchdog allows at least the configured cutter
  feed distance, repeat count, servo settle phases, and slack
- **AND** cutter-side timeout failures enter `TC_ERROR` instead of being
  treated as successful cut completion

### Requirement: Manual Cutter Execution
The host SHALL be able to trigger the exact cutter sequence independently of a full toolchange.

#### Scenario: Manual Cut with Feed
- **WHEN** `CU:` is commanded
- **AND** both lanes are idle and preloaded (`IN=1`, `OUT=0`)
- **THEN** the system executes the full cutter state machine (Open -> Feed -> Close -> Open -> Repeat -> Block)
- **AND** emits `EV:CUT:DONE` upon successful parking
- **AND** emits `EV:CUT:ERROR` upon failure or timeout

#### Scenario: Manual Cut Rejected Outside Preloaded State
- **WHEN** `CU:` is commanded
- **AND** either lane is not idle and preloaded (`IN=1`, `OUT=0`)
- **THEN** the command returns `ER:NOT_PRELOADED`
- **AND** the cutter state machine does not start

#### Scenario: Manual Cut without Feed (Bare)
- **WHEN** `CX:` is commanded
- **THEN** the system executes the cutter state machine but skips the filament feed logic (Open -> Close -> Open -> Repeat -> Block)
- **AND** emits `EV:CUT:DONE` upon successful parking
- **AND** emits `EV:CUT:ERROR` upon failure or timeout

#### Scenario: Active manual extruder unload cuts after first OUT clear
- **WHEN** `UL:` is commanded while `CUTTER` and `UNLOAD_CUT` are enabled
- **THEN** the system reverses until `OUT` clears
- **AND** then runs the fed cutter sequence
- **AND** then reverses until `OUT` clears again

#### Scenario: Manual unload skips cutter when disabled
- **WHEN** `UL:` or active-lane `UM:` is commanded while `CUTTER` or
  `UNLOAD_CUT` is disabled
- **THEN** the unload sequence continues without starting the cutter phase

### Requirement: Manual unload supports explicit standby lane eject
Manual MMU unload SHALL accept `UM`, `UM:`, `UM:1`, and `UM:2`. `UM` and
`UM:` SHALL preserve active-lane behavior. `UM:n` SHALL target the explicit
lane without changing `active_lane`.

When the explicit target lane is inactive, the command SHALL act only as a
standby-preload eject: the target lane must be idle, must have filament at
`IN`, and must not have filament at `OUT`. It SHALL unload until `IN` clears
without clearing active toolhead filament state, disabling active-lane sync,
changing active lane selection, inspecting shared Y-splitter occupancy, or
starting any cutter phase.

#### Scenario: Active-lane manual MMU eject from loaded state runs full unload first
- **WHEN** `UM`, `UM:`, or `UM:n` where `n` is the active lane is commanded
- **AND** `OUT` is present
- **AND** `CUTTER` and `UNLOAD_CUT` are enabled
- **THEN** the system runs the full active `UL:` sequence first
- **AND** then continues reverse until `IN` clears

#### Scenario: Active-lane manual unload remains unchanged without cutter
- **WHEN** `UM`, `UM:`, or `UM:n` where `n` is the active lane is commanded
- **AND** `CUTTER` or `UNLOAD_CUT` is disabled
- **THEN** the active-lane manual MMU unload behavior remains a non-cut eject to
  `IN` clear

#### Scenario: Inactive standby lane eject preserves active print state
- **WHEN** lane 1 is active and lane 2 is idle with `IN=1` and `OUT=0`
- **AND** `UM:2` is commanded
- **THEN** lane 2 unloads until `IN` clears
- **AND** `active_lane`, active sync state, and toolhead filament state are
  preserved

#### Scenario: Unsafe inactive target is rejected
- **WHEN** `UM:n` targets an inactive lane that is busy, lacks filament at
  `IN`, or has filament at `OUT`
- **THEN** the command returns an error and no motion starts

#### Scenario: Invalid lane payload is rejected
- **WHEN** `UM:<payload>` is commanded with a non-empty payload other than
  `1` or `2`
- **THEN** the command returns `ER:ARG`

#### Scenario: Static Servo Position Tuning
- **WHEN** `CP:<us>` is commanded
- **THEN** the system immediately sets the servo PWM pulse width to `<us>` microseconds and leaves it there
- **AND** interrupts any active cutter state machine sequence

### Requirement: RELOAD Buffer-Driven Contact
During runout RELOAD, the new lane SHALL approach until physical buffer contact is detected.

#### Scenario: RELOAD Approach
- **WHEN** the old lane clears the Y-splitter and `RELOAD_JOIN_MS` elapses
- **THEN** the new lane starts `TASK_FEED` at `JOIN_SPS`
- **AND** waits for the buffer to hit `BUF_COMPRESSION`
- **AND** aborts if the configured travel limit or physical timeout is reached before contact

### Requirement: RELOAD Bang-Bang Pressure Cycle
During the RELOAD follow phase, the new lane SHALL over-feed to close the gap and maintain pressure on the old tail.

#### Scenario: Follow Phase
- **WHEN** physical contact is established (`BUF_COMPRESSION`)
- **THEN** the motor target becomes `extruder_est_sps * RELOAD_LEAN_FACTOR` (over-feeding)
- **AND** drops to `COMPRESSION_RATE` if the physical arm hits the `COMPRESSION` wall
- **AND** repeats this cycle until `LOADED` (toolhead sensor triggered or `BUF_TENSION` sustained)
