# Sync State Model Specification

## Purpose
Durable contract for the explicit sync controller lifecycle state machine
(`SYNC_OFF`, `SYNC_ACTIVE`, `SYNC_HOLD`, `SYNC_RELIEF_PAUSE`,
`SYNC_FAULT_HOLD`) and its non-destructive relief/fault behavior, replacing
ad-hoc destructive disable.
## Requirements
### Requirement: Explicit Sync Lifecycle States
The sync controller SHALL maintain a single explicit state among
`SYNC_OFF`, `SYNC_ACTIVE`, `SYNC_HOLD`, `SYNC_RELIEF_PAUSE`, and
`SYNC_FAULT_HOLD`. All sync lifecycle behavior SHALL be derived from this
state rather than independent ad-hoc flags.

#### Scenario: Single source of truth
- **WHEN** the sync controller changes lifecycle behavior
- **THEN** the change is the result of a defined transition between the five
  states
- **AND** no lifecycle decision contradicts the current state

#### Scenario: True off remains destructive
- **WHEN** the controller enters `SYNC_OFF` (toolchange, unload, manual,
  tail-assist finished, host TS)
- **THEN** the estimator, drift observer, sigma/confidence, and integrators
  are reset exactly as the prior `sync_disable(true)` behavior

### Requirement: Non-Destructive Relief Pause
The controller SHALL enter `SYNC_RELIEF_PAUSE` instead of destructive disable
on a sustained compression/overfull condition, preserving the extruder
estimator, drift observer, sigma/confidence, and reserve integrator.

#### Scenario: Enter relief pause without losing state
- **WHEN** the continuous-compression terminal condition is reached (previously
  `sync_disable(true)` at the auto-stop path)
- **THEN** the controller enters `SYNC_RELIEF_PAUSE`
- **AND** local assist is reduced to zero/floor
- **AND** estimator, drift, sigma, and integrator values are retained

#### Scenario: Resume on TENSION re-arm
- **WHEN** the buffer reaches `BUF_TENSION` while in `SYNC_RELIEF_PAUSE`
- **THEN** the controller returns to `SYNC_ACTIVE`
- **AND** it reuses the preserved estimator instead of cold bootstrap unless
  the estimate is stale per `SYNC_EST_FRESH_MS`

#### Scenario: TENSION is never paused
- **WHEN** the buffer is in `BUF_TENSION`
- **THEN** the controller SHALL NOT enter `SYNC_RELIEF_PAUSE`

### Requirement: Fault Hold With Autonomous Recovery
The controller SHALL enter `SYNC_FAULT_HOLD` instead of destructive disable on
a hard-wall / jam condition, stopping the motor while preserving controller
state, and SHALL recover conservatively without host involvement.

#### Scenario: Hard wall enters fault hold
- **WHEN** the hard-wall critical condition is reached (previously
  `sync_disable(true)` at the hard-wall path)
- **THEN** the controller enters `SYNC_FAULT_HOLD`
- **AND** the motor is stopped
- **AND** estimator/drift/sigma/integrator state is preserved

#### Scenario: Standalone recovery
- **WHEN** `SYNC_FAULT_HOLD` has been stable for the configured recovery
  interval
- **THEN** the controller recovers conservatively without any host command

### Requirement: Host Hold Is Non-Destructive
The host `HD` HOLD command SHALL place the controller in `SYNC_HOLD`: closed
loop off, buffer-stabilize toward NEUTRAL permitted, controller state preserved,
learning paused. It SHALL NOT destructively reset estimator/drift/sigma.

#### Scenario: Host requests hold
- **WHEN** the host sends `HD` (sent only while paused/idle, extruder not
  pulling)
- **THEN** the controller enters `SYNC_HOLD`
- **AND** closed-loop sync stops while buffer-stabilize-to-NEUTRAL stays available
- **AND** estimator/drift/sigma state is preserved

#### Scenario: Host clears hold
- **WHEN** the host clears `HD`
- **THEN** the controller leaves `SYNC_HOLD` and resumes normal lifecycle

### Requirement: Full-Bias Invariant Preserved
The reserve/full-biased buffer target (between NEUTRAL and COMPRESSION) SHALL remain
owned exclusively by `SYNC_ACTIVE` control and SHALL be unchanged by this
state model. `SYNC_RELIEF_PAUSE` and `SYNC_FAULT_HOLD` SHALL NOT drain the
buffer below the reserve target by design.

