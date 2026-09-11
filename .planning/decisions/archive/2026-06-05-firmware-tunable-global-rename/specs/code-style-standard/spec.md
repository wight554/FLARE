## MODIFIED Requirements

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
