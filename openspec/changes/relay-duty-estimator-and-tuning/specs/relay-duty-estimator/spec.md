## ADDED Requirements

### Requirement: Relay NEUTRAL feed is a bounded duty-cycle estimate

The `BUF_NEUTRAL` relay feed target SHALL be derived from a runtime
duty-cycle estimator computed from the cadence of TENSION↔COMPRESSION
switch flips in 2-switch relay mode (`BUF_SENSOR_TYPE == 0`), not from a
fixed hand-tuned multiplier. The estimate SHALL be clamped to an
offline-provided `[lo, hi]` bound before use. The `BUF_TENSION` catch-up
and `BUF_COMPRESSION` stop branches SHALL be unchanged.

#### Scenario: Estimator drives NEUTRAL when confident

- **WHEN** relay mode is active, the buffer is in `BUF_NEUTRAL`, and the
  estimator has observed enough paired duty cycles within the recency
  window to be confident
- **THEN** the NEUTRAL feed target is the duty-weighted effective feed
  `v_est = (1 - fh)·v_low + fh·v_high` clamped to `[lo, hi]`
- **AND** the `BUF_TENSION` and `BUF_COMPRESSION` targets are unaffected

#### Scenario: Offline bounds clamp the estimate

- **WHEN** the computed `v_est` falls outside the offline-recommended
  `[lo, hi]` bounds
- **THEN** the NEUTRAL target is the clamped bound value, never the raw
  out-of-range estimate

### Requirement: Never-TENSION compression lean is preserved on top

The estimator SHALL replace only the demand guess. The existing
never-TENSION compression lean (the `SYNC_RELAY_NEUTRAL_FRAC` /
`SYNC_COMPRESSION_BIAS_FRAC` overfeed policy) SHALL be applied on top of
the estimate so the buffer continues to park between NEUTRAL and
COMPRESSION and never drifts to TENSION.

#### Scenario: Lean applied after estimate

- **WHEN** the estimator produces a demand-matched NEUTRAL feed
- **THEN** the compression-lean overfeed is applied to that estimate
  before the existing ramp/clamp
- **AND** the resulting steady-state buffer position is on the
  COMPRESSION side of NEUTRAL, never pinned at TENSION

### Requirement: Unconfident fallback to the proven fixed-demand path

Relay NEUTRAL SHALL fall back to the existing `extruder_est_sps ×
SYNC_RELAY_NEUTRAL_FRAC` behavior whenever the estimator is not confident
(insufficient or stale duty cycles, including boot), with no behavior
change versus `relay-buffer-control-2switch`.

#### Scenario: Boot uses fallback

- **WHEN** relay mode starts and no duty cycles have been observed yet
- **THEN** the NEUTRAL feed is the fixed `extruder_est_sps ×
  SYNC_RELAY_NEUTRAL_FRAC` path

#### Scenario: Stale cycles revert to fallback

- **WHEN** previously-confident duty cycles age past the recency window
- **THEN** the controller reverts to the fixed-demand fallback without a
  feed discontinuity that destabilizes the cycle

### Requirement: Estimate and confidence are observable

The runtime SHALL expose, via the status/telemetry protocol, whether the
NEUTRAL feed is currently the estimate or the fallback, and the estimator
confidence, so divergence is visible and not silent.

#### Scenario: Telemetry distinguishes estimate vs fallback

- **WHEN** an operator polls status during a relay print
- **THEN** the status line indicates estimate-vs-fallback state and a
  confidence value

### Requirement: Estimator state is volatile and never persisted

The runtime estimator SHALL NOT write to flash and SHALL NOT persist its
state across reboots. Persistence of recommended bounds/baseline is solely
the offline analyzer's responsibility.

#### Scenario: No flash write from the estimator

- **WHEN** the estimator updates its duty statistics during a print
- **THEN** no flash save is triggered by the estimator

### Requirement: Optional distance-hysteresis flip guard

The system SHALL provide an optional motion-based (distance) flip
hysteresis for relay state transitions alongside the existing time-based
`BUF_HYST_MS`, selectable by config, defaulting to the existing
time-based behavior so default behavior is unchanged.

#### Scenario: Default preserves time-based hysteresis

- **WHEN** no distance-hysteresis config is set
- **THEN** relay flip debounce uses the existing `BUF_HYST_MS` time-based
  behavior unchanged

#### Scenario: Distance hysteresis when enabled

- **WHEN** distance-hysteresis is configured with a minimum flip travel
- **THEN** a relay state flip is suppressed until at least that filament
  travel has accumulated since the last flip
