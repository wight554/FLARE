## ADDED Requirements

### Requirement: Type-D tuning guidance names the controller-correct lever

Operator-facing type-D tuning guidance SHALL attribute the relay limit cycle
and its COMPRESSION/TENSION drift to `relay_neutral_frac` (and
`relay_catchup_frac`), and SHALL NOT instruct the operator to change
`sync_kp_rate` for a type-D (`BUF_SENSOR_TYPE == 0`) buffer. This requirement
applies to both `TUNING.md` and the verdict/help text emitted by
`flare_sync_check.py` (`analyze_stability`, `analyze_drift`). `sync_kp_rate`
guidance MAY appear only for analog type P (`BUF_SENSOR_TYPE == 1`), whose
`psf_control_law` actually consumes it.

#### Scenario: Stability/drift advice points at the relay knob for type-D

- **WHEN** `flare_sync_check.py` reports a stability (ringing) or drift FAIL
- **THEN** the remediation text names `relay_neutral_frac` (down = less
  COMPRESSION time, up = less TENSION drift), not `sync_kp_rate`

#### Scenario: kp is documented as inert for type-D

- **WHEN** `TUNING.md` describes type-D relay tuning
- **THEN** it states that `sync_kp_rate` / accel autotune does not affect the
  type-D relay (those apply to analog type-P) and that the quiet-cycle lever
  is `relay_neutral_frac`

### Requirement: Default relay_neutral_frac tracks demand without deliberate overfeed

The shipped default `relay_neutral_frac` SHALL match demand (`1.00`) for type-D
after the no-overshoot ramp fix, not deliberately overfeed. `TUNING.md` and
`config.ini.example` SHALL show this default, and it SHALL match the
`gen_config.py` default. Operators MAY raise it slightly only if hardware soak
shows steady TENSION drift.

#### Scenario: Documented default matches the generator

- **WHEN** the `relay_neutral_frac` default is read from `TUNING.md`,
  `config.ini.example`, and `gen_config.py`
- **THEN** all three agree on `1.00`

#### Scenario: Stability detector threshold is not masking the cycle

- **WHEN** the `flare_sync_check.py` stability ringing threshold is read
- **THEN** it is `1.0` cycles/s (not raised to absorb the relay limit cycle),
  so a genuine sustained cycle is reported rather than silenced

### Requirement: Type-D relay trim is a one-sided anti-starvation integrator

For `BUF_SENSOR_TYPE == 0`, the volatile neutral feed trim SHALL only ever
*raise* NEUTRAL feed. A TENSION touch (starvation, the dangerous rail) SHALL
increase the trim by `SYNC_RELAY_TRIM_STEP_SPS`, clamped to
`+SYNC_RELAY_TRIM_CLAMP_SPS`, and the trim SHALL leak toward zero during
`BUF_NEUTRAL` dwell. A COMPRESSION touch SHALL NOT reduce the trim: COMPRESSION
is the tolerated/safe rail, and steady overfeed is corrected by the
switch-crossing demand estimator (`extruder_est_sps`), not by cutting feed.
Consequently the trim SHALL remain non-negative, so it can never drive NEUTRAL
feed below `demand × relay_neutral_frac` (the prior two-sided trim ratcheted
negative under compression-dominated dynamic flow and dragged the buffer toward
TENSION). The trim SHALL apply only to the type-D NEUTRAL relay feed and SHALL
NOT alter analog type-P feedforward.

#### Scenario: COMPRESSION touch does not cut neutral feed

- **WHEN** the type-D buffer crosses from `BUF_NEUTRAL` to `BUF_COMPRESSION`
- **THEN** the learned neutral trim is left unchanged (no down-step)
- **AND** any standing overfeed is corrected through `extruder_est_sps`
  crossing samples instead

#### Scenario: TENSION touch raises neutral feed

- **WHEN** the type-D buffer crosses from `BUF_NEUTRAL` to `BUF_TENSION`
- **THEN** the learned neutral trim is increased by `SYNC_RELAY_TRIM_STEP_SPS`
- **AND** the result is clamped to `+SYNC_RELAY_TRIM_CLAMP_SPS`

#### Scenario: Trim cannot push the buffer toward TENSION

- **WHEN** the type-D buffer dwells in `BUF_NEUTRAL`
- **THEN** the effective trim is non-negative
- **AND** the commanded NEUTRAL feed is never below `demand × relay_neutral_frac`

### Requirement: Type-D COMPRESSION feed drains gently while the extruder draws

For `BUF_SENSOR_TYPE == 0`, COMPRESSION feed SHALL be a bounded fraction of
estimated demand (`SYNC_COMPRESSION_DRAIN_FRAC × extruder_est_sps`) — not a hard
zero — while sync is active and the extruder is actively drawing filament
(estimated demand above a small threshold), so
the buffer drains a small bounded amount off the COMPRESSION rail instead of
dumping the full span toward TENSION and forcing a re-ramp from zero. The drain
fraction SHALL be clamped strictly below demand so the buffer cannot net-fill
while pinned. When estimated demand is ≈ 0 (end-of-feed / `TASK_IDLE`),
COMPRESSION feed SHALL remain a true zero to preserve the purge/idle no-grind
behavior. Applies only to type-D; SHALL NOT alter analog type-P.

#### Scenario: Active-draw COMPRESSION drains gently

