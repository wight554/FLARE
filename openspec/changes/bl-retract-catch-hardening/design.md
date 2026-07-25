## Context

### Rig numbers

`g_mm_per_step` = `0.0024437` mm (`CONF_L1_MM_PER_STEP`). `BUF_MAX_TRAVEL_MM` =
16 on the type-P rig (default `CONF_BUF_MAX_TRAVEL_MM` 25).

| quantity | sps | mm/s | source |
|---|---|---|---|
| `BUF_STAB_SPS` — type-P BL prime rate | 4092 | 10.0 | `tune.h:45`, `sync.c:786` |
| `SYNC_MAX_SPS` — PD ceiling, type-D prime | 15004 | 36.7 | `tune.h:46` |
| `GLOBAL_MAX_SPS` — BL follow/catch ceiling | 34101 | 83.3 | `tune.h:76`, `motion.c:54` |
| observed `BL:T:30:3000` follow | 20460 | 50.0 | not clamped |

Ramp: `RAMP_STEP_SPS` 5115 per `RAMP_TICK_MS` 5 = 1.02e6 sps/s. 0 → 20460 sps in
20 ms. Ramp is not the bottleneck.

### Failure timeline (reproduced from code, rig numbers)

```
t=0.00  _FLARE_BUFFER_STABILIZE
        BS drives buffer to BUF_GOAL 0.700 raw = +0.40 norm  ← COMPRESSION side
        G4 P2000 blind dwell

t=2.00  RUN_SHELL_COMMAND "BL:T:30:3000"
        daemon acks arm ~10ms → CLI RETURNS
        BL_PRIME at 10.0 mm/s; needs +0.40 → -0.90 norm
        = 0.65 x 16mm = 10.4 mm ≈ 1.05 s

t=2.01  END_PRINT retract at ~40 mm/s
        MMU retracting 10, extruder pushing slack in 40 → net fill +30 mm/s
        buffer hits compression stop in ~0.5 s

t=3.6   PRIME_BOUND (16mm cap @10mm/s) → BL_LOCKED → lock_broken → BL_FOLLOW 50 mm/s
        retract already finished; follow yanks an empty buffer

t=32    EV:BL:TIMEOUT (critical) — no closing BS
```

```
 buffer pos
  +1 COMP |        ,-------- SLAM
          |       /
  +0.4 ---+------X BS parks here (wrong side)
     0    |      / \ prime crawls at 10mm/s
          |         \
  -0.9 ---+- - - - - \- - - target (never reached)
  -1 TENS |           `
          +--+---+---+---+-->
             2  2.5  3  3.6 s
