## Scope

**Type-P (`BUF_SENSOR_TYPE=1`) only.** Every behavior change lands behind a
`g_buf_sensor_type == BUF_SENSOR_TYPE_P` branch. Type-D keeps its current
prime rate, fixed-rate follow, and switch-click stop, byte-for-byte. No type-D
hardware validation is in scope; type-D preservation is asserted by construction
(guarded branches) and by diff review, not by a rig run.

One item is type-agnostic on purpose: the prime-cap spec correction (§ Removed
Capabilities) documents the value the firmware *already* uses for both types. It
changes no code and no type-D behavior.

## Why

Print-end retract slams buffer to compression; MMU never catches up. Reported
symptom: buffer sits near compression feeding slowly at print end, slicer end
G-code arms `BL:T` then `END_PRINT` immediately fast-retracts, MMU cannot pick up.

Slicer end G-code in use:

```
_FLARE_BUFFER_STABILIZE
RUN_SHELL_COMMAND CMD=flare PARAMS="BL:T:30:3000"
END_PRINT
```

Five stacked defects, three of them spec violations:

1. **`BL` has no host completion wait.** `scripts/flare_cmd.py:46` `COMPLETION_EVENTS`
   lists only `RL`/`CU`/`CX`. Firmware already emits `EV:BL:LOCKED` and
   `EV:BL:PRIME_BOUND`. CLI returns on daemon ack (~10ms) while prime still runs
   ~1.0s → extruder retract races prime. Same gap for `BS` (`EV:BUF_STAB:DONE`),
   papered over with blind `G4` dwells.

2. **Type-P prime out-gunned.** `sync.c:786` primes type-P at `BUF_STAB_SPS`
   (4092 sps = 10.0 mm/s) because the PSF EMA lags a full-speed prime past
   `PSF_HOME_THRESHOLD_NORM`. Typical end-print retract is 30–60 mm/s. Prime
   loses even when not racing.

3. **Prime cap violates spec.** `buffer-state-lock` "Bounded Half-Travel Prime"
   requires cap `BUF_MAX_TRAVEL_MM / 2`. `sync.c:797` sets
   `g_bl_prime_cap_mm = g_buf_max_travel_mm` — full travel, 2x the contract.

4. **Slam-catch replaced by open-loop follow — undocumented.** `buffer-state-lock`
   "Instant-Slam Catch With Asymmetric Safety" + `sync-state-model` "Catch
   sub-state engages on raw departure" require: mirror-direction drive at
   `GLOBAL_MAX_SPS`, instant `current_sps = target` write, no ramp, `EV:BL:BREAK`.
   Code has no `BL_CATCH` sub-state and no `EV:BL:BREAK`; lock-break enters
   `BL_FOLLOW` at a *host-supplied fixed rate* with a pull-in ramp, and only when
   `follow_mm > 0`. Ramp was a correct hardware fix (instant rate jump stalls the
   motor at pull-in limit) but silently downgraded catch authority from
   83.3 mm/s closed-on-break to whatever the macro guessed. Nothing escalates when
   compression builds — the only feedback is `PSF_FOLLOW_RAIL_NORM`, which gates
   the catch *down*.

5. **No closing `BS`; guaranteed critical event.** `_FLARE_BL_RETRACT` contract:
   caller must send one `BS` when the retract chain ends. Slicer end G-code does
   not. `BL_WATCHDOG_DEFAULT_MS` 30000 fires `cmd_event_critical("BL","TIMEOUT")`
   ~30s after every print end.

Structural cause behind 4: `psf_control_law()` ends `clamp_i(target, 0, max_sps)`
— type-P PD is feed-only, cannot command reverse. Every retract falls back to BL,
so BL open-loop *is* the whole retract control authority.

**The follow ceiling is the binding constraint, not the servo.** `GLOBAL_MAX_SPS`
34101 sps = 83.33 mm/s = 5000 mm/min is a hard wall on how fast the MMU can
follow any retract. Three configured numbers sit wrong against it:

| where | value | vs 83.33 mm/s ceiling |
|---|---|---|
| the reported `BL:T:30:3000` follow rate | 50.0 mm/s | under ceiling, and above the 35 mm/s retract — adequate |
| `_FLARE_VARS.mmu_follow_rate` 6000 mm/min | 100 mm/s | over ceiling, silently clamped |
| `_FLARE_TIP_FORMING_DEFAULTS.park_speed` | 125 mm/s | 50% over — permanent 41.7 mm/s deficit |

Reserve budget for a retract of length `L` at speed `V > 83.33`:
`fill = L * (1 - 83.33/V)`, survivable while `fill <= BUF_MAX_TRAVEL_MM`. At
`V=125` that is `L <= 48 mm`; at `V=100`, `L <= 96 mm`; at `V <= 83.33`,
unbounded. Tip-form park (125 mm/s, ~25–30 mm) survives at ~60% of budget on the
16 mm rig. A print-end retract at the same speed but longer does not. No amount
of catch escalation fixes this class — 83.33 mm/s is the ceiling the servo
escalates *toward*.

