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
