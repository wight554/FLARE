# operator-tuning-guide Specification

## Purpose
TBD - created by archiving change tuning-operator-guide. Update Purpose after archive.
## Requirements
### Requirement: Self-contained jargon-free tuning guide

The repository SHALL provide a `TUNING.md` that a user with no firmware or
internals knowledge can follow end to end. It MUST NOT contain internal
"Phase 2.x" (or similar internal-phase) labels. It MUST begin with a
"simplest path" TL;DR that gets defaults working, followed by a plain
explanation of what tuning does (baseline and compression-bias, what good
and bad behavior look like) with no assumed firmware knowledge.

#### Scenario: New user reaches a working default

- **WHEN** a user with no firmware context follows only the TL;DR section
- **THEN** they reach a working default tuning state without needing any
  other document

#### Scenario: No internal phase jargon

- **WHEN** `TUNING.md` is searched for internal phase labels
- **THEN** no "Phase 2.x"-style internal labels are present

### Requirement: Exact copy-paste commands verified against scripts

Every command in `TUNING.md` MUST be copy-paste accurate against the
current scripts as they are (`flare_live_tuner.py`, `gcode_marker.py`,
`flare_analyze.py`, `flare_baseline_recommender.py`, `gen_config.py`,
`flash_flare.sh`). The guide MUST cover prerequisites with exact commands
(find serial port, find Klipper socket, install pyserial, back up the
state file). Where a script cannot match an operator-friendly command
as-is, the guide MUST describe the script as-is and the limitation MUST be
recorded as an open question rather than changing code.

#### Scenario: Documented flag exists

- **WHEN** any command flag shown in `TUNING.md` is checked against the
  owning script's `--help`
- **THEN** the flag exists and behaves as the guide describes

#### Scenario: Script limitation surfaced honestly

- **WHEN** a script lacks an operator-friendly option implied by the guide
- **THEN** the guide documents the script's actual behavior and the gap is
  listed as an open question, with no script change

### Requirement: Recovery path for misbehaving setups

`TUNING.md` MUST provide a "scary behavior" recovery path, reachable from
the TL;DR, that is followed BEFORE any capture/tuning when the setup
misbehaves (repeated `FAULT_HOLD`, repeated `cannot_refill`/
`cannot_relieve`, jams, stalls, or a pinned buffer). It MUST instruct the
user to revert to the shipped scalar defaults and reflash, verify boring
behavior, and treat persistent faults as mechanical (with concrete checks)
rather than a tuning value, and MUST state that tuning on a misbehaving
setup is rejected by the analyzer.

#### Scenario: Scary behavior routes away from capture

- **WHEN** a user observes repeated sync faults or jams and consults
  `TUNING.md`
- **THEN** the guide routes them to revert-to-safe-defaults and a
  mechanical checklist before any capture or analyze step

### Requirement: Sidecar is the only capture path

`TUNING.md` SHALL document the Klipper sidecar capture path as the single
live-capture mechanism.

#### Scenario: User captures live data

- **WHEN** a user follows the capture live data section
- **THEN** they can capture a calibration run using the documented
  sidecar commands

### Requirement: Two-profile deterministic workflow explained

`TUNING.md` MUST state the two-profile bracket model up front: print the
same model twice (fastest-cubic-flow profile and slowest-cubic-flow
profile), capture each, and derive one deterministic result. It MUST give
the exact analyze command using `--profile-fast`, `--profile-slow`, and
`--emit-flow-schedule`, show what the output looks like, explain the
sparse→one-point fallback, and explain that identical inputs give
identical output (and that the scalar one-point path is the safe simple
choice).

#### Scenario: User produces a flow schedule

- **WHEN** a user runs the documented analyze command on a fast and a slow
  capture
- **THEN** they obtain a flow schedule (or a one-point fallback) and can
  identify which `config.ini` keys to copy

#### Scenario: Determinism question answered

- **WHEN** a user worried about "different numbers each run" reads the
  troubleshooting section
- **THEN** it explains the deterministic guarantee and points to the
  scalar one-point path as the simple safe option

### Requirement: Apply, recommender, and verification documented

`TUNING.md` MUST give exact review/apply steps (which `config.ini` keys —
`flow_schedule_cap` + `[flow_schedule.v1]`, or scalar `baseline_rate` /
`sync_compression_bias_frac`), then exact `gen_config.py`, build, flash, and
watermark commands. It MUST document `flare_baseline_recommender.py` as
observe-only (suggests, never writes; offline analyzer remains the
authority) with its exact invocation. It MUST include a verification
section: the exact `STATUS` command, how to read relevant fields including
`SYNC_REFILL_MM` / `SYNC_RELIEVE_MM`, and the operator meaning of
`FAULT_HOLD`, `FAULT_HOLD_RECOVERY`, `cannot_refill`, and `cannot_relieve`
in observable terms only. It MUST explain acceptance-gate FAIL vs WARN in
plain language with the action for each.

#### Scenario: User applies and verifies

- **WHEN** a user copies the indicated keys, runs the documented
  regenerate/build/flash/watermark commands, then the `STATUS` command
- **THEN** they can confirm the values took effect and interpret the
  effort fields and events without reading internal design docs

#### Scenario: Recommender understood as advisory

