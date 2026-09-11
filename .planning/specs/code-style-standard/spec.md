# code-style-standard Specification

## Purpose
Enforced C style + lint contract for FLARE firmware so a human maintainer can read and
safely change it: format/lint tooling, naming, file/function size, magic-number + DRY
rules, doc/comprehension comments, and the behavior-preserving constraint on refactors.
## Requirements
### Requirement: Enforced format configuration

Repo SHALL carry `.clang-format`, `.clang-tidy`, and `.editorconfig` at root, and
all firmware C sources (`firmware/src/*.c`, `firmware/include/*.h`) SHALL conform.

#### Scenario: Source conforms to format

- **WHEN** `clang-format --dry-run -Werror` runs over `firmware/src` and `firmware/include`
- **THEN** it exits zero with no reformatting required

#### Scenario: Format config is present

- **WHEN** a contributor clones the repo
- **THEN** `.clang-format`, `.clang-tidy`, and `.editorconfig` exist at root
- **AND** `.clang-format` derives from the LLVM base style with project overrides

### Requirement: Local lint invocation

`STYLE.md` SHALL document the local `clang-format` and `clang-tidy` invocation, and
the `clang-tidy` config SHALL enable the project check set. No CI lint gate is
required in this change.

#### Scenario: Contributor lints locally

- **WHEN** a contributor reads `STYLE.md`
- **THEN** it gives the exact `clang-format --dry-run -Werror` and `clang-tidy`
  commands over `firmware/src` and `firmware/include`

#### Scenario: Naming check enabled

- **WHEN** the `clang-tidy` config is read
- **THEN** `readability-identifier-naming` is enabled with the project naming scheme
- **AND** magic-number, function-size, and bugprone check families are enabled

### Requirement: Naming conventions

Identifiers SHALL be intention-revealing per `STYLE.md`. Domain vocabulary terms
(`sps`, `mm`, `tmc`, `buf`, `psf`, `adc`, `pio`) MAY remain abbreviated and SHALL
be defined in `STYLE.md`. Single-letter and opaque identifiers SHALL NOT be used
for variables with non-trivial scope.

#### Scenario: A cryptic identifier is encountered

- **WHEN** a refactor touches an identifier like `A`, `L`, `m`, or `din_t`
- **THEN** it is renamed to an intention-revealing name (`lane`, `motor`,
  `debounced_input_t`)
- **AND** the rename preserves behavior with no logic edit in the same commit

#### Scenario: A domain abbreviation is used

- **WHEN** code uses a documented domain term such as `sps` or `mm`
- **THEN** the abbreviation is retained
- **AND** `STYLE.md` defines its meaning

### Requirement: File and function size norms

`STYLE.md` SHALL state translation-unit and function size norms, and oversized
units SHALL be split into cohesive modules and oversized functions extracted.

#### Scenario: A translation unit exceeds the norm

- **WHEN** a `.c` file substantially exceeds the documented size norm
- **THEN** it is split into cohesive translation units that keep one owner per
  domain boundary
- **AND** the split commit changes no behavior and passes the build

### Requirement: Magic-number policy

Non-trivial numeric literals in firmware SHALL be replaced by named constants or
documented tunables; values that are runtime-tunable SHALL follow the existing
`config.ini` → `tune.h` → `CONF_*` path.

#### Scenario: A bare numeric literal is found

- **WHEN** a refactor encounters an unexplained numeric literal in control logic
- **THEN** it is replaced by a named constant with an explanatory comment, or a
  config-backed tunable if runtime-adjustable

### Requirement: Comprehension comments and self-documenting structure

Each firmware `.c` SHALL carry a file-header doc-block stating what the unit owns,
its core algorithm, and a pointer to the relevant `BEHAVIOR.md`/spec section. Inline
comments SHALL explain why (intent, invariants, hardware quirks, edge cases), not
narrate obvious code. Every state machine SHALL carry a state-transition map comment.

#### Scenario: A contributor opens an unfamiliar module

- **WHEN** a new contributor opens any `firmware/src/*.c`
- **THEN** the file-header doc-block states what the unit owns, its core algorithm,
  and where the behavior is documented

#### Scenario: A state machine is read

- **WHEN** a contributor reads a state machine such as `tc_state_t` in `toolchange.c`
- **THEN** a transition-map comment lists the states, legal transitions, and the
  trigger for each edge

#### Scenario: An inline comment is added

- **WHEN** an inline comment is written
- **THEN** it explains intent, an invariant, a hardware quirk, or an edge case
- **AND** it does not narrate self-evident code

### Requirement: Doc-comment format and rationale preservation

`STYLE.md` SHALL define the function/struct/macro doc-comment format, and existing
rationale comments SHALL be preserved.

#### Scenario: A function is documented

- **WHEN** a non-trivial function is reformatted under the standard
- **THEN** its comment follows the `STYLE.md` doc-comment format
- **AND** existing tuning-history rationale comments are kept verbatim in meaning

### Requirement: Behavior-preserving refactor constraint

All overhaul edits SHALL be behavior-preserving: no serial protocol, config key,
tunable, or runtime-behavior change, and the build SHALL pass before every commit.

#### Scenario: A refactor commit is prepared

- **WHEN** a format, rename, or split commit is staged
- **THEN** `ninja -C build_local` passes
- **AND** the serial protocol, `config.ini` keys, `tune.h` generation, and
  `settings_t` layout are unchanged

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

All firmware global variables SHALL be named `g_lower_case`, including config-backed
runtime tunables, and the `g_` prefix SHALL be enforced by `.clang-tidy` (no blanket
`GlobalVariableIgnoredRegexp` exemption). `STYLE.md` SHALL document this and SHALL state
that tunable-vs-state is distinguished by the `controller_shared.h` tunables section,
the `settings_t` mirror, and the `SET:`/`GET:` surface — not by casing. Protocol param
names and `config.ini` keys remain `UPPER_CASE` strings and are not affected by the
identifier naming.

#### Scenario: A contributor reads STYLE.md

- **WHEN** a contributor looks up the global-naming convention
- **THEN** `STYLE.md` states all globals (incl. tunables) use `g_lower_case`
- **AND** it states protocol/config names remain `UPPER_CASE` strings, and `CONF_*` are
  generated compile-time defaults

#### Scenario: A global tunable is declared

- **WHEN** a config-backed runtime tunable is declared in `controller_shared.h`
- **THEN** its C identifier is `g_lower_case`
- **AND** its `SET:`/`GET:` param string and `config.ini` key are unchanged
- **AND** `.clang-tidy` enforces the `g_` prefix with no blanket ignore regexp

