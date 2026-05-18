## Why

FLARE invents its own buffer-sensor vocabulary ("2-switch", "relay",
"BUF_SENSOR_TYPE 0/1") that conflates the *sensor* with the *control law*
and does not match the established Happy Hare conceptual model every MMU
user already knows. The tension/compression rename
(`rename-buffer-states-tension-compression`) already proved the cost of
home-grown sync vocabulary (five debug rounds, one real polarity bug) and
the value of aligning to Happy Hare. Happy Hare's umbrella term is
**Sync-Feedback Sensor (SFS)**; the analog variant is **PSF**
(Proportional Sync-Feedback, already used in FLARE comments/audit) and the
discrete dual-switch 3-state variant is naturally **DSF** (Discrete
Sync-Feedback). The decision and rationale are recorded in
`relay-duty-estimator-and-tuning` design D9; this change is the pure
vocabulary rollout, deliberately split out (no behavior change, not
bundled with logic — same methodology as the tension/compression rename).

## What Changes

- Adopt the umbrella concept **Sync-Feedback Sensor (SFS)** with two
  variants: **PSF** = Proportional (analog) Sync-Feedback;
  **DSF** = Discrete (dual-switch, 3-state) Sync-Feedback.
- Document the `BUF_SENSOR_TYPE` value contract as **`DSF = 0`,
  `PSF = 1`** wherever the type is referenced (comments, specs, docs,
  TUNING.md, config.ini.example). The integer values are unchanged.
- Separate the **sensor** name from the **control law** name everywhere:
  the discrete path is the "DSF two-level / hysteretic relay control law",
  not a "2-switch feature". Stop using "2-switch" / "relay" as if they
  named the sensor.
- Rename `relay-buffer-control-2switch` references in live prose to the
  DSF/two-level vocabulary where they describe the sensor-vs-law split
  (the change folder name itself is historical and left as-is, like
  archived changes under the rename precedent).
- Comments / specs / docs / `config.ini.example` / TUNING.md updated in
  lockstep. **No firmware behavior change. No `BUF_SENSOR_TYPE` value
  change. No symbol/enum rename that alters the build** — this is prose +
  documented contract only (mirrors the tension/compression rename's
  "faithful rename" gate).
- Archived OpenSpec changes left as historical record (no live contract
  references the old umbrella terms).

## Capabilities

### New Capabilities

<!-- none — pure vocabulary rollout -->

### Modified Capabilities

- `sync-state-model`: the buffer sensor taxonomy is stated as
  Sync-Feedback Sensor with DSF/PSF variants and the documented
  `BUF_SENSOR_TYPE` DSF=0/PSF=1 contract.
- `sync-refactor`: the discrete-path control law is named separately from
  the sensor (DSF two-level/relay), removing the sensor/law conflation.
- `operator-tuning-guide`: TUNING.md uses the SFS/DSF/PSF vocabulary and
  documents the `BUF_SENSOR_TYPE` value contract.

## Impact

- Docs/specs/comments: `TUNING.md`, `config.ini.example`, source comments
  in `firmware/src/sync.c` / `firmware/include/*`, live specs
  `sync-state-model`, `sync-refactor`, `operator-tuning-guide`. Prose
  only.
- No firmware behavior, no `BUF_SENSOR_TYPE` integer change, no enum/
  symbol rename that changes the binary. Host build + status snapshot
  identical pre/post (faithful-rename gate, same as
  `rename-buffer-states-tension-compression`).
- Relationship: records/implements `relay-duty-estimator-and-tuning`
  design D9. Independent of the on-Pi 4.2 baseline (pure docs); can land
  before or after the estimator change.
- Out of scope: any control-logic change; renaming `BUF_SENSOR_TYPE`
  values or the C identifier; renaming archived change folders.
