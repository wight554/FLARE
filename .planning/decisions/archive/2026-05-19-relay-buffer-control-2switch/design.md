## Context

User-confirmed hardware: standalone Sync-Feedback Sensor type D
(`BUF_SENSOR_TYPE == 0`, D=0) = two microswitches only
(COMPRESSION/TENSION). No analog position, no Klipper extruder feedback.
Real prints; `AV:0.00` between crossings is normal for virtual-endstop mode.

Symptom data: with `RT` at -3.90 (H1) the buffer still slammed ±7.8
wall-to-wall every cycle, passing the target for one tick (`RE:0.27`) then
overshooting. `EST` collapsed/hallucinated (`454`, `885`). FAULT_HOLD fired
from `compression_wall_critical` then froze feed 5 s → recovery slam →
TENSION pin.

Control theory: a continuous PI controller toward a setpoint, on a
pure-integrator plant observed only by 3 discrete switch states with
seconds of lag and no inter-crossing feedback, is a relay oscillator. No
gain/setpoint constant fixes a strategy/sensor mismatch. The matched
controller for a type-D sensor is the two-level / hysteretic relay law
(standard MMU buffer design).

## Goals / Non-Goals

**Goals:** stable, slow, shallow, never-TENSION-leaning limit cycle in
type-D standalone mode; stop FAULT_HOLD manufacturing the
pause→slam→TENSION failure.

**Non-Goals:** no analog-mode behavior change; no removal of B/C/F/G/H
(kept for analog); no new observer/estimator; not eliminating the limit
cycle entirely (a type-D sensor with two switches inherently cycles — make
it benign).

## Decisions

### D1 — relay override, keep ramp/clamp/relief

In `BUF_SENSOR_TYPE == 0` (D=0), immediately before the existing ramp/clamp,
override `target_sps`. FLARE polarity: `BUF_TENSION` = empty/starved,
`BUF_COMPRESSION` = full reserve (negative `RT`, REFILL in TENSION, RELIEVE
in COMPRESSION).
`TENSION → base*CATCHUP(1.45)` (strong fixed refill, estimator-independent
safety), `COMPRESSION → SYNC_MIN` (stop; extruder draws the full buffer
down), `NEUTRAL → clamp(EST*NEUTRAL(1.10), SYNC_MIN, base)` where
`base = baseline_control_floor_sps()`. NEUTRAL tracks *demand* (EST), not the
fixed baseline: anchoring NEUTRAL to baseline (~5× real demand on the test
print) rocketed the buffer to the full wall and caused the NEUTRAL↔COMPRESSION
bangbang. The legacy `compression_floor` inherited an old empty/full
assumption and is skipped in relay mode.
Placed after all est/reserve assembly so it discards the dead-reckon
controller while the slow ramp (`SYNC_RAMP_UP/DN`), `[SYNC_MIN,max]`
clamp, fast-brake and relief logic still apply and damp
the cycle. Rationale: minimal blast radius, reuses validated downstream
damping, analog path untouched (guard).

### D2 — NEUTRAL frac carries the never-TENSION lean

`NEUTRAL = EST*1.10` makes the equilibrium drift gently toward COMPRESSION,
so the buffer spends more time on the compression side and the cycle is
never-TENSION-leaning without parking on a wall. Replaces H1/H2 depth/trim
as the lean mechanism in relay mode.

### D3 — switch contact is not a fault

Gate the tension-dwell FAULT_HOLD and `compression_wall_critical` FAULT_HOLD
to `BUF_SENSOR_TYPE != 0` (type P analog, P=1). `compression_wall_critical`
already required a virtual endstop, so it only ever fired in type-D mode and
was purely harmful there. Relay-law catch-up handles an empty buffer; relay
stop handles a full one. Genuine runout/idle is still handled by the existing
RELIEF_PAUSE / continuous-compression auto-stop path (unchanged).

### D4 — tuning knobs

`SYNC_RELAY_CATCHUP_FRAC` / `SYNC_RELAY_NEUTRAL_FRAC` are the primary
on-hardware knobs. Heuristic: CATCHUP high enough that TENSION refills
within a few hundred ms; NEUTRAL just above 1 (a few %) for compression lean.
Period/amplitude tuned so it never reaches a wall hard.

## Risks / Trade-offs

- [Relay still produces a limit cycle] → Inherent to a type-D sensor;
  goal is slow/shallow/benign, not zero. Ramp + fracs control amplitude.
- [Fracs wrong on first flash] → Two `#define`s, named primary knobs;
  CATCHUP↑ if it starves, NEUTRAL↓ for less compression lean.
- [Disarming compression_wall_critical hides a real jam] → A real jam now
  surfaces via the unchanged RELIEF_PAUSE / continuous-compression auto-stop
  and runout paths, without a 5 s feed freeze mid-print.
- [Hardware-only validation] → No MMU on dev host; `TEST_CASES.md`
  regression added.

## Migration Plan

1. Add `SYNC_RELAY_CATCHUP_FRAC` and `SYNC_RELAY_NEUTRAL_FRAC`.
2. Relay override before ramp/clamp, gated `BUF_SENSOR_TYPE == 0` (D=0).
3. Gate the two FAULT_HOLD calls to analog only.
4. Local build; `py_compile`; `openspec validate`.
5. `TEST_CASES.md`; on-Pi A/B; tune the fracs.

Rollback: revert the edits; the PI/reserve path returns for type-D mode.

## Open Questions

- Final frac values — `1.45 / 1.10` are current starting points; on-Pi
  cycle period/amplitude and underextrusion decide.
- Whether NEUTRAL should hold the *last* commanded rate instead of
  `extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC` (smoother but slower to
  settle) — revisit after first run.
