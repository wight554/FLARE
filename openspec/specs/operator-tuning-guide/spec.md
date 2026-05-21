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

