## REMOVED Requirements

### Requirement: Bounded Half-Travel Prime
**Reason**: The `BUF_MAX_TRAVEL_MM / 2` cap predates the `BUF_GOAL`-parked idle
buffer position. `BS` parks a type-P buffer at `BUF_GOAL` (0.700 raw = `+0.40`
norm, compression side); the tension rail is then `0.65 x BUF_MAX_TRAVEL_MM`
away — more than half. Enforcing the half-travel cap would make `PRIME_BOUND`
the normal outcome and the rail unreachable. Replaced by "Bounded Prime Travel"
below, which adopts the full-travel bound already implemented in
`sync_buffer_lock_arm`.

**Migration**: None. The implemented cap is unchanged; only the contract is
corrected to match. Hosts relying on a prime never exceeding half travel must
instead rely on `EV:BL:PRIME_BOUND` and the locked-state watchdog.

## ADDED Requirements

### Requirement: Bounded Prime Travel
On `BL` the firmware SHALL drive the active lane toward the requested extreme
and stop as soon as either the corresponding buffer state (`BUF_TENSION` or
`BUF_COMPRESSION`) is reached or `BUF_MAX_TRAVEL_MM` mm of MMU travel is
completed, whichever comes first. The prime MUST NOT exceed `BUF_MAX_TRAVEL_MM`
of travel, which is the buffer's full physical span and therefore a hard
mechanical bound.

#### Scenario: Prime reaches the armed extreme first
- **WHEN** `BL:T` is armed
- **AND** the buffer reaches `BUF_TENSION` before `BUF_MAX_TRAVEL_MM` of travel
- **THEN** the lane stops at that point
- **AND** the lifecycle enters the locked sub-state

#### Scenario: Prime hits travel bound first
- **WHEN** `BL:T` is armed
- **AND** the MMU has traveled `BUF_MAX_TRAVEL_MM` without reaching
  `BUF_TENSION`
- **THEN** the lane stops
- **AND** `EV:BL:PRIME_BOUND` is emitted
- **AND** the lifecycle enters the locked sub-state at the bounded endpoint

### Requirement: Prime Rate And Type-P Rail Prediction
The firmware SHALL prime both sensor types at `SYNC_MAX_SPS`. For type-P
(analog) buffers the firmware SHALL evaluate the reached-extreme test against a
predicted position `g_buf_pos + BL_PRIME_PREDICT_LEAD_S * g_vel_norm_f` rather
than the instantaneous position, so the prime stops before the filtered signal
crosses `PSF_HOME_THRESHOLD_NORM`. For type-D (switch) buffers the reached test
SHALL remain the raw switch state, which stops the prime on the click with no
prediction.

`BL_PRIME_PREDICT_LEAD_S` is a firmware compile-time constant, not a runtime
tunable, and MUST NOT be exposed through `SET`/`GET` or `config.ini`.

#### Scenario: Type-P prime stops short of the rail
- **WHEN** `BL:T` is armed on a type-P buffer
- **AND** the buffer approaches the tension rail at the primed rate
- **THEN** the lane stops when the predicted position crosses
  `PSF_HOME_THRESHOLD_NORM`
- **AND** the buffer does not slam the mechanical tension extreme

#### Scenario: Type-D prime unchanged
- **WHEN** `BL:T` is armed on a type-D buffer
- **THEN** the prime rate is `SYNC_MAX_SPS`
- **AND** the prime stops on the raw `BUF_TENSION` switch state with no
  predictive lead applied

### Requirement: Host-Settable Locked-State Timeout
The `BL` command SHALL accept an optional trailing `timeout_ms` field,
`BL:<state>:<follow_mm>:<follow_rate>:<timeout_ms>`, setting the locked-state
watchdog window for that arm only. When the field is absent the firmware SHALL
use `BL_WATCHDOG_DEFAULT_MS` (30 seconds), so every existing 2- and 3-field
caller is unchanged. A malformed or negative `timeout_ms` SHALL be rejected with
`ER:` rather than silently defaulted.

The value SHALL be per-arm and MUST NOT be persisted to flash or exposed as a
`SET`/`GET` runtime parameter: a host that wraps a long macro needs a longer
window for that operation only, not globally.

#### Scenario: Host widens the window for a wrapped macro
- **WHEN** the host sends `BL:T:40:5000:120000`
- **THEN** the locked-state watchdog fires 120 seconds after the lock is
  established, not 30

#### Scenario: Omitted field keeps the default
- **WHEN** the host sends `BL:T` or `BL:T:40:5000`
- **THEN** the locked-state watchdog uses `BL_WATCHDOG_DEFAULT_MS`

