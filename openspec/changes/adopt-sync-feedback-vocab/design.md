## Context

Decision and rationale frozen in `relay-duty-estimator-and-tuning` design
**D9**. FLARE's home-grown "2-switch / relay / BUF_SENSOR_TYPE 0/1 / PSF"
vocabulary conflates sensor and control law and diverges from the Happy
Hare conceptual model. HH's canonical taxonomy (wiki "Synchronized Gear
Extruder → Sync-Feedback 'Buffer' Type Sensors") uses single-letter type
codes; this change is the pure rollout, methodologically identical to
`rename-buffer-states-tension-compression`.

## Goals / Non-Goals

**Goals:** one umbrella concept (Sync-Feedback Sensor) with HH's exact
type codes (P/D/TO/CO); sensor named separately from control law in all
live prose; `BUF_SENSOR_TYPE` D=0/P=1 contract documented at every
reference; legacy "PSF" retired in favor of HH "P"; reviewable as
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
"2-switch" / "relay" name the *law/wiring*, never the sensor. FLARE legacy
**"PSF" ≡ HH type P** and is retired. No invented acronyms — HH codes
verbatim.

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
inventory of the home-grown umbrella terms + "PSF" in the live
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
- [Rollout silently misses a non-compiler-checked doc or a stray "PSF"] →
  D3 grep inventory + zero-survivor gate on the live set.
- [Vocabulary churn for readers of old logs/docs] → accepted (active dev);
  archived material left historical.
- [Accidentally changes behavior] → D2 snapshot gate; edits are
  comment/prose only by construction.

## Migration Plan

P0 freeze D1 (HH-exact). P1 grep inventory of home-grown umbrella terms +
"PSF" in the live set (deliverable). P2 reword live specs + TUNING.md +
config.ini.example + sensor-describing source comments in lockstep;
document TO/CO as not-implemented. P3 faithful-rollout gate D2. Rollback:
revert; pure prose, no state/format/behavior migration.

## Open Questions

- Whether a later change should also rename the C identifier
  `BUF_SENSOR_TYPE` and add an enum named with HH codes (`SFS_P`/`SFS_D`).
  Deferred: out of scope here (D3); revisit only if the
  documented-contract-only approach proves confusing in practice.
