## Why

Four rounds of fixes (B/C, F1/F2, G1/G2, H1/H2) each corrected a real bug
but the in-print oscillation persisted, because they all sat on top of the
wrong control architecture. Hardware fact (user-confirmed): standalone
Sync-Feedback Sensor type D (`BUF_SENSOR_TYPE == 0`, D=0) has **only two
microswitches**: COMPRESSION and TENSION. No analog buffer position, no
Klipper extruder feedback.

The controller ran a continuous PI loop toward a reserve setpoint on a
**dead-reckoned** buffer position. With only 3 discrete observations and a
pure-integrator plant with seconds of transport lag, that loop is a
relaxation oscillator **by construction**: between switch crossings there is
zero position information, so the extruder estimator collapses/hallucinates
and the loop swings the full ±7.8 mm wall-to-wall. Moving the setpoint (H1)
did not help — the buffer passed through it for a single tick then slammed
the opposite wall.

Logs also showed FAULT_HOLD *manufacturing* the user's symptoms:
`compression_wall_critical` (`sync.c`, virtual-endstop + COMPRESSION + hard
push) treated normal COMPRESSION-switch contact as a jam → 5 s feed freeze
(`CONF_SYNC_FAULT_HOLD_RECOVERY_MS`) while the print kept consuming →
guaranteed deficit → recovery bootstrap slam → TENSION pin (user symptom 1:
"hitting compression creates a long pause then we reach tension and stay").
The relay-law swing itself is user symptom 2 ("bangbang compression↔tension").

## What Changes

- **Type-D relay control (`BUF_SENSOR_TYPE == 0`, D=0).** Override
  the est/reserve/PI feed target with a two-level / hysteretic relay law
  matched to the switches (FLARE polarity: TENSION = empty, COMPRESSION =
  full reserve).
  Decoupled anchors so the cycle is slow/shallow:
  - `BUF_TENSION` (empty) → strong **fixed** refill `baseline_floor *
    SYNC_RELAY_CATCHUP_FRAC` — safety, never starve, estimator-independent.
  - `BUF_COMPRESSION` (full) → **stop** (`SYNC_MIN_SPS`) so the extruder
    draws the buffer off the full wall (no overfill / no physical slam).
  - `BUF_NEUTRAL` → **track demand**: `extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC`,
    clamped `[SYNC_MIN, baseline_floor]`. `NEUTRAL_FRAC` ~1.1 = gentle
    full/COMPRESSION-reserve lean. Anchoring NEUTRAL to the fixed baseline
    (~5× real demand) was what rocketed the buffer to the full wall and
    caused the NEUTRAL↔COMPRESSION bangbang; matching demand makes it drift
    slowly. Hysteresis is inherent (two switches + the NEUTRAL band); the
    existing ramp/clamp/relief logic damp the cycle.
- **Disarm FAULT_HOLD on normal switch contact in relay mode.** Switch
  contact is the control signal, not a fault: gate the tension-dwell
  FAULT_HOLD and `compression_wall_critical` FAULT_HOLD to
  `BUF_SENSOR_TYPE != 0` (type P analog, P=1 only). The relay-law stop /
  catch-up handle full/empty.
- Type-P analog mode (`BUF_SENSOR_TYPE != 0`, P=1) and all other paths unchanged.
  Prior B/C/F/G/H fixes remain (inert under the relay override but valid
  for analog mode).
- `SYNC_RELAY_*_FRAC` are the new primary on-hardware tuning knobs.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sync-refactor`: in type-D standalone mode, buffer control uses a
  two-level / hysteretic relay law matched to the switches; normal switch
  contact is the control signal and must not trigger FAULT_HOLD.

## Impact

- Firmware: `firmware/src/sync.c` — relay override before the
  ramp/clamp; two FAULT_HOLD gates; three new `#define` fracs. No config
  schema change.
- No host/script/state-format change. Analog mode byte-identical.
- OpenSpec: `openspec/specs/sync-refactor/spec.md` on archive.
- Hardware A/B retest pending on the Pi; `SYNC_RELAY_*_FRAC` will need
  on-device tuning so the limit cycle is slow, shallow, never-TENSION,
  and never faults.
