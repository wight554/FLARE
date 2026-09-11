## MODIFIED Requirements

### Requirement: Module ownership shall stay explicit

Each firmware module SHALL keep ownership aligned with the documented
architecture boundaries. A module MAY be split into multiple cohesive translation
units provided each unit keeps a single domain owner and the file map stays
documented.

#### Scenario: A change affects toolchange behavior

- **WHEN** a change modifies cutter, toolchange, or RELOAD state transitions
- **THEN** primary logic belongs in `firmware/src/toolchange.c`
- **AND** shared declarations belong in module headers or
  `firmware/include/controller_shared.h`
- **AND** unrelated modules are touched only for required integration points

#### Scenario: A module is split for readability

- **WHEN** an oversized translation unit is split into cohesive units
- **THEN** each new unit keeps one domain owner aligned with its module boundary
- **AND** the documented file map (`AGENTS.md` Key Files, `project-architecture`)
  is updated to list the new units
- **AND** the split changes no behavior

#### Scenario: Sync and protocol are split units

- **WHEN** contributors need sync or protocol ownership context
- **THEN** `firmware/src/sync.c` owns sync orchestration, buffer lock, and boot
  stabilization
- **AND** `firmware/src/sync_buf.c` owns buffer sensing, virtual position, signal
  publishing, and estimator updates
- **AND** `firmware/src/sync_relay.c` owns Type-D relay control and neutral feed
  sampling
- **AND** `firmware/src/sync_analog.c` owns Type-P analog helper/control metrics
- **AND** `firmware/src/protocol.c` owns command parsing, motion/system commands,
  and SET/GET dispatch
- **AND** `firmware/src/protocol_status.c` owns status dump formatting
- **AND** `firmware/src/protocol_tmc.c` owns advanced TMC serial commands
