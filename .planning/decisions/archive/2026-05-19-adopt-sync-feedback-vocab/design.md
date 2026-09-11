## Context

Decision and rationale frozen in `relay-duty-estimator-and-tuning` design
**D9**. FLARE's home-grown "wiring shorthand / relay / undocumented BUF_SENSOR_TYPE integers / analog alias"
vocabulary conflates sensor and control law and diverges from the Happy
Hare conceptual model. HH's canonical taxonomy (wiki "Synchronized Gear
Extruder → Sync-Feedback 'Buffer' Type Sensors") uses single-letter type
codes; this change is the pure rollout, methodologically identical to
`rename-buffer-states-tension-compression`.

## Goals / Non-Goals

**Goals:** one umbrella concept (Sync-Feedback Sensor) with HH's exact
type codes (P/D/TO/CO); sensor named separately from control law in all
live prose; `BUF_SENSOR_TYPE` D=0/P=1 contract documented at every
reference; the legacy analog alias retired in favor of HH "P"; reviewable as
"faithful vocabulary rollout, zero behavior delta".

**Non-Goals:** no control-logic change; no `BUF_SENSOR_TYPE` integer
change; no C identifier/enum rename; no new acronyms (no DSF/SFS); no
TO/CO implementation; no back-compat shim; no renaming archived or
in-progress change folders.

## Decisions

### D1 — Canonical taxonomy (frozen, HH-exact)

```
 Sync-Feedback Sensor              umbrella (HH wiki term)
   P   Proportional   analog ADC, continuous   BUF_SENSOR_TYPE == 1
   D   Dual           two switches, tension +  BUF_SENSOR_TYPE == 0
                       compression, 3-state
   TO  Tension Only    single switch           not implemented in FLARE
   CO  Compression Only single switch           not implemented in FLARE
 control law (named separately from the sensor):
   D → "two-level / hysteretic relay"   P → "PD / EKF (reserve PI)"
```
Wiring shorthand / "relay" name the *law/wiring*, never the sensor. The FLARE
legacy analog alias is HH type P and is retired. No invented acronyms — HH
codes verbatim.

### D2 — Faithful-rollout gate

Host build green + a captured status-line + event-token snapshot identical
pre/post. Any numeric/behavioral or `BUF_SENSOR_TYPE` value delta = stop.
Unlike the tension/compression rename, **no** protocol token / config key
/ C symbol changes here — the integer contract is only *documented*, not
changed.

### D3 — Scope of edits

Prose surfaces only: live specs (`sync-state-model`, `sync-refactor`,
`operator-tuning-guide`), `TUNING.md`, `config.ini.example`, and source
*comments* in `firmware/src/sync.c` / `firmware/include/*` describing the
sensor. Code identifiers (`BUF_SENSOR_TYPE`, enums, functions) are **not**
renamed (out of scope; keeps the diff a pure-prose review). A grep
inventory of the home-grown umbrella terms + legacy analog alias in the live
(non-archived) set is a Phase-1 deliverable; zero unscoped survivors at
the end.

### D4 — Archived & folder names left historical

`openspec/changes/archive/*` and the `relay-buffer-control-2switch` /
`relay-duty-estimator-and-tuning` folder names are not rewritten (same
rule the tension/compression rename used: only live contracts reworded).
Live prose *inside* in-progress changes that describes the sensor is
updated; folder names are not.

### D5 — TO/CO documented as known-but-absent

TO/CO are part of HH's taxonomy and are documented as recognized types
**not implemented in FLARE**, so the vocabulary is complete and a future
single-switch port has a name ready, without implying FLARE supports them.

## Risks / Trade-offs

- [Doc says type P/D but code symbol still `BUF_SENSOR_TYPE`] → accepted,
  explicit: D3 scopes to prose; the D=0/P=1 contract is documented at each
  reference so the mapping is unambiguous. Symbol rename is a separate
  future option (open question), not this change.
- [Rollout silently misses a non-compiler-checked doc or a stray legacy alias] →
  D3 grep inventory + zero-survivor gate on the live set.
- [Vocabulary churn for readers of old logs/docs] → accepted (active dev);
  archived material left historical.
- [Accidentally changes behavior] → D2 snapshot gate; edits are
  comment/prose only by construction.