Sixth, minor: `_FLARE_BUFFER_STABILIZE` before `BL:T` is counterproductive. `BS`
parks the buffer at `BUF_GOAL` 0.700 raw = `+0.40` norm (compression side);
`BL:T` then travels `+0.40 → -0.90` = 0.65 x `BUF_MAX_TRAVEL_MM` before locking.
Walk east, then run west.

### Measured on the reporting rig

The end-of-print retract is Klippain's `END_PRINT`: `G1 E-40 F2100` =
**40 mm at 35 mm/s**, from `_USER_VARIABLES.retract_length` 40. `CANCEL_PRINT`
repeats the same line.

```
buffer at BS goal +0.40 -> compression stop = 0.60 norm = 4.8 mm
fill rate during prime  = 35 - 10 = 25 mm/s
time to slam            = 4.8 / 25 = 0.19 s

retract duration        = 40 / 35 = 1.14 s
PRIME_BOUND fires at    = 16 / 10 = 1.60 s   (after the retract already ended)
```

35 mm/s is far under the 83.33 mm/s follow ceiling, so the reserve budget is
never in play, and the 50 mm/s follow rate out-runs the retract comfortably.
**The failure is the ordering race plus an under-budgeted follow distance**: the
`30` in `BL:T:30:3000` was for a 40 mm retract. Load-bearing fixes are A (host
wait) and C (fast prime); D is spec-debt repayment and insurance for retracts
this rig does not currently issue.

The half-travel subtraction at `sync.c:800` is deliberate — it parks the follow
at NEUTRAL rather than at the rail — so the correct argument is the true retract
length (`BL:T:40:...`), not a padded one.

## What Changes

- **A. Host completion wait.** Add `BL` and `BS` to `flare_cmd.py`
  `COMPLETION_EVENTS`. `BL` completes on `EV:BL:LOCKED` / `EV:BL:PRIME_BOUND`,
  errors on `EV:BL:TIMEOUT`. `BS` completes on `EV:BUF_STAB:DONE`, errors on
  `EV:BUF_STAB:TIMEOUT` / `EV:BUF_STAB:STAGNANT_TIMEOUT`.
- **B. Macros stop guessing.** `_FLARE_BUFFER_STABILIZE` drops the blind `G4`
  settle (CLI now blocks on `EV:BUF_STAB:DONE`); `SETTLE` kept as an optional
  extra dwell, default `0`. `_FLARE_BL_RETRACT` drops `PAUSE` for the same
  reason. Remove the pre-`BL:T` `BS` from the retract path.
- **C. Fast type-P prime.** Drop the type-P `BUF_STAB_SPS` branch at
  `sync.c:786`; type-P then falls through to the existing `SYNC_MAX_SPS` prime
  already used by type-D. Stop on a predict-ahead threshold
  (`g_buf_pos + BL_PRIME_PREDICT_LEAD_S * g_vel_norm_f`) instead of the raw
  threshold — same trick already used for boot stabilize
  (`SYNC_STAB_PREDICT_LEAD_S`, `sync.c:311`). Restores 36.7 mm/s prime authority
  without slamming the rail. Type-D prime rate and switch-click stop untouched.
- **D. Servo catch replaces fixed-rate follow (type-P).** `BL_FOLLOW` becomes a
  rate-servo catch: seeded at the host `follow_rate` (back-compat), ramped
  pull-in-safe, and *escalated toward `GLOBAL_MAX_SPS` proportional to buffer
  error away from the armed rail*. Needs a continuous mid-band position signal,
  which only type-P has (`typed-buffer-no-midband-groundtruth`), so the
  escalation is type-P-gated and type-D keeps the fixed seed rate. Keeps the
  existing `PSF_FOLLOW_RAIL_NORM` down-gate. Emit `EV:BL:BREAK` on lock-break
  (both types — event only, no motion change). Host `follow_rate` becomes a
  floor/seed, not a ceiling.
- **E. Injectable retract guard.** FLARE cannot own a retract buried inside a
  third-party macro: Klippain's `END_PRINT` retracts from inside an
  `{% elif printer.extruder.can_extrude %}` branch, driven by a foreign variable,
  and `CANCEL_PRINT` duplicates it. So `flare_mmu.cfg` ships two primitives —
  `_FLARE_RETRACT_GUARD_BEGIN LENGTH= SPEED= TIMEOUT=` (arms `BL:T`, blocking)
  and `_FLARE_RETRACT_GUARD_END` (closing `BS` + `_FLARE_SYNC_TOOLHEAD`) — and
  `KLIPPER.md` documents a `rename_existing` wrapper recipe the operator applies
  to any macro, by any name, in any plugin set. FLARE ships no macro named
  `END_PRINT`, so it never collides with the host config or another wrapper.
  `LENGTH` is the true retract length: the `sync.c:800` half-travel subtraction
  is intentional and parks the follow at NEUTRAL.
- **H. Host-settable buffer-lock watchdog.** Wrapping a whole foreign macro holds
  the lock far longer than a bare retract. Extend the command to
  `BL:<state>:<follow_mm>:<follow_rate>:<timeout_ms>`; the 4th field is optional
  and defaults to `BL_WATCHDOG_DEFAULT_MS` (30000) when omitted.
  `_FLARE_RETRACT_GUARD_BEGIN` exposes it as `TIMEOUT=`, defaulting to 120 s.
  No `settings_t` or flash change — the value is per-arm, not persisted.
