## Context

User-confirmed hardware: standalone buffer = 2 microswitches only
(TRAILING/ADVANCE). No analog position, no Klipper extruder feedback. Real
prints; `AV:0.00` between crossings is normal for virtual-endstop mode.

Symptom data: with `RT` at -3.90 (H1) the buffer still slammed ±7.8
wall-to-wall every cycle, passing the target for one tick (`RE:0.27`) then
overshooting. `EST` collapsed/hallucinated (`454`, `885`). FAULT_HOLD fired
from `trailing_wall_critical` then froze feed 5 s → recovery slam → ADVANCE
pin.

Control theory: a continuous PI controller toward a setpoint, on a
pure-integrator plant observed only by 3 discrete switch states with
seconds of lag and no inter-crossing feedback, is a relay oscillator. No
gain/setpoint constant fixes a strategy/sensor mismatch. The matched
controller for a 2-switch buffer is a hysteretic relay (standard MMU buffer
design).

## Goals / Non-Goals

**Goals:** stable, slow, shallow, never-ADVANCE-leaning limit cycle in
2-switch standalone mode; stop FAULT_HOLD manufacturing the
pause→slam→ADVANCE failure.

**Non-Goals:** no analog-mode behavior change; no removal of B/C/F/G/H
(kept for analog); no new observer/estimator; not eliminating the limit
cycle entirely (a 2-switch buffer inherently cycles — make it benign).

## Decisions

### D1 — relay override, keep ramp/clamp/relief

In `BUF_SENSOR_TYPE == 0`, immediately before the existing ramp/clamp,
override `target_sps`. FLARE polarity: `BUF_ADVANCE` = empty/starved,
`BUF_TRAILING` = full reserve (negative `RT`, REFILL in ADVANCE, RELIEVE
in TRAILING).
`ADVANCE → base*CATCHUP(1.45)`, `TRAILING → base*BACKOFF(0.35)`,
`MID → base*MID(1.05, gentle overfeed toward the full reserve side)`,
where `base = baseline_control_floor_sps()`. The legacy `trailing_floor`
(assumes trailing = empty) is skipped in relay mode so back-off is not
defeated.
Placed after all est/reserve assembly so it discards the dead-reckon
controller while the slow ramp (`SYNC_RAMP_UP/DN`), `[SYNC_MIN,max]`
clamp, `trailing_floor`, fast-brake and relief logic still apply and damp
the cycle. Rationale: minimal blast radius, reuses validated downstream
damping, analog path untouched (guard).

### D2 — MID frac < 1 carries the never-ADVANCE lean

`MID = base*0.97` makes the equilibrium drift gently toward TRAILING, so
the buffer spends more time on the trailing side and the cycle is
never-ADVANCE-leaning without parking on a wall. Replaces H1/H2 depth/trim
as the lean mechanism in relay mode.

### D3 — switch contact is not a fault

Gate the advance-dwell FAULT_HOLD and `trailing_wall_critical` FAULT_HOLD
to `BUF_SENSOR_TYPE != 0`. `trailing_wall_critical` already required a
virtual endstop, so it only ever fired in 2-switch mode and was purely
harmful there. Relay catch-up + `trailing_floor` handle an empty buffer;
relay back-off handles a full one. Genuine runout/idle is still handled by
the existing RELIEF_PAUSE / continuous-trailing auto-stop path (unchanged).

### D4 — tuning knobs

`SYNC_RELAY_CATCHUP_FRAC` / `BACKOFF_FRAC` / `MID_FRAC` are the primary
on-hardware knobs. Heuristic: CATCHUP high enough that TRAILING refills
within a few hundred ms; BACKOFF low enough that ADVANCE drains; MID just
below 1 (a few %). Period/amplitude tuned so it never reaches a wall hard.

## Risks / Trade-offs

- [Relay still produces a limit cycle] → Inherent to a 2-switch buffer;
  goal is slow/shallow/benign, not zero. Ramp + fracs control amplitude.
- [Fracs wrong on first flash] → Three `#define`s, named primary knobs;
  CATCHUP↑ if it starves, BACKOFF↓ if it pins ADVANCE, MID↓ for more
  trailing lean.
- [Disarming trailing_wall_critical hides a real jam] → A real jam now
  surfaces via the unchanged RELIEF_PAUSE / continuous-trailing auto-stop
  and runout paths, without a 5 s feed freeze mid-print.
- [Hardware-only validation] → No MMU on dev host; `TEST_CASES.md`
  regression added.

## Migration Plan

1. Add `SYNC_RELAY_{CATCHUP,BACKOFF,MID}_FRAC`.
2. Relay override before ramp/clamp, gated `BUF_SENSOR_TYPE == 0`.
3. Gate the two FAULT_HOLD calls to analog only.
4. Local build; `py_compile`; `openspec validate`.
5. `TEST_CASES.md`; on-Pi A/B; tune the fracs.

Rollback: revert the edits; the PI/reserve path returns for 2-switch mode.

## Open Questions

- Final frac values — `1.45 / 0.35 / 0.97` are starting points; on-Pi
  cycle period/amplitude and underextrusion decide.
- Whether MID should hold the *last* commanded rate instead of a fixed
  `base*0.97` (smoother but slower to settle) — revisit after first run.
