# Delta: motion-safety

## MODIFIED Requirements

### Requirement: Dry Spin Protection
The system SHALL halt any spinning motor if no filament is detected at the intake and the buffer is not pulling. Background controllers (sync, RELOAD follow) SHALL NOT drive a lane whose fault latch is set, and SHALL NOT place a lane into a motor-spinning state that disarms the dry-spin watchdog.

#### Scenario: Filament Lost Mid-Task
- **WHEN** `TASK_FEED` or `TASK_LOAD_FULL` is active
- **AND** the `IN` sensor clears
- **AND** the buffer is not in `BUF_TENSION` (pulling a tail)
- **AND** this state persists for > 8 seconds
- **THEN** the motor stops and `FAULT:DRY_SPIN` is emitted
- **AND** automatic background restarts (sync or reload) are blocked until cleared by manual command or new filament insertion

#### Scenario: Sync does not drive a faulted lane
- **WHEN** the active lane has `fault != FAULT_NONE` (any task state) and sync computes a positive feed target
- **THEN** `sync_apply_to_active` does not enable or set a rate on that lane's motor
- **AND** the fault remains latched until a manual motion command or filament insertion clears it

#### Scenario: Runout without reload disables sync
- **WHEN** `RUNOUT` fires on the active lane during auto-started sync and RELOAD does not trigger
- **THEN** sync disables instead of restarting `TASK_FEED` on the empty lane
- **AND** a later real load re-engages sync through the normal auto-start path

## ADDED Requirements

### Requirement: Hardware watchdog bounds a hung control loop
The firmware SHALL enable the RP2040 hardware watchdog and feed it once per main-loop pass, so a wedged CPU cannot leave step-generation PWM free-running. The watchdog timeout SHALL exceed the longest legitimate loop stall (flash sector erase + program) with margin.

#### Scenario: CPU hang stops motion within the watchdog period
- **WHEN** the main loop stops iterating for longer than the watchdog timeout while a motor PWM is enabled
- **THEN** the watchdog resets the MCU
- **AND** boot re-initializes all step PWM slices disabled and motor enables inactive

#### Scenario: Legitimate stalls do not trip the watchdog
- **WHEN** an activity-gated flash save (`SV:`/`LD:`/`RS:`/`CAL:`) stalls the loop for its normal duration
- **THEN** the watchdog does not fire
