## ADDED Requirements

### Requirement: Relay NEUTRAL feed is a bounded duty-cycle estimate

The `BUF_NEUTRAL` relay feed target SHALL be derived from a runtime
duty-cycle estimator computed from the cadence of TENSION↔COMPRESSION
switch flips in Sync-Feedback Sensor type D relay mode
(`BUF_SENSOR_TYPE == 0`, D=0), not from a fixed hand-tuned multiplier. The
estimate SHALL be clamped to an
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

#### Scenario: Fallback clamp matches the archived relay law

- **WHEN** relay NEUTRAL is on the unconfident fallback (or the
  cold-start seed) path
- **THEN** the feed is `extruder_est_sps × SYNC_RELAY_NEUTRAL_FRAC`
  clamped only to `[SYNC_MIN_SPS, relay_base]`, and SHALL NOT be clamped
  to the estimator `[lo, hi]` bounds
- **AND** this is byte-for-byte the archived `relay-buffer-control-2switch`
  round-2 behavior, so a major upward speed step is absorbed with full
  `relay_base` headroom (no `[lo,hi]`-induced underfeed / TENSION flip)

#### Scenario: Offline bounds gate only the confident estimator

- **WHEN** the estimator is confident and drives NEUTRAL
- **THEN** the `[lo, hi]` clamp applies to `v_est`
- **AND** the `[lo, hi]` clamp is never applied to the unconfident
  fallback or seed path

### Requirement: Estimator is a recovery arbitrator, not the steady driver

In a good low-flip cycle the estimator SHALL be expected to remain
unconfident, and that state SHALL be treated as the normal operating
point, not a fault. The bounded fixed-demand fallback is the intended
steady-state driver (it is the locked known-good baseline); the estimator
gains signal only from switch flips, which occur during demand
disturbances, so it SHALL act to re-find demand during recovery and decay
back to the fallback as flips cease.

#### Scenario: Quiet steady state stays on fallback by design

- **WHEN** the relay cycle is steady with very few or no switch flips
- **THEN** the estimator remains unconfident and the bounded fallback
  drives NEUTRAL
- **AND** no fault, warning, or instability is raised for the unconfident
  state

#### Scenario: Disturbance engages the estimator

- **WHEN** a demand disturbance causes repeated switch flips
- **THEN** the estimator accrues paired duty cycles and, once confident,
  arbitrates the NEUTRAL feed during recovery
- **AND** returns to the fallback as flip activity subsides

### Requirement: Cold-start fallback is offline-seeded

The relay NEUTRAL cold-start fallback SHALL be seeded from an
offline-provided relay baseline for a warmup window at print start / boot,
instead of a cold `extruder_est_sps`. The seed source is the recommended
`relay_base`, the `[lo, hi]` midpoint, or a dedicated offline-provided
seed key. The seed SHALL be offline-provided and SHALL NOT be persisted by
the firmware.

#### Scenario: Startup uses the offline seed, not cold EST

- **WHEN** a relay print begins and `extruder_est_sps` has not warmed
- **THEN** the NEUTRAL fallback feed is computed from the offline relay
  baseline seed, not a cold `extruder_est_sps`
- **AND** the warmup window exits on EST-warm or first estimator
  confidence, whichever comes first

#### Scenario: Seed is never persisted

- **WHEN** the cold-start seed is applied
- **THEN** no flash write occurs and the seed value originates only from
  offline-provided config

### Requirement: End-of-print COMPRESSION grind is an accepted limitation

This change SHALL NOT modify the `BUF_COMPRESSION → SYNC_MIN_SPS` branch.
The end-of-print COMPRESSION-floor grind observed in the 4.2 round-2
baseline is explicitly out of scope and SHALL be recorded as an accepted
limitation (print-tail only, draw≈0, already arrested by the existing
RELIEF_PAUSE / BUF_STAB auto-stop, no print-quality impact), cross-linked
rather than silently dropped.

#### Scenario: COMPRESSION branch untouched

- **WHEN** the buffer is in `BUF_COMPRESSION`
- **THEN** the target remains `SYNC_MIN_SPS` exactly as in the archived
  `relay-buffer-control-2switch` relay law
- **AND** the accepted-limitation note is present in the change docs/spec

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

### Requirement: Happy Hare relief-fraction snap remains reference-only

The Happy Hare relief-fraction snap SHALL be recorded as a formulation
reference only. FLARE SHALL NOT ship blind analog changes from that reference
without the deferred analog rig (`pending-analog-rig` /
`relay-buffer-control-2switch` task 7.3). Any future analog port MUST also
honor the Happy Hare polarity inversion (`+1 = compression` in HH,
`+1 = tension` in FLARE).

#### Scenario: No blind analog snap port

- **WHEN** this relay estimator change is implemented
- **THEN** the type-P analog path remains behavior-identical
- **AND** the Happy Hare relief-fraction snap is present only as a documented
  reference note
