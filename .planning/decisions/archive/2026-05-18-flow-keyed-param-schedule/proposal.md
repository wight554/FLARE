## Why

The sync controller's baseline rate and trailing-bias are single scalars.
They are regime-dependent: a print dominated by fast cubic flow wants a
different baseline/bias than a slow-flow print, so the offline analyzer
recommends different scalars depending on the speed mix of the captured
run. Operators see confusing run-to-run recommendations for the same model.
The deterministic dual-profile workflow (`sync-tuning-and-relief-finish`,
landed `4675c45`) brackets the regime but still collapses it to one scalar.
Keying baseline/bias to live extruder flow removes the regime ambiguity at
its root.

## What Changes

- The offline analyzer (`scripts/flare_analyze.py`) emits a **flow-keyed
  schedule**: bounded, ordered breakpoints mapping estimated flow →
  `{baseline_sps, trailing_bias_frac}`, derived deterministically from the
  existing per-`(feature, v_fil_bin)` velocity buckets (reuse the
  deterministic reducer + `BIAS_SAFE_MIN/MAX` clamps; no wall-clock
  recency).
- Firmware interpolates the active baseline/bias from the live
  `extruder_est_sps` against the schedule, instead of reading a single
  scalar.
- A new, **versioned, additive** schedule table format in `config.ini` →
  generated `tune.h`. Existing scalar keys keep working and define the
  **degenerate 1-breakpoint schedule**.
- The disciplined live baseline learner now ratchets **within the active
  flow segment** (still ephemeral, up-only, non-persistent) instead of a
  global scalar.
- **BREAKING (config format only, opt-in):** introduces a schedule table;
  configs without it run the scalar values as a 1-point schedule with
  **byte-for-byte identical behavior** (zero regression).

Hard constraints (carried, explicit):
1. 1-breakpoint schedule MUST reproduce current scalar behavior exactly.
2. Full-bias invariant untouched — schedule only supplies the
   baseline/bias inputs `SYNC_ACTIVE` reserve-target control already
   consumes; reserve target / `reserve_correction` / `zone_bias` /
   soft-wall trim / collapse ramp unchanged.
3. Deterministic — identical bucket inputs MUST produce identical
   breakpoints.
4. Offline analyzer remains the sole persistent authority.
5. No host / Klipper / encoder coupling — the flow key is the firmware's
   own `extruder_est_sps` only.
6. Breakpoint count bounded and config-tunable; interpolation is
   float-light for RP2040.

## Capabilities

### New Capabilities
- `flow-keyed-schedule`: the versioned schedule table format, its
  determinism and bounded-size contract, the firmware interpolation
  behavior on `extruder_est_sps`, and the degenerate-equivalence guarantee.

### Modified Capabilities
- `calibration-workflow`: the analyzer additionally emits a deterministic
  flow-keyed schedule (not only a scalar baseline) from the same buckets.
- `sync-refactor`: the controller derives baseline/bias by interpolating
  the schedule on live flow, with the scalar path as the degenerate
  fallback; live learner ratchets within the active segment.

## Impact

- Host: `scripts/flare_analyze.py` (emit schedule), tests for determinism
  and degenerate-equivalence.
- Config: `config.ini.example` + `scripts/gen_config.py` (versioned
  schedule table format + `tune.h` generation; scalar keys preserved).
- Firmware: `firmware/src/sync.c` (flow→param interpolation, scalar
  fallback, segment-scoped live ratchet), `firmware/include/sync.h`.
- Depends on `sync-tuning-and-relief-finish` (`4675c45`); built after it to
  avoid touching the baseline/bias path twice concurrently.
- No new control law, EKF, host-speed coupling, or persistence-format
  change beyond the additive schedule table.
