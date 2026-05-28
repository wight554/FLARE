# relay-neutral-frac-detune

## Why

Type-D (`BUF_SENSOR_TYPE == 0`) sync is audibly bang-banging in steady print:
the motor ramps to feed, slams to a stop at the COMPRESSION switch, drains,
ramps again — a loud relay limit cycle. Root cause is a stale default:
`relay_neutral_frac = 1.25` commands NEUTRAL feed at `1.25 × extruder_est_sps`
— a deliberate **25 % overfeed**. Since `extruder_est_sps` is a real measured
demand (recomputed every switch crossing from `mmu_rate + arm_velocity`,
`sync.c:913`), that 25 % surplus drives the buffer into COMPRESSION every
cycle, where the relay true-stops to `0` (`sync.c:2097`), and the cycle
repeats.

`1.30 / 1.25` were introduced in commit `558dbbb` during the
`relay-confidence-gate-harden` era to fight the *old confident-estimator*
TENSION-starvation. After `relay-fallback-only` (`SETTINGS_VERSION` 54) made
NEUTRAL unconditionally the accurate `extruder_est_sps` fallback, the heavy
overfeed lean was never re-tuned, and now produces the opposite failure:
TENSION ≈ 0 %, COMPRESSION ≈ 28–31 %, constant cycling.

A prior tuning session mis-modeled type-D as a continuous PI loop and chased
`sync_kp_rate` / `sync_ramp_accel`. **`kp` is not in the type-D control path**
(`relay_control_law`, `sync.c:1627`, keys only on discrete switch state; only
the analog `psf_control_law` uses kp). The kp sweep had "zero effect" because
kp is unused; the "28–31 % hardware equilibrium floor" is just the overfeed
duty cycle; the "1.33 Hz, kp-independent" peak is the relay limit-cycle
frequency, not a print-pattern false positive. That session then *raised*
`flare_sync_check.py` thresholds (`bfb29e8`: ringing 1.0→2.0 Hz, drift
30→38 %) to silence the detector that was correctly flagging the cycle, and
the detector guidance still tells operators to "Raise SYNC_KP_RATE" for a
controller that ignores it.

## What Changes

- Lower the `relay_neutral_frac` default `1.25 → 1.10` (the documented
  "gentle compression lean"; overfeed `0.25 → 0.10`). Stretches the
  climb-to-COMPRESSION ~2.5×, lengthening the quiet NEUTRAL dwell and cutting
  cycle frequency, while staying `> 1.0` so the buffer never starves to
  TENSION. `relay_catchup_frac` (1.30) is unchanged — it only fires in
  TENSION, which is no longer reached.
- Revert the `bfb29e8` detector masking: `analyze_stability` 2.0 → 1.0 Hz,
  `--tune-drift-pct` 38 → 30 %, so the ringing/drift metrics are meaningful
  again once the overfeed is reduced.
- Correct the controller-wrong guidance in `flare_sync_check.py`
  (`analyze_stability` / `analyze_drift` docstrings + the drift FAIL message):
  for type-D, ringing/compression-drift is tuned via `relay_neutral_frac`
  (down = less compression, up = less tension), not `sync_kp_rate`; the kp
  guidance applies only to analog type-P.
- Update `TUNING.md` and `config.ini.example` to the new default.

## Non-Goals

- No control-law / behavior change. The relay law (`NEUTRAL = clamp(est) ·
  neutral_frac`, TENSION catch-up, COMPRESSION true-stop) is byte-identical;
  only a tunable's default value moves. The `relay-fallback-only` contract is
  untouched.
- No `SETTINGS_VERSION` bump (a value-only default change must not wipe an
  operator's persisted TMC/calibration settings). Already-flashed units keep
  their persisted `relay_neutral_frac` until the operator runs
  `SET:RELAY_NEUTRAL_FRAC:1.10` or factory-resets; fresh flashes get 1.10.
- No firmware guard yet for "`tune` mode kp-autotunes a relay that ignores
  kp" — recorded as an open follow-up in `design.md`.
