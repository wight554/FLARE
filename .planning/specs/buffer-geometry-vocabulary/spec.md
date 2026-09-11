# buffer-geometry-vocabulary Specification

## Purpose
Defines shared buffer geometry terms so firmware, docs, and tuning guides use the same distance and switch-span vocabulary.

## Requirements
### Requirement: Buffer geometry vocabulary
Buffer geometry SHALL be configured through exactly two full-range tunables
with semantics aligned to Happy Hare / EMU Sync:

- `buf_switch_span_mm` — FULL filament travel between COMPRESSION switch trip
  and TENSION switch trip (switch-active sensing span).
- `buf_max_travel_mm` — FULL mechanical buffer travel (total stroke, including
  overtravel past switches).

Both keys SHALL appear in `config.ini` (and `config.ini.example` if it
exists) and flow to firmware via generated `CONF_*` macros. The legacy keys
`buf_half_travel_mm` and `buf_size_mm` SHALL NOT exist after this change.

#### Scenario: Config keys present and consumed
- **WHEN** firmware is generated from `config.ini`
- **THEN** `buf_switch_span_mm` and `buf_max_travel_mm` are represented in
  `config.ini`
- **AND** consumed in firmware via generated `CONF_*` macros
- **AND** no `buf_half_travel_mm` / `buf_size_mm` / `CONF_BUF_HALF_TRAVEL_MM`
  / `CONF_BUF_SIZE_MM` identifier remains in firmware or config

#### Scenario: Stale pre-rename config rejected
- **WHEN** `config.ini` still uses the legacy `buf_half_travel_mm` or
  `buf_size_mm` keys
- **THEN** config generation fails with an error naming the unknown key
- **AND** the build does not silently fall back to defaults

### Requirement: Full-range to internal-half conversion
The firmware SHALL convert the full-range `buf_switch_span_mm` to the
internal half-based geometry exactly once at the value-ingest boundary
(config ingest and the serial SET handler) as
`half = buf_switch_span_mm / 2`, and SHALL NOT apply the conversion anywhere
else. `buf_max_travel_mm` SHALL map 1:1 to the internal total-travel value
with no unit conversion. Internal `sync.c` geometry remains half-based;
only the *source* of the half value changes.

#### Scenario: Ingest conversion is single and correct
- **WHEN** `buf_switch_span_mm = 10` is ingested
- **THEN** the internal half-travel value used by `buf_threshold_mm()` /
  `buf_physical_half_travel_mm()` is `5`
- **AND** the type-D relay trace is behaviourally identical to a pre-rename
  build configured with internal half `= 5`

#### Scenario: Internal geometry math unchanged
- **WHEN** the conversion is applied
- **THEN** no half-based formula or call graph in `sync.c`
  (`buf_threshold_mm`, `buf_physical_half_travel_mm`, consumers, the relay
  law, virtual-position estimate, predictive timing, deadband) is modified
- **AND** the post-ingest invariant `1.0 ≤ half ≤ buf_max_travel_mm / 2`
  holds, identical to the pre-rename invariant

### Requirement: EMU Sync default values
The compiled defaults SHALL be the EMU Sync reference values:
`buf_switch_span_mm = 10` and `buf_max_travel_mm = 25`. These replace the
prior `buf_half_travel_mm = 7.8` (an untuned calibration artifact) and
`buf_size_mm = 22`.

#### Scenario: Fresh board uses EMU Sync defaults
- **WHEN** firmware boots with no valid saved settings
- **THEN** `buf_switch_span_mm` defaults to `10` (internal half `5`)
- **AND** `buf_max_travel_mm` defaults to `25`

### Requirement: Switch-span and max-travel clamp relationship
The two tunables SHALL preserve, in full-range terms, the clamp relationship
that existed in half-range terms. `buf_switch_span_mm` SHALL be clamped to
`[2.0, buf_max_travel_mm]`. `buf_max_travel_mm` SHALL be clamped to
`[10, 1000]`. Setting `buf_max_travel_mm` SHALL re-clamp `buf_switch_span_mm`
so the derived internal half never exceeds `buf_max_travel_mm / 2`.

#### Scenario: Switch-span clamped against max-travel
- **WHEN** a SET requests `buf_switch_span_mm` greater than the current
  `buf_max_travel_mm`
- **THEN** `buf_switch_span_mm` is clamped to `buf_max_travel_mm`

#### Scenario: Lowering max-travel re-clamps switch-span
- **WHEN** `buf_max_travel_mm` is set below the current `buf_switch_span_mm`
- **THEN** `buf_switch_span_mm` is reduced so the internal half stays
  `≤ buf_max_travel_mm / 2`

### Requirement: Serial vocabulary rename without aliases
The serial SET/GET tokens SHALL be `BUF_SWITCH_SPAN` and `BUF_MAX_TRAVEL`,
carrying full-range values. The legacy tokens `BUF_HALF_TRAVEL`,
`BUF_TRAVEL`, and `BUF_SIZE` SHALL be removed with no compatibility alias.

#### Scenario: New tokens round-trip full-range values
- **WHEN** `SET:BUF_SWITCH_SPAN:10` is issued and `GET BUF_SWITCH_SPAN` is read
- **THEN** the reply reports the full-range value `10`
- **AND** the internal half-travel in effect is `5`

#### Scenario: Legacy tokens rejected
- **WHEN** a client issues `SET:BUF_HALF_TRAVEL:...`, `SET:BUF_TRAVEL:...`,
  or `SET:BUF_SIZE:...`
- **THEN** the firmware rejects it as an unknown parameter (no silent alias)

### Requirement: Type-P analog parity preserved
The full→half ingest change SHALL NOT alter type-P (analog,
`BUF_SENSOR_TYPE != 0`) behavior. Only the *source* of the internal
half value changes; the analog consumers of
`buf_physical_half_travel_mm()` / `buf_threshold_mm()` SHALL behave
identically for an equivalent geometry.

#### Scenario: Analog path equivalent under equal geometry
- **WHEN** type-P firmware runs with `buf_switch_span_mm = 10`,
  `buf_max_travel_mm = 25`
- **THEN** its analog scaling and thresholds are identical to a pre-rename
  type-P build with internal half `= 5` and total travel `= 25`
