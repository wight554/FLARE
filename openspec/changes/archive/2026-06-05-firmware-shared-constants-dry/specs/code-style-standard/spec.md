## ADDED Requirements

### Requirement: Shared constants are single-definition

A numeric constant or small helper used by more than one translation unit SHALL be
defined once in a shared header, not copied per `.c`. Identical constants SHALL NOT
be redefined with divergent style (`#define` vs `static const`) across units.

#### Scenario: A constant is needed in multiple units

- **WHEN** a numeric constant (e.g. `MS_PER_SECOND_F`) is used in more than one `.c`
- **THEN** it is defined once in a shared header included by those units
- **AND** no per-`.c` duplicate definition remains

#### Scenario: A repeated idiom is factored

- **WHEN** the same small construction (e.g. the lane-digit string) appears in
  multiple units
- **THEN** it is provided as one shared helper and the call sites use it

### Requirement: Global-naming convention is documented

`STYLE.md` SHALL document the firmware global-naming convention so a reader can tell a
mutable tunable from internal state from a compile-time default by name alone.

#### Scenario: A contributor reads STYLE.md

- **WHEN** a contributor looks up the naming convention
- **THEN** `STYLE.md` states that `UPPER_CASE` = runtime-mutable config-backed tunable,
  `g_` = internal module/runtime state, and `CONF_*` = generated compile-time default
