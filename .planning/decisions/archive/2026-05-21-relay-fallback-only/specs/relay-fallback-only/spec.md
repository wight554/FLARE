## ADDED Requirements

### Requirement: Type-D relay NEUTRAL is unconditionally the fallback

For `BUF_SENSOR_TYPE == 0`, the NEUTRAL feed target SHALL always be
`clamp(extruder_est_sps, SYNC_MIN, relay_base) · RELAY_NEUTRAL_FRAC`.
There SHALL be no confidence-gated duty-estimator path, no `[lo,hi]`
estimate clamp, and no estimator/seed selection. The TENSION (catch-up)
and COMPRESSION (`SYNC_MIN`) branches SHALL be unchanged.

#### Scenario: NEUTRAL always uses the fallback

- **WHEN** the type-D relay is in NEUTRAL at any point in any print
- **THEN** the commanded feed is the `extruder_est_sps · neutral_frac`
  fallback (bounded `[SYNC_MIN, relay_base]`), identical to the
  pre-removal unconfident steady-state behavior

#### Scenario: Catch-up and stop branches preserved

- **WHEN** the buffer is TENSION or COMPRESSION
- **THEN** the relay still drives catch-up
  (`relay_base · RELAY_CATCHUP_FRAC`) and `SYNC_MIN` respectively,
  byte-identical to before this change

### Requirement: Firmware drops the duty-estimator machinery

The firmware SHALL NOT contain the relay duty-estimator state, the
`v_est` blend, the confidence gate, the pair-history/travel
accumulators, or the cold-start estimate seed. No code path may
reference the deleted estimator state.

#### Scenario: Build has no estimator references

- **WHEN** the firmware is built after this change
- **THEN** it compiles and links with no remaining duty-estimator /
  confidence-gate symbols, and sync still auto-arms (status `SM:1`) on a
  print

### Requirement: Protocol drops estimator telemetry fields

The device protocol and `flare_cmd.py --dump` SHALL NOT expose the
`RDE`, `RDCF`, or `RDV` fields or any estimate/confidence `SET:`/`GET:`
parameters. Unrelated status fields (`BUF`, `BP`, `EST`, `NC`, …) SHALL
be unchanged.

#### Scenario: Status string no longer carries estimator fields

- **WHEN** `?:` status is read after this change
- **THEN** `RDE`/`RDCF`/`RDV` are absent and all other fields are
  present and unchanged

### Requirement: Analyzer emits no relay duty recommendations

`flare_analyze.py` SHALL NOT compute or emit relay duty-cycle
recommendations (`relay_estimate_lo`/`relay_estimate_hi`/
`relay_seed_rate`) or a relay coverage verdict. All non-relay analyzer
output (`baseline_rate`, flow schedule, acceptance gate, verdicts) SHALL
be byte-identical to before this change on existing non-relay inputs.

#### Scenario: Non-relay parity preserved

- **WHEN** the analyzer runs on existing non-relay fixtures/inputs
- **THEN** the emitted patch and verdicts are byte-identical to the
  pre-change output

#### Scenario: No relay duty section emitted

- **WHEN** the analyzer runs on a type-D relay capture
- **THEN** it emits no `relay_estimate_*` / `relay_seed_rate` lines and
  no relay coverage verdict

### Requirement: Config surface drops the dead relay keys

Config, the generator, and persisted settings SHALL NOT define
`relay_estimate_lo`, `relay_estimate_hi`, `relay_confidence_cycles`,
`relay_confidence_window_ms`, or `relay_seed_warmup_ms`, and
`SETTINGS_VERSION` SHALL be bumped. `relay_catchup_frac`,
`relay_neutral_frac`, `relay_min_flip_mm`, and the `relay_collapse_*`
keys SHALL be retained with unchanged behavior.

#### Scenario: Generator omits removed keys

- **WHEN** `gen_config.py` produces the tune header
- **THEN** no `CONF_RELAY_ESTIMATE_*` / `CONF_RELAY_CONFIDENCE_*` /
  `CONF_RELAY_SEED_WARMUP_MS` macros are emitted, and the retained relay
  keys are unchanged

#### Scenario: Persisted settings reset cleanly

- **WHEN** firmware with the bumped `SETTINGS_VERSION` boots over older
  persisted settings
- **THEN** settings reset to defaults (which equal the fallback
  behavior) without fault