```

### Goal-norm derivation

`psf_goal_norm()` (`sync_buf.c:427`) with `BUF_PSF_MAX_COMP` 1.000,
`BUF_PSF_NEUTRAL` 0.500, `BUF_PSF_MAX_TENS` 0.000, `BUF_GOAL` 0.700, not
reversed: `goal_norm = -(0.700-0.500)/(1.000-0.500) = -0.40`, returned negated =
`+0.40`. Compression side confirmed.

While `BL` is armed, `psf_goal_norm()` short-circuits to `-1.0` (`BUF_TENSION`)
/ `+1.0` (`BUF_COMPRESSION`) via `g_bl_goal_override`, so an idle stabilize and
the PD both pull toward the armed rail. A `BS` immediately *before* the arm
clears the override and parks at `BUF_GOAL` first — pure loss.

### Spec vs code divergence found

| spec clause | code | verdict |
|---|---|---|
| `buffer-state-lock` Bounded Half-Travel Prime: cap `BUF_MAX_TRAVEL_MM / 2` | `sync.c:797` cap = `g_buf_max_travel_mm` | code 2x spec |
| `buffer-state-lock` Instant-Slam Catch: mirror dir at `GLOBAL_MAX_SPS`, instant write, no ramp | no `BL_CATCH` sub-state; `BL_FOLLOW` at host rate, ramped, only if `follow_mm > 0` | code diverged |
| `buffer-state-lock` Lock-Break: emit `EV:BL:BREAK` | emits `EV:BL:FOLLOW` | event renamed silently |
| `sync-state-model` catch at `min(GLOBAL_MAX_SPS, SYNC_MAX_SPS)` | — | conflicts with `buffer-state-lock` |
| `buffer-state-lock` Purpose: "type-D buffers" | type-P branches throughout `sync_buffer_lock_*` | scope stale |

Divergence origin: `b499f4a` "sync: implement buffer-lock prime ramping and
remove phase-2 settle" and the follow-ramp fix recorded in personal memory
`bl-follow-instant-rate-stall` ("instant `motor_set_rate_sps` jump stalled;
ramps follow to ~83mm/s"). The ramp is correct — the RP2040 stepper cannot
take an instant 0 → 34101 sps step (pull-in limit). The spec was never updated,
and the fix also swapped closed-on-break authority for an open-loop host rate.

### Type-P catch limit cycle (bounded, accepted)

Type-P `BL_FOLLOW` has two thresholds, both on `g_buf_pos`:

- gate down to `BL_LOCKED` at `PSF_FOLLOW_RAIL_NORM` = `0.95` (`sync.c:948`)
- break back to `BL_FOLLOW` at `PSF_HOME_THRESHOLD_NORM` = `0.90` (`sync.c:898`)

0.05 norm of hysteresis. Under a sustained external retract the catch cycles
gate → locked → break → ramp → gate, oscillating the buffer over
`0.05 / 2 * BUF_MAX_TRAVEL_MM` = 0.4 mm on the 16 mm rig. Ramp restart is 20 ms,
negligible. Bounded and small, but the servo (D) makes each cycle steeper, so
the cycle gets louder even though its amplitude does not grow.

Accepted for this change: the hard gate stays. A continuous law that folds
PRIME / LOCKED / FOLLOW into one type-P servo with rate → 0 at the rail would
remove the cycle entirely, but it rewrites the prime — the riskiest part, and
the part with a rail-slam failure mode — before any HW data exists. Revisit as a
follow-on once C and D are measured on the rig.

### Watch-item: `buf_state_raw()` is goal-relative under an armed lock

`buf_state_raw()` for type-P (`sync_buf.c:485`) zones against `psf_goal_norm()`
with a 0.1 deadband. While `BL:T` is armed the goal override pins that to
`-1.0`, so `BUF_TENSION` needs `buf_pos < -1.1` — unreachable — and everything
above `-0.9` reads `BUF_COMPRESSION`. The BL type-P paths are unaffected (they
compare `g_buf_pos` directly at `sync.c:846/898/948`), but `buf_read_stable()`
publishes from `buf_state_raw()`, so `g_buf.state` reads `BUF_COMPRESSION` for
nearly the whole span while a tension lock is armed.

Mostly inert — `SYNC_RETRACT_ASSIST` suppresses closed-loop sync, and fault
timers are already scoped to active sync (`typep-stale-fault-timers`). Two
consumers still read it live and are worth a look during implementation:
`toolchange.c:589` (`RELOAD_FOLLOW` success test, adjacent to
`typep-reload-no-consumer`) and `sync_effective_kp_sps(g_buf.state)`.
Pre-existing, not caused by this change; do not fix here unless HW shows it.

### Why the PD cannot help

`psf_control_law()` (`sync_analog.c:66`) returns `clamp_i(target, 0, max_sps)`.
Feed-only. Type-P sees demand directly, but only in the feed direction — there
is no reverse term. `SYNC_RETRACT_ASSIST` + BL is the entire retract control
path, and it is open-loop.

## Decisions

- **Type-P only; type-D preserved by construction.** The rig is type-P and the
  failure is type-P. Both behavior changes (C, D) sit behind
  `g_buf_sensor_type == BUF_SENSOR_TYPE_P`. C is preservation-by-deletion: type-P
  currently *branches away* from the shared `SYNC_MAX_SPS` prime, so removing the
  branch leaves type-D's expression untouched. D needs an explicit type-P gate
  because it adds a new term to a shared function. No type-D rig run; diff review
  plus the guarded branches carry it.

- **Fix ordering before authority.** A (host wait) + B (drop blind dwells) + E
  (`_FLARE_PRINT_END`) are cheap and remove the race entirely. Land them first
  and measure. C and D raise authority and are only load-bearing if the retract
  genuinely out-runs the follow rate — but both are also spec-debt repayment, so
  they land regardless.

- **Prime cap: adopt full `BUF_MAX_TRAVEL_MM` in spec, not half in code.** The
  half-travel cap predates the `BUF_GOAL`-parked idle position. From `+0.40`
  norm the rail is `0.65 x BUF_MAX_TRAVEL_MM` away — over half. Enforcing the
  spec value would make `PRIME_BOUND` the *normal* outcome and never reach the
  rail. Rename requirement to "Bounded Prime Travel". Full travel is still a
  hard bound: the buffer physically cannot exceed it.

- **Type-P prime: full `SYNC_MAX_SPS` + predict-ahead stop.** Reuses the boot
  stabilize pattern verbatim (`sync.c:311`,
  `predicted = g_buf_pos + LEAD_S * g_vel_norm_f`). The gentle-prime comment at
  `sync.c:783` is correct about the cause (EMA lag) but picked the wrong cure
  (slow down instead of lead the estimate). New compile-time
  `BL_PRIME_PREDICT_LEAD_S`, seeded `0.10f` to match
  `SYNC_STAB_PREDICT_LEAD_S`; tune on HW. Type-D unchanged (bang-bang click
  stops it instantly) — it already primes at `SYNC_MAX_SPS`, so this is the
  removal of a type-P-only exception, not a shared-path edit.

- **Servo catch, not instant slam.** Restore closed-on-break authority without
  reintroducing the pull-in stall:

  ```
  err     = |buf_pos - armed_rail_norm|          (0 at rail, ~2.0 fully opposite)
  target  = follow_seed + (GLOBAL_MAX - follow_seed) * clamp(err / BL_CATCH_ERR_SPAN, 0, 1)
  rate    -> target via existing RAMP_STEP_SPS / RAMP_TICK_MS ramp
  ```

  Host `follow_rate` becomes the seed/floor (back-compat: existing macros keep
  their current minimum behavior), `GLOBAL_MAX_SPS` the ceiling. Retains the
  `PSF_FOLLOW_RAIL_NORM` 0.95 down-gate → `BL_LOCKED` so the catch still cannot
  slam the armed rail. This is the smallest change that makes a too-fast
  extruder retract self-correcting.

- **`follow_mm` stays a budget, not a gate.** Today `BL_FOLLOW` is entered only
  when `follow_mm > 0`; a bare `BL:T` holds passively through an arbitrarily
  large external retract. Keep the distance budget (it bounds runaway) but allow
  catch entry with a default budget when the host supplied none, so a bare
  `BL:T` is not silently authority-free.

- **`EV:BL:BREAK` restored, `EV:BL:FOLLOW` kept as the catch-start event.** Emit
  `BREAK` on the raw departure edge (spec contract, host-observable), then
  `FOLLOW` when the catch actually starts driving. Two events, distinct
  meanings; no host currently keys on either.

- **CLI waits, macros do not dwell.** `G4` dwell is a blind guess for an event
  the firmware already publishes. `flare_cmd.py` runs in its own process and
  reads the daemon SSE stream, so it is not starved by the Klipper gcode lock
  (per `klipper-gcode-lock-starves-setmmu`: never poll `printer.mmu` mid-command;
  reading the daemon directly is the sanctioned path). Keep `SETTLE` as an
  optional *extra* dwell (default `0`) so a rig needing mechanical settle beyond
  the event can still ask for it.

- **Guard primitives + documented recipe; FLARE owns no foreign macro name.**
  An earlier draft had `_FLARE_PRINT_END` perform the retract itself. The
  injectability requirement kills that: Klippain retracts from inside
  `{% elif printer.extruder.can_extrude %}`, driven by
  `_USER_VARIABLES.retract_length`, and `CANCEL_PRINT` duplicates the line —
  FLARE cannot take it over without the operator editing a third-party file.
  Ship `_FLARE_RETRACT_GUARD_BEGIN` / `_FLARE_RETRACT_GUARD_END` and document a
  `rename_existing` wrapper:

  ```
  [gcode_macro END_PRINT]
  rename_existing: _FLARE_INNER_END_PRINT
  gcode:
      _FLARE_RETRACT_GUARD_BEGIN LENGTH=40 SPEED=35
      _FLARE_INNER_END_PRINT
      _FLARE_RETRACT_GUARD_END
  ```

  Works for any macro, any name, any plugin set, any number of wrap sites
  (`END_PRINT` and `CANCEL_PRINT` both need it). Rejected shipping the wrapper
  inside `flare_mmu.cfg`: it would hard-code `END_PRINT` / `CANCEL_PRINT`, fail
  at config load when those names do not exist, depend on include ordering, and
  collide with any other plugin that also wraps them.

- **Zero-macro auto-detection ruled out for this change.** Checked whether the
  firmware could see the retract itself: `g_extruder_est_sps` is reconstructed
  inside the firmware from buffer motion (`sync_buf.c:221`), never host-fed, and
  `klipper_motion_tracker.py` is a tuner sidecar whose spec requires
  "Host-Only Integration ... SHALL NOT require firmware changes" — it feeds
  markers to the tuner, not the controller. A macro-free guard would need a new
  host-to-board signed extruder-velocity command plus a signed control law
  (`psf_control_law` is `clamp_i(target, 0, max_sps)`). Out of scope; note it as
  the long-term shape.

- **`LENGTH` is the true retract length, unpadded.** The `sync.c:800` half-travel
  subtraction is deliberate: `follow = LENGTH - BUF_MAX_TRAVEL_MM/2`, so after
  the extruder pulls `LENGTH` the buffer nets half-travel from the tension rail
  and parks at NEUTRAL, not one step from the mechanical end. For the reporting
  rig: `follow 40-8 = 32`, extruder 40, net 8 mm, buffer -8 mm -> 0 mm. The
  reported `BL:T:30:...` was simply 10 mm short of the 40 mm retract; no padding
  was ever needed.

- **Watchdog becomes a per-arm host argument.** Wrapping a whole macro holds the
  lock across `PARK`, heater changes, and `M400`, not just the retract. Extend
  the command to `BL:<state>:<follow_mm>:<follow_rate>:<timeout_ms>`, 4th field
  optional, defaulting to `BL_WATCHDOG_DEFAULT_MS` 30000 when absent so every
  existing caller is unchanged. Chosen over a `SET BL_WATCHDOG_MS` runtime knob,
  which would be one global for all BL users and would drag `settings_t` and the
  flash layout into a change that otherwise touches neither. Guard default 120 s.

- **Params with var defaults, not either/or.** `params.X|default(v.y)|float` is
  already the idiom throughout `_FLARE_TIP_FORMING`. `_FLARE_VARS` gains
  `print_end_retract_len` and `print_end_retract_speed`; `LENGTH=` / `SPEED=`
  override per call. Slicers that vary retract by filament pass params; everyone
  else sets the vars once.

- **Reserve budget: warn, never clamp.** Chosen over clamping `SPEED` to the
  ceiling. Clamping would slow the tip-form park retract from 125 to 83.33 mm/s
  — a 33% change to a tip-quality-critical move, needing reprint validation for
  a problem that is currently surviving. `_FLARE_BL_RETRACT` instead `RESPOND`s
  a warning when `LENGTH > buf_max_travel_mm / (1 - mmu_follow_ceiling/SPEED)`,
  so the operator is told when a retract is over budget and by how much.

- **Ceiling and travel mirrored into `_FLARE_VARS`, with a drift guard.** The
  warning needs `GLOBAL_MAX_SPS` (83.33 mm/s) and `BUF_MAX_TRAVEL_MM` (16), both
  firmware-side. Klipper cannot read them synchronously without a blocking shell
  call on every retract, so they are mirrored as
  `_FLARE_VARS.mmu_follow_ceiling` / `buf_max_travel_mm`. Duplicated constants
  drift: the vars carry a comment naming `flare_cmd.py --dump` as the source of
  truth, and a task cross-checks them.

- **`mmu_follow_rate` 6000 -> 5000.** 6000 mm/min is 100 mm/s, above the
  5000 mm/min ceiling, so the firmware clamps it and the configured number never
  equals the commanded one. The existing cfg comment calls this "harmless" —
  true for behavior, misleading for anyone reading the file to learn the real
  follow rate. Set it to the ceiling and say so.

- **`speed_mmpm` is mm/s.** `_FLARE_BL_RETRACT` does `F{speed_mmpm*60}`, so the
  variable holds mm/s and the `_mmpm` suffix is a misnomer. Call sites pass mm/s
  (`speed_hub_to_extruder` 50.0, `park_speed` 125.0) and are correct; only the
  name lies. Rename to `speed_mm_s`.

## Follow ceiling arithmetic

`GLOBAL_MAX_SPS` 34101 * 0.0024437 = **83.33 mm/s = 5000 mm/min**. Hard wall on
follow rate, and the value the catch servo escalates toward.

For a retract of length `L` at speed `V`:

```
V <= 83.33            -> MMU matches; no buffer fill; L unbounded
V >  83.33            -> fill_rate = V - 83.33
                         fill      = L * (1 - 83.33 / V)
                         survivable while fill <= BUF_MAX_TRAVEL_MM
                         L_max     = BUF_MAX_TRAVEL_MM / (1 - 83.33 / V)
