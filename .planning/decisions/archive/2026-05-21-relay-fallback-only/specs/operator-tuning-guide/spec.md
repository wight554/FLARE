## ADDED Requirements

### Requirement: TUNING.md relay section is fallback-only

The TUNING.md relay content SHALL describe only the fallback relay law
(`relay_catchup_frac`, `relay_neutral_frac`), the deep-COMPRESSION
collapse-ramp keys, and the `relay_min_flip_mm` 0.0/deadlock caveat. It
SHALL NOT document a relay duty estimator, a confidence gate, an offline
relay capture/analyze/apply loop, or the bimodal duty-ratchet note —
that machinery no longer exists.

#### Scenario: Operator reads the relay section post-removal

- **WHEN** an operator opens the TUNING.md relay section
- **THEN** it covers only the fallback law + collapse-ramp +
  `relay_min_flip_mm` caveat, with no estimator/confidence-gate/offline
  duty-analyzer instructions

#### Scenario: No dangling references

- **WHEN** TUNING.md / MANUAL.md are searched for `RDE` or the relay
  duty estimator after this change
- **THEN** there are no references to the removed estimator, telemetry,
  or removed config keys
