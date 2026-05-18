## Why

FLARE invents its own buffer-sensor vocabulary ("2-switch", "relay",
"BUF_SENSOR_TYPE 0/1", FLARE-historic "PSF") that conflates the *sensor*
with the *control law* and does not match the established Happy Hare
conceptual model every MMU user already knows. The tension/compression
rename (`rename-buffer-states-tension-compression`) already proved the cost
of home-grown sync vocabulary (five debug rounds, one real polarity bug)
and the value of aligning to Happy Hare. Happy Hare's umbrella is the
**Sync-Feedback Sensor** with canonical single-letter type codes
(HH wiki "Sync-Feedback 'Buffer' Type Sensors"): **P** Proportional
(analog), **D** Dual (two-switch), **TO** Tension-Only, **CO**
Compression-Only. FLARE's discrete path is HH type **D**; its analog path
is HH type **P** (FLARE's legacy "PSF" alias). The decision is recorded in
`relay-duty-estimator-and-tuning` design D9; this change is the pure
vocabulary rollout, deliberately split out (no behavior change, not
bundled with logic — same methodology as the tension/compression rename).

## What Changes

- Adopt the umbrella concept **Sync-Feedback Sensor** with Happy Hare's
  canonical type codes: **P** = Proportional (analog), **D** = Dual
  (two-switch, 3-state), **TO** = Tension-Only, **CO** = Compression-Only.
  Do **not** mint new acronyms (no DSF/SFS); use HH's exact codes.
- Document the `BUF_SENSOR_TYPE` value contract as **`D = 0`, `P = 1`**
  wherever the type is referenced (comments, specs, docs, TUNING.md,
  config.ini.example). The integer values are unchanged. Note TO/CO are
  HH types **not implemented in FLARE**.
- Retire the FLARE-historic alias **"PSF"** — it is HH type **P**; live
  prose/comments say "type P (analog)", not "PSF".
- Separate the **sensor** name from the **control law** name everywhere:
  the discrete path is "the type-D two-level / hysteretic relay control
  law", not a "2-switch feature". Stop using "2-switch" / "relay" as if
  they named the sensor.
- Comments / specs / docs / `config.ini.example` / TUNING.md updated in
  lockstep. **No firmware behavior change. No `BUF_SENSOR_TYPE` value
  change. No C symbol/enum rename that alters the build** — prose +
  documented contract only (mirrors the tension/compression rename's
  "faithful rename" gate).
- Archived OpenSpec changes and change-folder names left as historical
  record (no live contract references the old umbrella terms).

## Capabilities

### New Capabilities

<!-- none — pure vocabulary rollout -->

### Modified Capabilities

- `sync-state-model`: the buffer sensor taxonomy is stated as the
  Sync-Feedback Sensor with HH type codes P/D/TO/CO and the documented
  `BUF_SENSOR_TYPE` D=0/P=1 contract.
- `sync-refactor`: the discrete-path control law is named separately from
  the type-D sensor, removing the sensor/law conflation.
- `operator-tuning-guide`: TUNING.md uses Sync-Feedback Sensor + P/D
  vocabulary and documents the `BUF_SENSOR_TYPE` value contract.

## Impact

- Docs/specs/comments: `TUNING.md`, `config.ini.example`, sensor-describing
  source comments in `firmware/src/sync.c` / `firmware/include/*`, live
  specs `sync-state-model`, `sync-refactor`, `operator-tuning-guide`.
  Prose only.
- No firmware behavior, no `BUF_SENSOR_TYPE` integer change, no enum/
  symbol rename. Host build + status snapshot identical pre/post
  (faithful-rename gate, same as `rename-buffer-states-tension-compression`).
- Relationship: realizes `relay-duty-estimator-and-tuning` design D9.
  Independent of the on-Pi 4.2 baseline (pure docs); can land before or
  after the estimator change.
- Out of scope: any control-logic change; renaming `BUF_SENSOR_TYPE`
  values or the C identifier; implementing TO/CO; renaming archived or
  in-progress change folders.
