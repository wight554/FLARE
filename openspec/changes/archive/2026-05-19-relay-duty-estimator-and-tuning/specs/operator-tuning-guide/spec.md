## ADDED Requirements

### Requirement: Type-D relay-law tuning section

TUNING.md SHALL include a section for Sync-Feedback Sensor type D relay mode
(`BUF_SENSOR_TYPE == 0`, D=0) covering: the relay config keys (catch-up,
NEUTRAL, estimator bounds, confidence), the relay capture/analyze loop via
`flare_analyze`, and the runtime duty estimator (estimate vs fallback,
how to read the confidence telemetry). It SHALL state that relay knobs are
config-driven (config→flash/`SET:`), not compile-time.

#### Scenario: Relay user can tune without source edits

- **WHEN** a type-D standalone operator follows TUNING.md
- **THEN** they tune the relay via documented `config.ini` keys and the
  relay analyze loop, with no instruction to edit firmware `#define`s

### Requirement: Documented status tokens match firmware emission

The status-field reference in TUNING.md SHALL match the tokens the
firmware actually emits. The stale `TB` token SHALL be corrected to `CB`
(firmware emits `CB:`; the buffer-state rename mapped `TB→CB`).

#### Scenario: TB corrected to CB

- **WHEN** an operator reads the TUNING.md status-field list
- **THEN** the compression-bias field is documented as `CB`, matching the
  firmware status line

### Requirement: Happy Hare polarity inversion is recorded

Documentation SHALL prominently record, where the Happy Hare analog
reference is cited (the `audit-sync-polarity` D4 reference), that Happy
Hare uses `+1 = compression / -1 = tension`, the inverse of FLARE
(`+1 = tension / -1 = compression`), so any analog port from Happy Hare
MUST flip every sign. It SHALL cross-link `relay-buffer-control-2switch`
task 7.3.

#### Scenario: Analog porter is warned about the sign flip

- **WHEN** someone consults the analog reference to port Happy Hare logic
- **THEN** the documented note states the polarity is inverted and every
  ported sign must be flipped, with a cross-link to the analog-debt task