## Migration Plan

P0 freeze D1 (HH-exact). P1 grep inventory of home-grown umbrella terms +
legacy analog alias in the live set (deliverable). P2 reword live specs + TUNING.md +
config.ini.example + sensor-describing source comments in lockstep;
document TO/CO as not-implemented. P3 faithful-rollout gate D2. Rollback:
revert; pure prose, no state/format/behavior migration.

## Apply Inventory And Plan (2026-05-19)

### Frozen taxonomy

Use the D1 table above as the rollout contract: Sync-Feedback Sensor is the
umbrella; HH type D is FLARE `BUF_SENSOR_TYPE == 0`; HH type P is FLARE
`BUF_SENSOR_TYPE == 1`; TO/CO are recognized HH types not implemented in
FLARE. The type-D control law is the two-level / hysteretic relay law, named
separately from the sensor. The type-P control law is the analog PD/EKF
reserve law.

### Pre-change protocol/status snapshot

Captured before prose edits:

```text
822388ee98ff8c2f18971c183b75b2a80493209865217d5c1be64329cd279016  /tmp/flare-sfs-vocab-pre-snapshot.txt
165 /tmp/flare-sfs-vocab-pre-snapshot.txt
```

Snapshot command sorted emitted protocol/status/event token sites in
`firmware/src`, `firmware/include`, and `scripts`. Post-change must match.

### Live inventory

Zero-survivor terms to retire in live prose/comments:

- Legacy analog alias: `AGENTS.md`, `TEST_CASES.md`, `firmware/include/buf_signal.h`,
  `openspec/specs/sync-refactor/spec.md`, and the new change artifacts.
- Sensor-as-wiring-shorthand / sensor-as-`relay`: `TEST_CASES.md`,
  `firmware/src/sync.c` comments, `openspec/changes/relay-buffer-control-2switch/*`,
  `openspec/changes/relay-duty-estimator-and-tuning/*`, and affected live
  specs. Keep folder names historical.
- Bare `BUF_SENSOR_TYPE` prose references missing the D=0/P=1 contract:
  `config.ini.example`, `TUNING.md` additions, live specs, source comments,
  `TEST_CASES.md`, and in-progress change prose.

### Edit plan

#### OpenSpec live specs and change specs

- `openspec/specs/sync-state-model/spec.md`: add Sync-Feedback Sensor
  taxonomy requirement and D=0/P=1 contract.
- `openspec/specs/sync-refactor/spec.md`: replace legacy alias wording with type-P
  analog wording; add sensor/law separation and TO/CO note; keep behavior
  unchanged.
- `openspec/specs/operator-tuning-guide/spec.md`: require TUNING/config
  vocabulary parity.
- `openspec/changes/relay-buffer-control-2switch/{proposal,design,tasks}.md`:
  reword live prose to type-D dual-switch sensor plus relay law; leave folder
  names historical.
- `openspec/changes/relay-duty-estimator-and-tuning/**/*`: reword live prose
  to type-D/type-P vocabulary; note this change realizes D9.
- `openspec/changes/adopt-sync-feedback-vocab/*`: update task progress and
  validation notes only after each unit lands.

#### Operator docs and test docs

- `TUNING.md`: add a concise Sync-Feedback Sensor mode section and use P/D
  names where sensor mode appears.
- `config.ini.example`: document `buf_sensor_type` as D=0/P=1 with TO/CO not
  implemented.
- `AGENTS.md` and `TEST_CASES.md`: retire legacy alias and sensor-as-wiring-shorthand wording.

#### Source comments only

- `firmware/src/sync.c`: reword sensor/law comments; no C identifiers or logic
  changed.
- `firmware/include/buf_signal.h` and `firmware/include/controller_shared.h`:
  add D=0/P=1 contract in comments; retire legacy alias wording.

Risk: docs-only plus comments, but touching C comments can still disturb review
noise. Guard: post snapshot hash must match pre snapshot; build must pass.

## Open Questions

- Whether a later change should also rename the C identifier
  `BUF_SENSOR_TYPE` and add an enum named with HH codes (`SFS_P`/`SFS_D`).
  Deferred: out of scope here (D3); revisit only if the
  documented-contract-only approach proves confusing in practice.
