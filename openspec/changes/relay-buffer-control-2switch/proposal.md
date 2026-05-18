## Why

Four rounds of fixes (B/C, F1/F2, G1/G2, H1/H2) each corrected a real bug
but the in-print oscillation persisted, because they all sat on top of the
wrong control architecture. Hardware fact (user-confirmed): the standalone
buffer has **only two microswitches** — TRAILING and ADVANCE. No analog
buffer position, no Klipper extruder feedback.

The controller ran a continuous PI loop toward a reserve setpoint on a
**dead-reckoned** buffer position. With only 3 discrete observations and a
pure-integrator plant with seconds of transport lag, that loop is a
relaxation oscillator **by construction**: between switch crossings there is
zero position information, so the extruder estimator collapses/hallucinates
and the loop swings the full ±7.8 mm wall-to-wall. Moving the setpoint (H1)
did not help — the buffer passed through it for a single tick then slammed
the opposite wall.

Logs also showed FAULT_HOLD *manufacturing* the user's symptoms:
`trailing_wall_critical` (`sync.c`, virtual-endstop + TRAILING + hard push)
treated normal TRAILING-switch contact as a jam → 5 s feed freeze
(`CONF_SYNC_FAULT_HOLD_RECOVERY_MS`) while the print kept consuming →
guaranteed deficit → recovery bootstrap slam → ADVANCE pin (user symptom 1:
"hitting trailing creates a long pause then we reach advance and stay").
The relay swing itself is user symptom 2 ("bangbang trailing↔advance").

## What Changes

- **Relay control (2-switch standalone, `BUF_SENSOR_TYPE == 0`).** Override
  the est/reserve/PI feed target with a hysteretic relay matched to the
  switches:
  - `BUF_TRAILING` → catch-up: `baseline_floor * SYNC_RELAY_CATCHUP_FRAC`
    (buffer empty → refill faster than demand).
  - `BUF_ADVANCE` → back-off: `baseline_floor * SYNC_RELAY_BACKOFF_FRAC`
    (buffer full → feed slow, let the extruder drain it).
  - `BUF_MID` → trailing-biased hold: `baseline_floor *
    SYNC_RELAY_MID_FRAC` (`<1`, never-ADVANCE lean → slow, shallow cycle).
  Hysteresis is inherent (two physical switches + the MID band). The
  existing ramp, clamp, `trailing_floor` and relief logic are kept and
  damp/limit the cycle.
- **Disarm FAULT_HOLD on normal switch contact in relay mode.** Switch
  contact is the control signal, not a fault: gate the advance-dwell
  FAULT_HOLD and `trailing_wall_critical` FAULT_HOLD to `BUF_SENSOR_TYPE
  != 0` (analog only). The relay back-off / catch-up handle full/empty.
- Analog mode (`BUF_SENSOR_TYPE != 0`) and all other paths unchanged.
  Prior B/C/F/G/H fixes remain (inert under the relay override but valid
  for analog mode).
- `SYNC_RELAY_*_FRAC` are the new primary on-hardware tuning knobs.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sync-refactor`: in 2-switch standalone mode, buffer control is a
  hysteretic relay matched to the switches; normal switch contact is the
  control signal and must not trigger FAULT_HOLD.

## Impact

- Firmware: `firmware/src/sync.c` — relay override before the
  ramp/clamp; two FAULT_HOLD gates; three new `#define` fracs. No config
  schema change.
- No host/script/state-format change. Analog mode byte-identical.
- OpenSpec: `openspec/specs/sync-refactor/spec.md` on archive.
- Hardware A/B retest pending on the Pi; `SYNC_RELAY_*_FRAC` will need
  on-device tuning so the limit cycle is slow, shallow, never-ADVANCE,
  and never faults.