- **WHEN** the type-D buffer is in `BUF_COMPRESSION` and the extruder is
  actively drawing (estimated demand above the idle threshold)
- **THEN** the commanded feed is `SYNC_COMPRESSION_DRAIN_FRAC × demand`,
  clamped strictly below demand

#### Scenario: Idle COMPRESSION true-stops

- **WHEN** the type-D buffer is in `BUF_COMPRESSION` and estimated demand is
  ≈ 0 (`TASK_IDLE` / end-of-feed)
- **THEN** the commanded feed is `0` (true-stop preserved)

### Requirement: Asymmetric relay cycle analyzer reports tuning metrics

A host analyzer SHALL parse the status poll stream and report, over a window:
TENSION touch count (the hard constraint; target `0`), COMPRESSION pin
duration, mean `EST − MM` during `BUF_NEUTRAL` (the underfeed / tension-drift
signature), the `BP` distribution and minimum, and the relay cycle period. The
analyzer SHALL emit an asymmetric-objective verdict: PASS only when TENSION
touches are zero; otherwise it SHALL recommend the controller-correct lever
(raise `relay_neutral_frac` or adjust the COMPRESSION drain / trim settings) and
SHALL NOT recommend `sync_kp_rate` for type-D. The analyzer SHALL operate
read-only from existing poll fields (`BP`, `BUF`, `MM`, `EST`) and SHALL NOT
require new firmware telemetry.

#### Scenario: A TENSION touch fails the asymmetric objective

- **WHEN** the analyzed window contains at least one TENSION touch
- **THEN** the verdict is FAIL with remediation naming `relay_neutral_frac` /
  COMPRESSION-drain / trim, not `sync_kp_rate`

#### Scenario: Underfeed drift is surfaced from existing fields

- **WHEN** mean `EST − MM` during `BUF_NEUTRAL` is positive over the window
- **THEN** the analyzer reports a tension-ward underfeed drift without requiring
  any new firmware telemetry

### Requirement: Type-D estimator anchors on neutral fill

For `BUF_SENSOR_TYPE == 0`, the firmware SHALL treat the
`BUF_NEUTRAL -> BUF_COMPRESSION` transition as the primary demand sample by
averaging the actual applied `sync_current_sps` over the NEUTRAL dwell,
preferring the pre-taper portion before compression-side braking when available,
and subtracting the measured fill rate. Degenerate fill samples SHALL be ignored,
slow near-converged fills SHALL remain eligible, and accepted demand samples
SHALL blend into `extruder_est_sps`. When later compression-side fill samples no
longer have known switch-to-switch travel, the pre-taper applied feed average
SHALL be eligible as an upper-bound demand sample, and a short
`BUF_COMPRESSION -> BUF_NEUTRAL` true-stop drain with near-zero applied feed
SHALL be eligible as a fallback demand sample. The residual neutral trim SHALL
leak toward zero during `BUF_NEUTRAL` dwell. This estimator correction and trim
leak SHALL NOT alter the analog type-P estimator/feedforward path.

#### Scenario: NEUTRAL fill samples known applied feed

- **WHEN** the type-D buffer crosses from `BUF_NEUTRAL` to `BUF_COMPRESSION`
- **THEN** firmware estimates demand from averaged NEUTRAL feed minus fill rate
- **AND** prefers the pre-taper applied-feed average when that window has enough
  samples
- **AND** blends that sample into `extruder_est_sps`

#### Scenario: Degenerate fill sample is rejected

- **WHEN** the type-D buffer crosses from `BUF_NEUTRAL` to `BUF_COMPRESSION`
- **AND** the NEUTRAL dwell is too short or the fill rate is far above averaged feed
- **THEN** firmware does not update `extruder_est_sps` from that crossing

#### Scenario: Same-side compression fill samples applied feed

- **WHEN** the type-D buffer crosses from `BUF_NEUTRAL` to `BUF_COMPRESSION`
- **AND** the crossing has no known switch-to-switch travel because the prior
  entry came from `BUF_COMPRESSION`
- **THEN** firmware blends the pre-taper applied-feed average as an upper-bound
  demand sample

#### Scenario: Short true-stop drain remains eligible

- **WHEN** the type-D buffer crosses from `BUF_COMPRESSION` to `BUF_NEUTRAL`
- **AND** the COMPRESSION dwell is long enough to reject bounce
- **AND** the averaged applied feed is near zero
- **THEN** firmware may blend the drain-derived demand sample into `extruder_est_sps`

#### Scenario: Residual trim self-centers in neutral

- **WHEN** the type-D buffer dwells in `BUF_NEUTRAL`
- **THEN** the learned neutral trim moves toward zero over time

### Requirement: Type-D reserve target provides speed-step headroom

For `BUF_SENSOR_TYPE == 0`, the firmware SHALL park the virtual neutral target
slightly toward the compression side using the existing `SYNC_RESERVE_PCT`
reserve percentage. This reserve SHALL give sharp real-print speed-ups physical
headroom before the buffer reaches TENSION. This SHALL NOT change analog type-P
control behavior, and it SHALL NOT require increasing `relay_neutral_frac` above
the demand-match default.

#### Scenario: Real-print speed-up consumes reserve before TENSION

- **WHEN** Type-D sync is active during a slow-to-fast print segment change
- **THEN** the reserve target is compression-side by `SYNC_RESERVE_PCT`
- **AND** the controller refills toward that reserve while still using switch
  crossings as calibration truth
