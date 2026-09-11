## ADDED Requirements

### Requirement: Buffer service commands preempt compatible buffer activity

`BS` SHALL cancel active sync, buffer lock, an existing buffer-stabilize drive,
and standalone lane commands before starting a fresh buffer stabilize, while
hard activities (`TC`, cutter, manual unload) SHALL still reject with `ER:BUSY`.
`BL:T` and `BL:C` SHALL cancel an active buffer-stabilize drive before arming
buffer lock so tip-form macros can transition from neutralization to lock without
a racy delay.

#### Scenario: Buffer lock follows buffer stabilize

- **WHEN** `BS` has started buffer stabilization
- **AND** the host sends `BL:T`
- **THEN** the firmware cancels the stabilize drive
- **AND** arms the TENSION buffer lock with `OK`

#### Scenario: Hard activity remains busy

- **WHEN** toolchange, cutter, or manual unload is active
- **AND** the host sends `BS` or `BL:T`
- **THEN** the firmware returns `ER:BUSY`
