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

- Port the type-P no-overshoot ramp clamp to the type-D relay slew so
  `sync_current_sps` lands on `target_sps` instead of straddling it every 20 ms
  (`MM:720↔1080` at 10 mm/s).
- Stop the pinned-COMPRESSION true-stop path from dragging `extruder_est_sps`
  toward a stopped motor; switch-crossing estimates remain the demand authority.
- Lower the `relay_neutral_frac` default `1.25 → 1.00` (via the intermediate
  `1.10` test path) so NEUTRAL feed matches estimated demand and switches act as
  guardrails instead of adding deliberate overfeed. `relay_catchup_frac` (1.30)
  is unchanged — it only fires in TENSION refill.
- Revert the `bfb29e8` detector masking: `analyze_stability` 2.0 → 1.0 Hz,
  `--tune-drift-pct` 38 → 30 %, so the ringing/drift metrics are meaningful
  again once the overfeed is reduced.
- Correct the controller-wrong guidance in `flare_sync_check.py`
  (`analyze_stability` / `analyze_drift` docstrings + the drift FAIL message):
  for type-D, ringing/compression-drift is tuned via `relay_neutral_frac`
  (down = less compression, up = less tension), not `sync_kp_rate`; the kp
  guidance applies only to analog type-P.
- Update `TUNING.md`, `BEHAVIOR.md`, `MANUAL.md`, `config.ini`,
  `config.ini.example`, and the OpenSpec delta to the new default/model.

## Success criteria (type-D, from HW follow-ups 5-8)

- Compression touches **decay to sparse** over a soak and plateau low, with long
  quiet NEUTRAL dwell between them; each touch is a learning event that trims
  feed toward demand.
- Applied feed (`MM`) is **steady** — no 50 Hz ramp chatter, no oscillation
  across the StealthChop threshold (→ no "wroom-wroom" chopper flip).
- TENSION ≈ 0 (no starvation).
- **FAIL:** fixed-rate "constant clicking" (touch rate does not decay) or
  starvation.

## Non-Goals

- **Eliminating compression touches / holding perfect mid-band.** Impossible on a
  2-switch dead-reckoning buffer (no mid-band position sensor → the virtual
  position always drifts and must touch a switch to recalibrate). The COMPRESSION
  switch is the mid-band truth signal; rare self-correcting touches are the
  target, not a defect.
- No type-P control-law change. Type-P keeps the existing distance-EMA ramp and
  analog PD/feedforward path. All type-D work is scoped `BUF_SENSOR_TYPE == 0`.
- No `SETTINGS_VERSION` bump (a value-only default change must not wipe an
  operator's persisted TMC/calibration settings). Already-flashed units keep
  their persisted `relay_neutral_frac` until the operator runs
  `SET:RELAY_NEUTRAL_FRAC:1.00` or factory-resets; fresh flashes get 1.00.
- No firmware guard yet for "`tune` mode kp-autotunes a relay that ignores
  kp" — recorded as an open follow-up in `design.md`.

## Addendum (2026-06-02): dynamic-flow TENSION drift

The steady-state fixes above (frac → 1.00, ramp tuning, no-overshoot slew,
softened tension fallback, compression-side reserve) hold a **constant-feed**
soak well. A real print that steps demand hard (300 mm/min outer walls ↔
1500 mm/min infill) still degrades: on each COMPRESSION touch feed slams to `0`
and the buffer then drifts slowly toward TENSION while "unsure", risking
starvation. HW poll confirms it: `MM` sits a few percent **below** `EST`
throughout NEUTRAL.

Root cause (traced, `design.md` "Dynamic-flow TENSION drift"): the two-sided
neutral trim is a slow integrator that assumes COMPRESSION/TENSION touches are a
*standing feed bias*. Under demand-stepped flow the touches are *demand
transients* (wall ↔ infill), so the trim mis-attributes a compression-dominated
cycle as overfeed and **ratchets negative** (`−SYNC_RELAY_TRIM_STEP_SPS` per
COMPRESSION touch, clamp `−SYNC_RELAY_TRIM_CLAMP_SPS = −2000 sps ≈ −293 mm/min`,
leak only `+75 sps/s`). That negative trim drives NEUTRAL feed below demand →
dead-reckoned position integrates toward TENSION. A small `relay_neutral_frac`
raise does **not** fix it: one COMPRESSION touch (−44 mm/min) is under a
`1.0→1.1` raise (+81 mm/min), but two touches (−88) eat it and the clamp buries
it. The trim is the disease, not a confounder.

The two complaints ("drops too hard on COMPRESSION", "drifts to TENSION when
unsure") are the two **turning points of one relay limit cycle**. This phase
attacks both, by removing a bias rather than stacking new ones:

- **One-sided trim.** Drop the COMPRESSION down-step; keep the TENSION up-step
  (fast anti-starvation) and the neutral leak. Trim becomes a non-negative
  anti-starvation integrator that can never push toward TENSION. Steady overfeed
  is already corrected by the switch-crossing `extruder_est_sps` estimator, so
  the down-step was redundant *and* harmful under dynamic flow.
- **Gated COMPRESSION partial-drain.** While the extruder actively draws, feed
  `SYNC_COMPRESSION_DRAIN_FRAC × demand` (clamped < demand) instead of a hard
  `0`, so the buffer drains a small bounded amount off the COMPRESSION rail
  instead of dumping the full span and re-ramping from zero. Keep the true-stop
  `0` only when demand ≈ 0 (end-of-feed / `TASK_IDLE`), preserving the
  purge/idle no-grind behavior.
- **Asymmetric-objective analyzer + tuning guide.** A read-only poll analyzer
  (`scripts/`) reporting the metrics that matter (TENSION touches → must be `0`;
  COMPRESSION pin time; `mean(EST − MM)` in NEUTRAL = the underfeed signature;
  BP distribution; cycle period) and emitting a controller-correct verdict, plus
  a `TUNING.md` section codifying the asymmetric tuning sequence. Makes the
  multi-knob surface a near-1-D search ("raise lean until TENSION touches hit 0,
  then lower drain until COMPRESSION pin is comfortable").

This deliberately re-frames the goal as a **small asymmetric limit cycle hugging
the COMPRESSION rail** (the benign, self-recalibrating rail) — the relay-control
+ constraint-back-off framing — rather than mid-band regulation, which is
unobservable on a 2-switch buffer. `relay_neutral_frac` stays at the `1.00`
demand-match default; the time-since-crossing "probe" and a frac raise are
recorded as **deferred fallbacks** only if the pair under-leans on HW.

### Addendum decisions / scope deltas

- **`SETTINGS_VERSION` bump (supersedes the no-bump non-goal for this phase).**
  `SYNC_COMPRESSION_DRAIN_FRAC` is a new persisted runtime knob (SET/GET) so the
  analyzer/guide can sweep it live; adding a settings field requires a version
  bump. Migration is safe — the new field loads its default; existing
  TMC/calibration fields are preserved by the normal defaulted-load path.
- The one-sided trim **supersedes** the two-sided trim shipped earlier in this
  same change (§"Type-D relay trim learns from switch crossings" → "one-sided
  anti-starvation integrator"). The change is still active/unarchived, so this is
  an in-flight iteration, not a spec contradiction.
- Still no type-P control-law change; all work scoped `BUF_SENSOR_TYPE == 0`.
