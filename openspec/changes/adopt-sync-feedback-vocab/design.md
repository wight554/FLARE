## Context

Decision and rationale are frozen in `relay-duty-estimator-and-tuning`
design **D9**. FLARE's home-grown "2-switch / relay / BUF_SENSOR_TYPE 0/1"
vocabulary conflates sensor and control law and diverges from the Happy
Hare conceptual model. PSF is already used in FLARE for the analog path;
DSF is the natural counterpart for the discrete dual-switch path. This
change is the pure rollout, methodologically identical to
`rename-buffer-states-tension-compression`: faithful rename, behavior
byte-identical, not bundled with logic, archived changes left historical.

## Goals / Non-Goals

**Goals:** one umbrella concept (SFS) with two named variants (PSF/DSF);
sensor named separately from control law everywhere in live prose; the
`BUF_SENSOR_TYPE` DSF=0/PSF=1 contract documented at every reference;
reviewable as "faithful vocabulary rollout, zero behavior delta".

**Non-Goals:** no control-logic change; no `BUF_SENSOR_TYPE` integer
change; no C identifier/enum rename that alters the binary; no back-compat
shim; no renaming of archived change folders or the
`relay-buffer-control-2switch` folder name (historical, like archived
changes under the rename precedent).

## Decisions

### D1 — Canonical taxonomy (frozen, from D9)

```
 Sync-Feedback Sensor (SFS)            umbrella concept (Happy Hare term)
   ├─ PSF  Proportional SF  analog     BUF_SENSOR_TYPE == 1
   └─ DSF  Discrete SF      dual-sw,   BUF_SENSOR_TYPE == 0
                            3-state
 control law (named separately from the sensor):
   DSF → "two-level / hysteretic relay"   PSF → "PD / EKF (reserve PI)"
```
"2-switch" and "relay" name the *law/wiring*, never the sensor. The sensor
is DSF/PSF/SFS.

### D2 — Faithful-rename gate (from the precedent)

Host build green + a captured status-line + event-token snapshot identical
pre/post. Any numeric/behavioral or `BUF_SENSOR_TYPE` value delta = stop;
this is not a faithful rollout. No protocol token, config key, or C
symbol changes here (distinct from the tension/compression rename, which
did rename tokens — this one is purely conceptual/doc, the integer
contract is only *documented*, not changed).

### D3 — Scope of edits

Prose surfaces only: live specs (`sync-state-model`, `sync-refactor`,
`operator-tuning-guide`), `TUNING.md`, `config.ini.example`, and source
*comments* in `firmware/src/sync.c` / `firmware/include/*` that describe
the sensor. Code identifiers (`BUF_SENSOR_TYPE`, enum names, function
names) are **not** renamed — out of scope, avoids build churn and keeps
the diff a pure-prose review. An exhaustive grep inventory of the
home-grown umbrella terms in the live (non-archived) set is a Phase-1
deliverable; zero unscoped survivors at the end.

### D4 — Archived changes & folder names left historical

`openspec/changes/archive/*` and the `relay-buffer-control-2switch` /
`relay-duty-estimator-and-tuning` folder names are historical and not
rewritten (same rule the tension/compression rename used: only live
contracts are reworded). Live prose inside in-progress changes that
describes the sensor is updated; folder names are not.

## Risks / Trade-offs

- [Doc says DSF/PSF but code symbol still `BUF_SENSOR_TYPE`] → accepted
  and explicit: D3 scopes to prose; the value contract DSF=0/PSF=1 is
  *documented* at each reference so the mapping is unambiguous. A symbol
  rename is a separate future option, not this change.
- [Rollout silently misses a non-compiler-checked doc] → D3 grep
  inventory + zero-survivor gate on the live set (same safety net as the
  prior rename).
- [Vocabulary churn for readers of old logs/docs] → accepted (active dev);
  archived material left as historical record.
- [Accidentally changes behavior] → D2 snapshot gate; edits are
  comment/prose only by construction.

## Migration Plan

P0 freeze D1 taxonomy. P1 grep inventory of home-grown umbrella terms in
the live set (deliverable). P2 reword live specs + TUNING.md +
config.ini.example + sensor-describing source comments in lockstep. P3
faithful-rename gate D2 (build + snapshot identical). Rollback: revert;
pure prose, no state/format/behavior migration.

## Open Questions

- Whether a later change should also rename the C identifier
  `BUF_SENSOR_TYPE` → an SFS-named symbol + enum (DSF/PSF). Deferred:
  out of scope here (D3); revisit only if the documented-contract-only
  approach proves confusing in practice.
