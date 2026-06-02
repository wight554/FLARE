# config-surface-tiers Specification

## Purpose
TBD - created by archiving change tier-config-surface. Update Purpose after archive.
## Requirements
### Requirement: Configuration parameters are classified by tier

Every configuration parameter SHALL belong to exactly one tier, and its storage
and exposure SHALL follow that tier:

- **T0 board constant** — pins, sense resistor, lane count. Lives in
  `firmware/include/config.h`. Not in `config.ini`, not persisted, not settable.
- **T1 hardware limit** — physical motor/driver/wiring facts (a wrong value
  mis-drives the mechanism). Full path: `config.ini` → `gen_config.py` →
  `CONF_*` → `settings_t` → `SET:`/`GET:`.
- **T2 durable tunable** — this-build geometry and operator/print taste with a
  documented tuning procedure. Same full path as T1.
- **T3 internal constant** — control-loop constants tuned once against the
  algorithm, with no documented operator procedure. Lives as a compile-time
  constant — a `tune_internal.h` `#define` (unit-independent values) or, where a
  value requires the motor's `mm_per_step` conversion, a `gen_config`-emitted
  `tune.h` `CONF_*`. NOT in `config.ini.example`, NOT persisted, NOT in the
  release `SET:`/`GET:` surface.

#### Scenario: A parameter without an operator procedure is T3

- **WHEN** a parameter has no documented operator tuning procedure (TUNING.md,
  live tuner, or analyzer) and is not a board/hardware/geometry fact
- **THEN** it is classified T3 and lives in the owning source module, not in
  `config.ini` or `settings_t`

#### Scenario: A parameter with a documented procedure stays durable

- **WHEN** TUNING.md or a FLARE tool documents how and why an operator changes a
  parameter
- **THEN** it is T1 or T2 and retains the full config/persist/SET/GET path

### Requirement: Internal constants live in code, not the runtime surface

A T3 internal constant SHALL NOT appear in `config.ini.example`, in
`settings_t`, or in the release-build `SET:` / `GET:` handlers; a
unit-independent T3 constant additionally SHALL live in `tune_internal.h` and
not in `gen_config.py` `DEFAULTS`. Changing a T3 constant SHALL be a source edit
+ recompile and SHALL NOT require a `SETTINGS_VERSION` bump.

#### Scenario: Tweaking a T3 constant needs no version bump

- **WHEN** a firmware author changes a T3 constant's value
- **THEN** the change is a one-line source edit + rebuild
- **AND** `SETTINGS_VERSION` is unaffected and persisted operator settings survive

#### Scenario: A T3 key is rejected from the release SET surface

- **WHEN** a release build receives `SET:<T3_KEY>:<value>`
- **THEN** it replies `ER:SET:UNKNOWN_PARAM`

### Requirement: A dev build may expose T3 constants as ephemeral overrides

A `FLARE_DEV_TUNING` build flag SHALL gate optional re-exposure of T3 constants
as `SET:`-only, non-persisted runtime overrides for bench experimentation. The
flag SHALL be undefined in release builds, and a dev override SHALL NOT survive a
reboot.

#### Scenario: Dev override reverts on reboot

- **WHEN** a `FLARE_DEV_TUNING` build receives `SET:<T3_KEY>:<value>` and then
  reboots
- **THEN** the value takes effect until reboot
- **AND** after reboot the constant returns to its compiled value (no
  `settings_t` field exists to persist it)

### Requirement: Demoted keys are migrated gracefully

A demoted (T3) parameter SHALL migrate gracefully: an existing `config.ini` that
still sets it SHALL build with a warning rather than a hard error, and the device
config dump SHALL NOT emit the demoted key.

#### Scenario: Stale config key warns, does not abort

- **WHEN** `gen_config.py` reads a `config.ini` containing a demoted key
- **THEN** it warns to stderr and ignores the key
- **AND** does not `sys.exit(1)`

#### Scenario: Config dump omits demoted keys

- **WHEN** `flare_cmd.py --config` dumps a device configuration
- **THEN** the output contains no demoted (T3) key
- **AND** the dumped config rebuilds without warnings about demoted keys