#### Scenario: Active control output parity
- **WHEN** the controller is in `SYNC_ACTIVE`
- **THEN** reserve target, reserve correction, zone bias, soft-wall trim, and
  collapse ramp/cap behavior are equivalent to the pre-change controller

#### Scenario: Paused states do not under-bias
- **WHEN** the controller is in `SYNC_RELIEF_PAUSE` or `SYNC_FAULT_HOLD`
- **THEN** assist is zero/floor on an already-overfull buffer
- **AND** the buffer returns to the full-biased band only via `SYNC_ACTIVE`
  after re-arm

### Requirement: Creep Suppressed In Non-Active States
`neutral_creep` SHALL be active only in `SYNC_ACTIVE` and SHALL be suppressed in
`SYNC_HOLD`, `SYNC_RELIEF_PAUSE`, and `SYNC_FAULT_HOLD`.

#### Scenario: No creep while paused
- **WHEN** the controller is in `SYNC_HOLD`, `SYNC_RELIEF_PAUSE`, or
  `SYNC_FAULT_HOLD`
- **THEN** `neutral_creep` produces zero added rate

### Requirement: Sync-Feedback Sensor Taxonomy
Live sync docs and specs SHALL describe the buffer sensor as a
Sync-Feedback Sensor using Happy Hare type codes: `D` = Dual two-switch
sensor (`BUF_SENSOR_TYPE == 0`), `P` = Proportional analog sensor
(`BUF_SENSOR_TYPE == 1`), `TO` = Tension-Only (not implemented in FLARE),
and `CO` = Compression-Only (not implemented in FLARE). The sensor type
SHALL be named separately from the control law.

#### Scenario: Type code names the sensor
- **WHEN** a live doc or spec refers to the buffer sensor mode
- **THEN** it states the Sync-Feedback Sensor type and the `D=0` / `P=1`
  `BUF_SENSOR_TYPE` value contract

#### Scenario: Control law named separately
- **WHEN** the dual-switch path is described
- **THEN** type D names the sensor and "two-level / hysteretic relay"
  names the control law

### Requirement: Sync-Feedback Sensor taxonomy uses Happy Hare type codes

Documentation and live specs SHALL use the umbrella concept Sync-Feedback
Sensor with Happy Hare's canonical type codes: P (Proportional, analog),
D (Dual, two-switch 3-state), TO (Tension-Only), CO (Compression-Only).
New acronyms (DSF/SFS) MUST NOT be minted, and wiring shorthand or "relay"
MUST NOT be used to denote the sensor in live prose.

#### Scenario: Sensor referred to by HH type code

- **WHEN** a live spec or doc refers to the buffer sensor
- **THEN** it uses the Sync-Feedback Sensor term and a P/D/TO/CO code, not
  wiring shorthand, "relay", or an invented acronym

#### Scenario: Legacy analog alias retired

- **WHEN** the analog sensor is referenced in live prose or comments
- **THEN** it is called Happy Hare type P, not the legacy analog alias

### Requirement: BUF_SENSOR_TYPE value contract is documented

Every live reference to `BUF_SENSOR_TYPE` SHALL document the value
contract as `D = 0` and `P = 1`. The integer values MUST remain unchanged
by this change.

#### Scenario: Value contract stated at reference

- **WHEN** `BUF_SENSOR_TYPE` appears in a live spec, TUNING.md, or
  config.ini.example
- **THEN** the D=0 / P=1 mapping is stated or unambiguously linked

#### Scenario: Integer values unchanged

- **WHEN** the firmware is built after this change
- **THEN** `BUF_SENSOR_TYPE == 0` is still the dual-switch path and `== 1`
  the analog path, byte-identical behavior

### Requirement: TO and CO documented as recognized but unimplemented

Documentation SHALL list TO and CO as recognized Happy Hare Sync-Feedback
Sensor types that are not implemented in FLARE, so the taxonomy is
complete without implying FLARE support.

#### Scenario: TO/CO present but marked absent

- **WHEN** the sensor taxonomy is documented
- **THEN** TO and CO appear with an explicit "not implemented in FLARE"
  note