- **WHEN** a user runs `flare_baseline_recommender.py` per the guide
- **THEN** the guide makes clear it only suggests a baseline and performs
  no writes, and the offline analyzer remains the persistent authority

### Requirement: Sync-Feedback Sensor vocabulary in tuning docs

`TUNING.md` and `config.ini.example` SHALL describe buffer sensor mode with
Happy Hare Sync-Feedback Sensor type codes: `D` = Dual two-switch sensor
(`BUF_SENSOR_TYPE == 0`, D=0), `P` = Proportional analog sensor
(`BUF_SENSOR_TYPE == 1`, P=1), and `TO`/`CO` as recognized but not
implemented in FLARE. The docs SHALL name the sensor separately from the
control law: type-D two-level / hysteretic relay control law, type-P analog
PD/EKF reserve control law.

#### Scenario: Operator sees value contract

- **WHEN** an operator reads `TUNING.md` or `config.ini.example` near sensor
  mode selection
- **THEN** the `D=0` / `P=1` value contract is visible and no legacy analog
  alias is used

### Requirement: TUNING.md uses Sync-Feedback Sensor P/D vocabulary

TUNING.md SHALL use the Sync-Feedback Sensor vocabulary with Happy Hare
type codes (P, D; TO/CO noted as unimplemented) and SHALL document the
`BUF_SENSOR_TYPE` value contract (D=0, P=1) where sensor mode is
referenced, naming the sensor separately from the control law and not
using the legacy analog alias.

#### Scenario: Dual/analog sections use the taxonomy

- **WHEN** an operator reads the dual-switch or analog tuning guidance in
  TUNING.md
- **THEN** it identifies the sensor as type D or type P with the
  `BUF_SENSOR_TYPE` value stated, and the control law named separately

#### Scenario: config.ini.example aligned

- **WHEN** an operator reads `config.ini.example` around the sensor type
- **THEN** the D=0 / P=1 contract is documented in the same Sync-Feedback
  Sensor vocabulary as TUNING.md, with no legacy analog alias

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
while pinned. The partial-drain path SHALL also be bounded by
`SYNC_COMPRESSION_DRAIN_BUDGET_MM` of COMPRESSION relieve effort; once the budget
is reached, COMPRESSION feed SHALL true-stop at `0` until the buffer leaves
COMPRESSION. When estimated demand is ≈ 0 (end-of-feed / `TASK_IDLE`),
COMPRESSION feed SHALL remain a true zero to preserve the purge/idle no-grind
behavior. `SYNC_COMPRESSION_DRAIN_FRAC = 0.0` or
`SYNC_COMPRESSION_DRAIN_BUDGET_MM = 0.0` SHALL disable the partial-drain path and
restore the legacy hard-stop for A/B testing. Applies only to type-D; SHALL NOT
alter analog type-P.

#### Scenario: Active-draw COMPRESSION drains gently

- **WHEN** the type-D buffer is in `BUF_COMPRESSION` and the extruder is
  actively drawing (estimated demand above the idle threshold)
- **THEN** the commanded feed is `SYNC_COMPRESSION_DRAIN_FRAC × demand`,
  clamped strictly below demand

#### Scenario: Over-budget COMPRESSION true-stops

- **WHEN** the type-D buffer stays in `BUF_COMPRESSION`
- **AND** COMPRESSION relieve effort reaches `SYNC_COMPRESSION_DRAIN_BUDGET_MM`
- **THEN** the commanded feed is `0` (true-stop preserved)

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

### Requirement: Type-D estimator attacks rising demand faster than falling demand

For `BUF_SENSOR_TYPE == 0`, the estimator SHALL use a faster attack when a
switch-crossing demand sample is higher than the current `extruder_est_sps`.
The firmware SHALL blend that sample with `SYNC_EST_ATTACK_ALPHA`, a
runtime/non-persisted float clamped `[0.65, 1.0]`, and SHALL bypass the normal
`EST_ALPHA_MAX` clamp for that rising-demand update.
When the sample is lower than or equal to current `extruder_est_sps`, the
estimator SHALL keep the existing dwell-derived EMA clamped by
`EST_ALPHA_MIN..EST_ALPHA_MAX`, so falling demand remains slow and does not
reintroduce COMPRESSION chatter. This SHALL apply only to type-D; analog type-P
per-tick estimation and `psf_control_law` SHALL remain unchanged.

#### Scenario: Rising type-D crossing sample fast-attacks EST

- **WHEN** a type-D switch-crossing sample is above current `extruder_est_sps`
- **THEN** firmware blends it with `SYNC_EST_ATTACK_ALPHA`
- **AND** does not clamp that alpha to `EST_ALPHA_MAX`

#### Scenario: Falling type-D sample keeps the slow EMA

- **WHEN** a type-D switch-crossing sample is below or equal to current
  `extruder_est_sps`
- **THEN** firmware uses the existing dwell-derived alpha clamped to
  `EST_ALPHA_MIN..EST_ALPHA_MAX`

#### Scenario: Type-P estimator remains unchanged

- **WHEN** `BUF_SENSOR_TYPE == 1`
- **THEN** the analog per-tick estimator and `psf_control_law` behavior are not
  changed by `SYNC_EST_ATTACK_ALPHA`

