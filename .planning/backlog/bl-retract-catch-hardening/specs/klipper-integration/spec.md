## ADDED Requirements

### Requirement: Buffer Commands Block Until Firmware Completion
`flare_cmd.py` SHALL treat `BL` and `BS` as completion-gated commands, blocking
on the daemon event stream until the firmware reports the operation finished,
rather than returning on the command acknowledgement. Klipper macros SHALL NOT
approximate these completions with `G4` dwells.

`COMPLETION_EVENTS` SHALL map:

- `BL` — success on `EV:BL:LOCKED` or `EV:BL:PRIME_BOUND`; failure on
  `EV:BL:TIMEOUT`.
- `BS` — success on `EV:BUF_STAB:DONE`; failure on `EV:BUF_STAB:TIMEOUT` or
  `EV:BUF_STAB:STAGNANT_TIMEOUT`.

Every firmware path that completes a `BS` SHALL emit a terminal event, so no
host invocation can hang to `--timeout`. On a failure event the script SHALL
exit non-zero; on no event within `--timeout` it SHALL report a completion
timeout and exit non-zero.

#### Scenario: BL blocks until the lock is established
- **WHEN** a macro runs `flare_cmd.py BL:T:30:6000`
- **THEN** the script blocks until `EV:BL:LOCKED` or `EV:BL:PRIME_BOUND`
- **AND** the following G-code move begins only after the buffer is at the
  armed rail

#### Scenario: BL timeout fails the macro
- **WHEN** the firmware emits `EV:BL:TIMEOUT` while the script waits
- **THEN** the script exits non-zero
- **AND** the Klipper macro surfaces the failure

#### Scenario: BS blocks until stabilization completes
- **WHEN** a macro runs `flare_cmd.py BS`
- **THEN** the script blocks until `EV:BUF_STAB:DONE`
- **AND** no dwell is required to cover the settle

#### Scenario: Stabilize failure is not silently absorbed
- **WHEN** the firmware emits `EV:BUF_STAB:TIMEOUT` or
  `EV:BUF_STAB:STAGNANT_TIMEOUT`
- **THEN** the script exits non-zero
