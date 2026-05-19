## ADDED Requirements

### Requirement: Flip-heavy bimodal prints stay on the fallback driver

The type-D relay confidence-gate defaults SHALL be set so that an
ordinary flip-heavy bimodal print (alternating slow and fast features,
the never-COMPRESSION lean producing frequent TENSION↔NEUTRAL cycling)
does NOT satisfy the confidence gate, so NEUTRAL is driven by the
`extruder_est_sps` fallback rather than the confidence-gated duty
estimator. The relay control law (TENSION catch-up, COMPRESSION
`SYNC_MIN`, NEUTRAL demand-track) MUST be unchanged; only the
`relay_confidence_cycles` / `relay_confidence_window_ms` defaults change.

#### Scenario: Bimodal cube stays unconfident

- **WHEN** the 60×60 bimodal calibration cube is printed with the
  hardened defaults
- **THEN** the captured relay estimate-active fraction (`RDE`) is
  approximately 0% and NEUTRAL feed tracks the fallback

#### Scenario: Buffer no longer rides the empty wall

- **WHEN** the same bimodal print is captured steady-state (startup
  excluded) with the hardened defaults
- **THEN** `BPmax` stays within the switch span (does not peg the
  physical empty wall `buf_max_travel_mm/2`) and there is no persistent
  mid-print TENSION or COMPRESSION wall slam

#### Scenario: Genuine low-flip single-regime run can still confirm

- **WHEN** a sustained single-regime run produces stable paired duty
  cycles over the confidence window
- **THEN** the estimator MAY still reach confidence (the gate is
  hardened to exclude flip-heavy bimodal traffic, not to disable the
  estimator outright)

#### Scenario: SET/GET clamp ranges preserved

- **WHEN** the hardened defaults are generated into the tune header
- **THEN** they remain within the existing `RELAY_CONF_CYCLES` (1–64)
  and `RELAY_CONF_WINDOW_MS` (1000–300000) clamp ranges and the runtime
  `SET:`/`GET:` surface is unchanged

### Requirement: Anti-chatter distance hysteresis enabled by default

The relay `relay_min_flip_mm` distance-hysteresis flip guard SHALL ship
with a non-zero default, suppressing a buffer flip until commanded
filament travel since the last accepted flip reaches the configured
distance. A value of `0.0` MUST remain supported and behavior-identical
to the pre-change time-based flip handling (instant rollback via
config).

#### Scenario: Non-zero default damps residual flips

- **WHEN** the firmware boots with the shipped default and a type-D
  relay buffer
- **THEN** flips occurring before the configured commanded-travel
  distance has elapsed since the last accepted flip are held

#### Scenario: Zero disables the guard

- **WHEN** `relay_min_flip_mm` is set to `0.0`
- **THEN** flip handling is identical to the time-based behavior prior
  to this change

### Requirement: Collapse-ramp parameters are config-driven

The system SHALL expose the deep-COMPRESSION collapse-ramp parameters
(collapse delay, ramp multiplier, cap) as generated config keys rather
than compile-time constants. The shipped defaults MUST equal the prior
compile-time values so this change introduces zero collapse-ramp
behavior change.

#### Scenario: Defaults preserve current behavior

- **WHEN** the config keys are generated with their shipped defaults
- **THEN** the emitted values equal the prior constants
  (delay 250 ms, ramp multiplier 3, cap 600 ms) and runtime
  deep-COMPRESSION stop behavior is unchanged

#### Scenario: Operator can soften the deep stop

- **WHEN** an operator increases the collapse delay or lowers the ramp
  multiplier in config and regenerates
- **THEN** the firmware consumes the new values without a source edit

### Requirement: Hardware A/B validation against the locked baseline

The change SHALL be hardware-validated by an on-printer A/B against the
archived locked 4.2 relay baseline, across at least one slow-only model
and one fast/bimodal model, with the captured comparison metrics
recorded.

#### Scenario: Bimodal model passes

- **WHEN** the fast/bimodal cube is reprinted with the new defaults and
  analyzed
- **THEN** `BPmax` is off the physical empty wall (r6-class), TENSION is
  not persistent mid-print, and the cycle is shallow

#### Scenario: Slow-only model has no startup slam

- **WHEN** a slow-only (~300–600 mm/min) model is printed with the new
  defaults
- **THEN** there is no startup COMPRESSION wall slam and the print
  completes without fault