- **F. Reserve-budget warning.** The MMU follow ceiling is `GLOBAL_MAX_SPS` =
  83.33 mm/s. A retract faster than that spends buffer reserve at
  `(SPEED - 83.33)` mm/s; the reserve is `BUF_MAX_TRAVEL_MM`. `_FLARE_BL_RETRACT`
  SHALL `RESPOND` a warning when `LENGTH > BUF_MAX_TRAVEL_MM / (1 - 83.33/SPEED)`.
  Warn only — no clamp, no behavior change. `_FLARE_TIP_FORMING_DEFAULTS.park_speed`
  is 125 mm/s (50% over ceiling) and stays as-is.
- **G. Follow-rate honesty.** `_FLARE_VARS.mmu_follow_rate` is 6000 mm/min =
  100 mm/s, silently clamped to the 5000 mm/min ceiling. Set it to `5000.0` and
  document it as the firmware ceiling, so the configured number matches the
  commanded one. Fix the `speed_mmpm` misnomer in `_FLARE_BL_RETRACT`: the
  variable holds mm/s (`F{speed_mmpm*60}`), not mm/min.
- **Spec reconcile.** Prime cap: adopt full `BUF_MAX_TRAVEL_MM` in spec (the
  implemented value; half-travel is too short to reach the rail from a
  `BUF_GOAL`-parked buffer) and rename the requirement. Catch: replace
  instant-slam wording with the ramped servo. Reconcile the two conflicting catch
  rate clauses (`buffer-state-lock` says `GLOBAL_MAX_SPS`, `sync-state-model` says
  `min(GLOBAL_MAX_SPS, SYNC_MAX_SPS)`) onto `GLOBAL_MAX_SPS`. Document type-P BL
  behavior in `buffer-state-lock` (Purpose still says type-D only).

## Capabilities

### New Capabilities

- `klipper-mmu-config`: `_FLARE_RETRACT_GUARD_BEGIN` / `_FLARE_RETRACT_GUARD_END`
  injectable primitives; buffer helper macros wait on events instead of `G4`
  dwells; retract reserve-budget warning.
- `buffer-state-lock`: optional `timeout_ms` field on the `BL` command surface.
- `klipper-integration`: `BL` / `BS` block until the firmware reports completion.
- `psf-type-p-sensor`: type-P BL prime uses a predictive rail stop.
- `buffer-state-lock`: prime rate + type-P rail prediction; bounded prime travel
  (replaces the half-travel cap).

### Modified Capabilities

- `buffer-state-lock`: servo catch replaces instant-slam; `EV:BL:BREAK` restored;
  bare `BL:T` gets a default catch budget.
- `sync-state-model`: catch sub-state rate contract deferred to
  `buffer-state-lock` (resolves the `GLOBAL_MAX_SPS` vs
  `min(GLOBAL_MAX_SPS, SYNC_MAX_SPS)` conflict).

### Removed Capabilities

- `buffer-state-lock`: "Bounded Half-Travel Prime" — the half-travel cap makes
  the rail unreachable from a `BUF_GOAL`-parked buffer. Replaced by "Bounded
  Prime Travel" at full `BUF_MAX_TRAVEL_MM`, matching the implementation.

## Impact

- `firmware/src/sync.c`: `sync_buffer_lock_arm` (+ `timeout_ms` argument),
  `sync_buffer_lock_prime`, `sync_buffer_lock_locked`, `sync_buffer_lock_follow`.
- `firmware/src/protocol.c`: `BL` parser gains an optional 4th field
  (`:1752`, doc block `:1625`).
- `firmware/include/sync_internal.h`: `BL_PRIME_PREDICT_LEAD_S`,
  `BL_CATCH_ESCALATE_*` constants.
- `scripts/flare_cmd.py`: `COMPLETION_EVENTS`.
- `klipper/flare_mmu.cfg`: `_FLARE_BUFFER_STABILIZE`, `_FLARE_BL_RETRACT`,
  `_FLARE_PRINT_END` (new); all `_FLARE_BUFFER_STABILIZE SETTLE=` call sites;
  `_FLARE_VARS` (`print_end_retract_len`, `print_end_retract_speed`,
  `mmu_follow_ceiling`, `buf_max_travel_mm`, `mmu_follow_rate` 6000 -> 5000).
- **Operator action, not a break**: the wrapper recipe is additive — foreign
  macros are wrapped by `rename_existing`, never edited. Nothing in the host
  config is deleted, and FLARE defines no macro that could collide with it. The
  include must be ordered after the wrapped macros' definitions.
- `KLIPPER.md`, `MANUAL.md` (BL event contract), `TEST_CASES.md`.
- No `settings_t` / flash-layout change. No `config.ini` / `tune.h` change
  (new constants are compile-time, not runtime tunables).
- **HW validation required**: print-end retract on the type-P rig. Needs the
  actual `END_PRINT` retract length + feedrate recorded first (see design
  "Open Inputs").