#### Scenario: Malformed timeout is rejected
- **WHEN** the host sends `BL:T:40:5000:abc` or a negative timeout
- **THEN** the firmware replies `ER:`
- **AND** no lock is armed

## MODIFIED Requirements

### Requirement: Lock-Break On External Force
The firmware SHALL treat any departure of the buffer state from the locked
extreme as a non-MMU (external) force lock-break and MUST transition to the
catch sub-state on the first raw edge, without waiting for the `BUF_HYST_MS`
debounce window. The firmware SHALL emit `EV:BL:BREAK` on that edge, and
`EV:BL:FOLLOW` when the catch begins driving the lane.

#### Scenario: Extruder retract breaks the lock
- **WHEN** the lane is locked at `BUF_TENSION`
- **AND** the printer-side extruder retracts, advancing the buffer toward
  neutral
- **AND** the buffer state leaves `BUF_TENSION`
- **THEN** the lifecycle enters the catch sub-state on the same firmware
  tick
- **AND** `EV:BL:BREAK` is emitted

#### Scenario: Catch start is separately observable
- **WHEN** the catch sub-state begins driving the lane
- **THEN** `EV:BL:FOLLOW` is emitted after `EV:BL:BREAK`

### Requirement: Instant-Slam Catch With Asymmetric Safety
On lock-break the firmware SHALL drive the active lane in the mirror direction
(retract for `BL:T` break, feed for `BL:C` break) under a rate servo bounded by
`GLOBAL_MAX_SPS`. The commanded rate SHALL be
`seed + (GLOBAL_MAX_SPS - seed) * clamp(err / BL_CATCH_ERR_SPAN_NORM, 0, 1)`,
where `seed` is the host-supplied follow rate converted to sps (or the firmware
default when the host supplied none) and `err` is the normalized distance of the
buffer position from the armed rail. The rate SHALL be approached through the
`SYNC_RAMP_UP_SPS` / `RAMP_STEP_SPS` pull-in ramp; the firmware MUST NOT issue
an instant `current_sps = target` write, which stalls the stepper at its pull-in
limit. The catch MUST tolerate transient over-drive back toward the armed
extreme as a safe recoverable direction and SHALL NOT throttle the catch to
avoid it. The rate servo SHALL apply to type-P buffers only; type-D buffers,
which have no mid-band position signal, SHALL hold the seed rate.

The host follow rate is a floor and seed, not a ceiling: the servo MAY escalate
above it up to `GLOBAL_MAX_SPS`, and SHALL NOT command below it while the lock
is broken.

#### Scenario: Tension-armed catch escalates under a fast external retract
- **WHEN** the lane was locked at `BUF_TENSION` and the lock is broken
- **AND** the external retract drives the buffer away from the tension rail
- **THEN** the active lane drives in the retract direction
- **AND** the commanded rate escalates toward `GLOBAL_MAX_SPS` in proportion to
  the buffer error, through the pull-in ramp

#### Scenario: Slow external retract holds the seed rate
- **WHEN** the catch is active
- **AND** the buffer stays close to the armed rail
- **THEN** the commanded rate remains at the host-supplied seed rate

#### Scenario: Over-drive back to tension is permitted
- **WHEN** the catch is active and the buffer briefly re-enters
  `BUF_TENSION`
- **THEN** the catch does not fault and does not throttle
- **AND** the lane continues mirroring until release

#### Scenario: Type-D catch is not servo'd
- **WHEN** the catch is active on a type-D buffer
- **THEN** the commanded rate is the seed rate with no error-proportional
  escalation

### Requirement: Locked Hold Contract
While locked the firmware SHALL energize the active lane motor with zero
commanded velocity, MUST NOT issue any closed-loop feed corrections from
the buffer state, and SHALL preserve estimator, drift observer, sigma,
confidence, and reserve integrator state. A `BL` armed without a host follow
distance SHALL default its catch distance budget to `BUF_MAX_TRAVEL_MM`, so a
bare `BL:T` retains catch authority against an external force rather than
holding passively.

#### Scenario: Lock holds against buffer spring
- **WHEN** the lane is locked at `BUF_TENSION`
- **AND** the printer/extruder is idle
- **THEN** the MMU motor remains energized at zero feed
- **AND** the buffer state remains `BUF_TENSION`

#### Scenario: Lock preserves controller learning
- **WHEN** the lane is locked
- **THEN** estimator, drift, sigma, confidence, and reserve integrator
  values are not reset

#### Scenario: Bare BL:T still catches
- **WHEN** `BL:T` is armed with no follow distance
- **AND** an external retract breaks the lock
- **THEN** the catch engages with a distance budget of `BUF_MAX_TRAVEL_MM`