```

| V (mm/s) | fill rate | `L_max` at 16 mm reserve |
|---|---|---|
| 125 (`park_speed`) | 41.7 mm/s | 48 mm |
| 100 (`mmu_follow_rate` as written) | 16.7 mm/s | 96 mm |
| 83.33 (ceiling) | 0 | unbounded |
| 50 (the reported `BL:T:...:3000`) | 0 | unbounded |

Tip-form park is ~25–30 mm at 125 mm/s = ~60% of a 48 mm budget. Survives, with
no margin documented anywhere. This is the arithmetic the `_FLARE_BL_RETRACT`
warning encodes.

## Open Inputs

- **Resolved.** Rig runs Klippain: `END_PRINT` and `CANCEL_PRINT` both do
  `G1 E-{retract_length} F2100` with `_USER_VARIABLES.retract_length` 40 =
  **40 mm at 35 mm/s**. Under the 83.33 mm/s ceiling, so the reserve budget is
  not in play and the 50 mm/s follow rate was adequate. Failure = ordering race
  (prime unfinished, buffer slams in 0.19 s) + follow distance 30 for a 40 mm
  retract. A and C are load-bearing; D is insurance and spec-debt repayment.
  Values seed `_FLARE_VARS.print_end_retract_len` / `print_end_retract_speed`.
- `BUF_MAX_TRAVEL_MM` on the rig assumed 16 (personal memory
  `typep-stabilize-overshoot-compression`). Confirm with `GET` before tuning
  `BL_CATCH_ERR_SPAN_NORM` and before seeding `_FLARE_VARS.buf_max_travel_mm`.

## Risks

- **Fast type-P prime overshoots the rail** if `BL_PRIME_PREDICT_LEAD_S` is too
  small — the exact failure the gentle prime was added to avoid. Mitigation:
  land C behind HW validation on a bench `BL:T` before trusting it in a print;
  `PSF_FOLLOW_RAIL_NORM`-style gating does not cover prime. Fallback: revert to
  `BUF_STAB_SPS` prime, keep A+B+E+D.
- **Servo catch escalation is positive feedback against a stalled path.** If the
  MMU cannot move (jam, path friction), error grows, catch escalates, motor
  skips harder. Existing `BL_WATCHDOG_DEFAULT_MS` bounds it at 30s; the
  `follow_mm` distance budget bounds it sooner. Watch for `FOLLOW_JAM`-class
  regressions (cf. `2b35bc9`).
- **Blocking CLI stalls the gcode queue.** `flare_cmd.py` blocking on
  `EV:BL:LOCKED` holds the Klipper shell command open for the prime duration
  (~0.3s after C, ~1.0s before). Acceptable at print end / toolchange; the
  existing `RL`/`CU` waits already work this way. `--timeout` guards a lost event.
- **Spec-cap change is a real contract loosening** (half → full travel).
  Documented as a deliberate correction with rationale, not a silent widening.
