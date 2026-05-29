## Context

FLARE firmware supports two buffer sensor types via `BUF_SENSOR_TYPE`:
- **Type-D (0)**: two digital switches (TENSION, COMPRESSION); position
  dead-reckoned between crossings via virtual position tick; relay bangbang
  control law.
- **Type-P (1)**: PSF analog sensor on GP29/ADC3; continuous position in
  normalized [-1,1]; continuous PD control law.

Type-P was stubbed with a symmetric normalization (`BUF_RANGE`, `BUF_INVERT`),
no endpoint calibration, a unit mismatch in the PD loop (normalized pos vs
mm target), and an inverted compression-floor that fights drain when buffer
is full. None of this was observable on type-D hardware; the `pending-analog-rig`
change tracked it as deferred. PSF hardware now exists. This change **supersedes
and merges** `pending-analog-rig`: its three carried items (#6 compression_floor,
#7 compression_recovery, H2 estimator drag) are folded in here and resolved
on-rig, so that tracker is removed.

Type-D PD infrastructure (`reserve_error_mm`, `kp_window`, `compression_recovery`
cap, `sync_apply_scaling`, integral centering) duplicates what type-P needs.
The divergence is purely a unit/scale mismatch, not a logic difference.

## Goals / Non-Goals

**Goals:**
- Calibrated PSF support: endpoint calibration commands, asymmetric
  normalization, auto-polarity (no explicit invert flag).
- Goal-relative zone control: TENSION/NEUTRAL/COMPRESSION boundaries derived
  from `BUF_GOAL` + deadband; "neutral" means "near goal."
- Unified PD infrastructure: both types produce a common normalized position
  and target before shared PD math; zero duplicated control logic.
- Isolated control laws: `relay_control_law()` (type-D) and `psf_control_law()`
  (type-P) as separate static functions.
- Continuous-measurement control for type-P: live extruder estimator, PD +
  filtered derivative, control dead zone, soft walls, hard catch with stop/
  slowdown disambiguation.
- Delete type-D estimator-compensation machinery from the type-P path (D9).
- Resolve `pending-analog-rig` deferred items #6, #7, H2.
- User-facing params in raw ADC [0,1] space — same convention as Happy Hare.

**Non-Goals:**
- EKF or autotune (Happy Hare feature) — direct measurement makes EKF
  unnecessary; EWMA + filtered derivative suffice.
- Changes to type-D relay control law behavior.
- Changes to type-D virtual position tick or drift observer (gated, untouched).
- Host/daemon/WebUI changes — firmware only. (Tip-shaping retract removal, if
  validated, is a host-side follow-up, not in this change.)

**Stretch (rig-gated, non-blocking):**
- Loop-rate bump for type-P (D16).
- Validate tip-shaping retract elimination hypothesis (D17).

## Decisions

### D1 — Common normalized scale [-1,1] for PD, raw [0,1] for user params

PD math normalizes to [-1,1] internally:
- Type-D: `buf_pos_norm() = g_buf_pos / buf_threshold_mm()`
- Type-P: `buf_pos_norm() = g_buf_pos` (already normalized from calibration)
- Both produce `error_norm = pos_norm - target_norm`; all shared PD math
  operates on `error_norm` with no sensor-type branches.

User-facing params (`BUF_PSF_MAX_COMP`, `BUF_PSF_MAX_TENS`, `BUF_PSF_NEUTRAL`,
`BUF_GOAL`) are all raw ADC fraction [0,1]. Same convention as Happy Hare
`sync_feedback_analog_max_compression` etc. Users calibrate in the space they
observe; the firmware converts once at normalization time.

*Alternative rejected*: keep everything in mm (scale type-P to mm at PD
entry). Rejected because `buf_threshold_mm()` = `BUF_SWITCH_SPAN_HALF_MM`
for type-D, which has no physical analog for type-P. Normalizing to [-1,1]
is the natural common unit for both "fraction of range to limit."

### D2 — Auto-polarity from calibration, remove BUF_INVERT

`reversed = (BUF_PSF_MAX_COMP < BUF_PSF_MAX_TENS)`. When `reversed=true`
(PSF default: compression=low raw ≈ 0, tension=high raw ≈ 1):
- fraction below neutral → negative norm → BUF_COMPRESSION ✓
- fraction above neutral → positive norm → BUF_TENSION ✓

When `reversed=false` (compression=high, tension=low): formula branches flip.
User never sets a polarity flag; they just calibrate `max_comp` and `max_tens`
to match physical reality.

*Alternative rejected*: keep `BUF_INVERT` as explicit override. Rejected
because it creates a second source of truth that can conflict with calibration.

### D3 — Goal-relative zone boundaries, drop BUF_THR for type-P

`BUF_THR` was a normalized threshold symmetric around 0 (legacy "neutral =
raw midpoint" assumption). With `BUF_GOAL` biasing the operating point (e.g.,
0.3 raw = slightly toward compression), "neutral" should mean "near goal,"
not "near 0.5 raw."

Zone boundaries for type-P:
- `goal_norm` = `BUF_GOAL` converted to [-1,1] via calibration endpoints
- COMPRESSION: `g_buf_pos < goal_norm - PSF_ZONE_DEADBAND`
- NEUTRAL:     `|g_buf_pos - goal_norm| <= PSF_ZONE_DEADBAND`
- TENSION:     `g_buf_pos > goal_norm + PSF_ZONE_DEADBAND`

`PSF_ZONE_DEADBAND = 0.1` (normalized, hardcoded). Equivalent to ~10% of
full sensor range. Not user-tunable (no evidence it needs to be).

`BUF_THR` is kept only for type-D (used in virtual position tick threshold
comparisons). It is removed from the type-P code path.

### D4 — Asymmetric normalization matching Happy Hare _map_reading

```c
bool reversed = (BUF_PSF_MAX_COMP < BUF_PSF_MAX_TENS);
float n = BUF_PSF_NEUTRAL;
float norm;
if (reversed) {
    float d_comp = n - BUF_PSF_MAX_COMP;  if (d_comp < 0.001f) d_comp = 0.001f;
    float d_tens = BUF_PSF_MAX_TENS - n;  if (d_tens < 0.001f) d_tens = 0.001f;
    norm = (fraction <= n) ? -(n - fraction) / d_comp
                           :  (fraction - n) / d_tens;
} else {
    float d_comp = BUF_PSF_MAX_COMP - n;  if (d_comp < 0.001f) d_comp = 0.001f;
    float d_tens = n - BUF_PSF_MAX_TENS;  if (d_tens < 0.001f) d_tens = 0.001f;
    norm = (fraction >= n) ? -(fraction - n) / d_comp
                           :  (n - fraction) / d_tens;
}
norm = clamp_f(norm, -1.0f, 1.0f);
g_buf_pos = BUF_ANALOG_ALPHA * norm + (1.0f - BUF_ANALOG_ALPHA) * g_buf_pos;
```

FLARE sign convention: negative = COMPRESSION (buffer full), positive =
TENSION (buffer slack). Opposite to HH's convention (HH: +1 = compression)
— FLARE convention kept to preserve `buf_state_raw()` logic.

### D5 — psf_control_law(): continuous PD on error_norm

```c
// target_sps from PD correction + baseline; no override/bangbang
// Uses existing reserve_correction and baseline_control_floor_sps()
// No relay override; ramp up/down handles smoothing
```

Type-D relay bangbang override (L1712-1724) is extracted to
`relay_control_law(s)` and gated strictly behind `BUF_SENSOR_TYPE == 0`.
For type-P, the PD correction (`reserve_correction`) already drives
`target_sps` via `error_norm`; no additional override needed.

### D6 — compression_floor removed for type-P

Block at L1750 (`BUF_SENSOR_TYPE != 0 && BUF_COMPRESSION → force-raise floor`)
assumed COMPRESSION = buffer empty. With consistent polarity (COMPRESSION =
buffer full), this fights drain. Removed entirely for type-P. The PD law
naturally backs off when `error_norm` shows we are below goal (buffer full).

### D7 — compression_recovery: shared, trigger verified on rig

`sync_compression_recovery_active` fires on → BUF_COMPRESSION transition,
caps speed during recovery drain. Concept is correct for type-P (arriving at
COMPRESSION = buffer is now full, recovering from over-tension). Trigger
timing (#7) and estimator drag direction (H2) must be verified on rig before
the tasks that touch those code paths are marked done.

### D9 — Type-P is a measurement, not an estimator: delete the compensation machinery

Type-D reconstructs an unobservable continuous position from binary switch
events. Type-P measures position directly at 50Hz. The following type-D
machinery exists solely to compensate for the blind estimator and SHALL be
gated to `BUF_SENSOR_TYPE == 0`, leaving the type-P path free of it:

- `buf_virtual_position_tick()` (dead-reckoning)
- drift observer / residual EWMA (L816-834)
- sigma / variance / confidence model (L1208-1232)
- variance-aware position blend (L1371-1377)
- confidence bias shift (L1412-1421)
- model-stalled detection (L1462-1505)
- EST_FALLBACK reset (L1213-1222)
- estimator alpha adaptation (L799-804)

For type-P, confidence is 1.0 unless the sensor is saturated (existing L1239
detection). No drift, no sigma, no fallback. This is the single largest
simplification in the change.

*Alternative rejected*: keep the machinery active but feed it the measured
position. Pointless — it would be correcting a measurement against itself,
adding lag and code for no benefit.

### D10 — Continuous extruder estimator (type-P)

Type-D updates `extruder_est_sps` only at switch crossings via
`arm_vel_mm_s = travel_mm / dwell_s` then `extruder_mm_s = mmu_mm_s + arm_vel`.
Type-P computes velocity every tick from the position delta:

```c
float vel_norm = (g_buf_pos - g_buf_pos_prev) / dt_s;   // normalized units/s
float arm_vel  = vel_norm * buf_physical_half_travel_mm();
float extruder_mm_s = mmu_mm_s + arm_vel;               // same formula, live
```

The extruder-estimate formula at L788-789 is reused verbatim; only the velocity
source changes (continuous vs crossing-event). This gives a live feedforward
term unavailable to type-D.

`g_buf_pos_prev` is a new static updated at the end of each `buf_analog_update()`.

### D11 — psf_control_law(): PD + derivative + dead zone

```c
static int psf_control_law(float error_norm, float vel_norm_f, uint32_t now_ms) {
    int ff = (int)extruder_est_sps;                       // continuous feedforward
    int p  = (fabsf(error_norm) < PSF_CTRL_DEADBAND)      // P dead-zoned (jitter)
           ? 0
           : (int)(error_norm * (float)SYNC_KP_SPS_scaled);
    int d  = (int)(vel_norm_f * (float)KD_PSF);           // D always active
    int target = ff + p + d;
    return clamp_i(target, 0, max_sps);                   // ramp slew applied by caller
}
```

P dead zone rejects ADC jitter near goal (small-amplitude, low-velocity). D is
always active so fast moves (high-velocity) are caught even inside the dead
zone — the amplitude/velocity split that separates noise from signal.

*Sign convention*: `error_norm = pos_norm - goal_norm`. Positive (toward
tension/slack) → feed more. Negative (toward compression/full) → feed less.
`vel_norm` positive = moving toward tension. Verify gain signs on rig.

### D12 — Filtered derivative (avoid derivative kick)

D on raw ADC amplifies noise. Two-stage filtering:
1. Position already EWMA'd by `BUF_ANALOG_ALPHA` in `buf_analog_update()`.
2. `vel_norm` computed from the smoothed position, then a light LPF:
   `vel_norm_f = PSF_VEL_ALPHA * vel_norm + (1-PSF_VEL_ALPHA) * vel_norm_f`.

`KD_PSF` tuned against the jitter floor on the rig. `PSF_VEL_ALPHA` default
~0.3 (heavier smoothing than position to tame the derivative).

### D13 — Soft walls (progressive, |norm| ∈ [0.8, 1.0])

As position nears saturation, blend the PD output toward a safety limit rather
than a hard cliff:

```c
float wall = (fabsf(pos_norm) - PSF_SOFT_WALL_START)
           / (1.0f - PSF_SOFT_WALL_START);           // 0 at 0.8, 1 at 1.0
wall = clamp_f(wall, 0.0f, 1.0f);
if (pos_norm > 0) target = lerp(target, max_sps, wall);   // TENSION: urgent refill
else             target = lerp(target, 0,       wall);    // COMPRESSION: stop overfeed
```

`PSF_SOFT_WALL_START = 0.8` (raw 0.9 / 0.1). Reuses the `sync_apply_scaling()`
taper philosophy. The PD law owns the mid-band; the wall owns the extremes;
they blend in between.

### D14 — Hard catch + stop/slowdown disambiguation (Layer 3)

Derivative-triggered reversible brake, then classify by subsequent motion:

```
|vel_norm| > PSF_JUMP_NORM_PER_S toward compression
        → sync_fast_brake (feed→0, reversible, reuse sync_fast_brake_until_ms)
        → within PSF_STOP_CONFIRM_MS:
             vel_norm turns positive (buffer drifts back toward tension)
                 → extruder resumed → resume PD  (it was a slowdown/dwell)
             buffer stays pinned compression (|pos_norm| near -1)
                 → real stop → sync_relief_pause()
```

Saturation-sustained (no jump, slow pin):
- `pos_norm <= -0.99` for `PSF_WALL_SAT_MS` → `sync_relief_pause()` (buffer full)
- `pos_norm >= +0.99` for `PSF_WALL_SAT_MS` → `sync_fault_hold()` (runout risk)

The brake being reversible avoids false-stopping on transients; only sustained
pinning confirms. Faster than the existing `SYNC_AUTO_STOP_MS` dwell because the
brake pre-fires on velocity and the dwell only confirms.

Reuses `sync_fast_brake_until_ms`, `sync_relief_pause()`, `sync_fault_hold()`,
`sync_post_compression_boost` (brief boost on leaving the wall). The type-D
`compression_wall_critical` fault (L1679, gated to `BUF_SRC_VIRTUAL_ENDSTOP`)
is left untouched; type-P gets this new analog-native catch instead.

### D15 — Intentional biasing replaces estimator-freshness caps

Type-D's `buf_target_reserve_mm()` carries H1/H2 bias-cap logic whose purpose is
to park the buffer off the wall so switch crossings stay frequent and the
estimator stays fed. Type-P needs no crossings, so for the type-P path
`buf_target_norm()` returns `BUF_GOAL` converted to normalized space directly —
a pure print-quality bias, no caps. `buf_target_reserve_mm()` and its caps stay
type-D only.

### D16 — Loop-rate bump for type-P

Control tick is `SYNC_TICK_MS = 20` (50Hz). Fast tip-shaping retracts can move
the buffer significantly within one tick. ADC read is cheap, so type-P MAY run
a faster sensor/control tick (target 100-200Hz). Approach: a separate type-P
tick interval (`PSF_TICK_MS`) decoupled from `SYNC_TICK_MS`, or run
`buf_analog_update()` + control at the faster rate while keeping telemetry at
50Hz. Decide implementation after measuring whether 50Hz is actually the
bottleneck on the rig.

### D17 — Tip-shaping retract elimination (stretch, rig-gated)

Hypothesis: continuous fast-move catching (D term + faster loop) lets the MMU
gear follow extruder tip-forming moves live, so the host can drop pre-programmed
retract triggers. Two hard gates decide feasibility:
1. Loop rate (D16) high enough to see the move before the buffer saturates.
2. Buffer mechanical travel + motor accel headroom enough to absorb/chase the
   move within reaction latency.

If either fails, manual retracts stay. Treated as a measured hypothesis with a
rig acceptance test, not a committed deliverable. Does not block the rest.

### D8 — NVM: settings_t gains 4 new float fields, loses 2

Remove: `buf_range` (float), `buf_invert` (bool).
Add: `buf_psf_max_comp` (float), `buf_psf_max_tens` (float),
     `buf_psf_neutral` (float, replaces `buf_analog_neutral`),
     `buf_psf_goal` (float).

Net: +3 floats = +12 bytes. `settings_t` has a 512-byte static_assert guard;
verify it still passes after change. Defaults: max_comp=0.0, max_tens=1.0,
neutral=0.5, goal=0.3.

Control gains that need rig tuning SHALL also be runtime-settable + persisted:
`KD_PSF` (derivative gain) and the P gain scale for type-P. Threshold constants
(`PSF_CTRL_DEADBAND`, `PSF_SOFT_WALL_START`, `PSF_VEL_ALPHA`,
`PSF_JUMP_NORM_PER_S`, `PSF_STOP_CONFIRM_MS`, `PSF_WALL_SAT_MS`, `PSF_TICK_MS`)
start as compile-time constants in `tune.h`; promote to NVM only if rig tuning
shows they need per-unit adjustment. Re-check the 512-byte guard after adding
any NVM gains.

Existing saved settings with `buf_range`/`buf_invert` will be invalidated
by CRC mismatch on first boot → factory reset to defaults. Acceptable;
type-P was not deployed.

### D18 — Auto-sync transition gating and higher tension threshold (Type-P only)

To prevent spurious auto-sync/feeding behavior when booted or homed at tension on Type-P analog sensors, we:
1. Introduce a transition-gated auto-start check via `g_sync_tension_transitioned` for Type-P only.
2. Set `g_sync_tension_transitioned = true` on the transition to `BUF_TENSION` state inside `sync_on_transition()`.
3. Clear `g_sync_tension_transitioned = false` when any synchronization state machine transition occurs (inside `sync_set_state()`).
4. For Type-P, only allow auto-start in `sync_tick()` when `g_sync_tension_transitioned` is true and `g_buf_pos > 0.6f` to avoid minor drift near the deadband zone from triggering auto-start.
5. For Type-D, keep original behavior untouched: auto-start triggers directly on `s == BUF_TENSION` without requiring a transition gate.

### D19 — Buffer Lock (BL) Support and Tighter Lock-Break for Type-P (Analog)

To enable `BL:` buffer-lock commands to work efficiently and pick up faster on Type-P analog configurations, we:
1. Define physical extreme targets in the `BL_PRIME` phase for Type-P. Reached is defined as `g_buf_pos >= 0.90f` for `BUF_TENSION` and `g_buf_pos <= -0.90f` for `BUF_COMPRESSION`. This ensures the prime phase drives the arm all the way to the endstop rather than stopping immediately at the zone neutral deadband.
2. Implement tight lock-break detection in `BL_LOCKED` for Type-P. The lock is broken as soon as `g_buf_pos < 0.90f` for tension or `g_buf_pos > -0.90f` for compression. This allows instant follow-on triggering the millisecond the extruder starts pulling, yielding a much faster pick-up reaction than the digital microswitch.

### D20 — Closed-Loop Buffer-Lock Follow for Type-P (Analog)

To keep the buffer perfectly neutral during fast print head retracts (where open-loop follow-on speed would cause tension or compression drift), we implement a closed-loop dynamic analog follower inside `BL_FOLLOW` for Type-P:
1. Calculate position error relative to goal: `err = g_buf_pos - psf_goal_norm()`.
2. Compute dynamic factor: `factor = -err / (1.0f + psf_goal_norm())` for tension, and `factor = err / (1.0f - psf_goal_norm())` for compression.
3. Dynamically set motor sps as a fraction of user-requested `max_follow_sps` proportional to the error: `follow_sps = (int)(factor * max_follow_sps)`.
4. This ensures that as the buffer compresses, the MMU retracts faster, automatically slowing down to 0 sps as the buffer returns to neutral.
5. Integrate actual distance traveled dynamically (`g_bl_follow_traveled_mm += follow_sps * MM_PER_STEP * dt_s`) to stop precisely when `g_bl_follow_mm` is consumed.

### D21 — Highly Sensitive Contact Detection in RELOAD Approach for Type-P (Analog)

To achieve instantaneous contact/touch tracking when the new filament tip hits the tail of the old filament during the `TC_RELOAD_APPROACH` phase:
1. On Type-D digital buffer, contact is blocked until the microswitch closes at `BUF_COMPRESSION`.
2. On Type-P analog buffer, the arm sits at the tension/home position (`g_buf_pos == 1.0f`) during free feed. Any physical contact with the tail will instantly push the arm away from the home stop.
3. We define contact when `g_buf_pos < 0.85f` for Type-P. This detects contact the moment the tip touches the tail, avoiding filament bowing or pressure build-ups.

## Risks / Trade-offs

- **#7 compression_recovery timing unverified** → Tasks for #7 and H2 are
  rig-gated; marked with BLOCKER note. Do not close `pending-analog-rig`
  until rig-validated.
- **settings_t CRC invalidation on upgrade** → Expected and acceptable;
  type-P params were not in use. Document in release notes.
- **PSF_ZONE_DEADBAND hardcoded** → If 0.1 proves wrong for some sensor
  mounting, it needs a tunable. Start hardcoded; make tunable only if needed.
- **buf_threshold_mm() for type-D normalization** → `buf_pos_norm()` divides
  by `buf_threshold_mm()` = `BUF_SWITCH_SPAN_HALF_MM`. If this is 0 (bad
  config), division by zero. Guard with `> 0.001f` clamp same as existing
  pattern.

## Open Questions

- **#7**: Does `compression_recovery` trigger on TENSION→COMPRESSION
  correctly for type-P, or does it fire spuriously? Rig-verify. (May be moot —
  the Layer 3 stop-catch supersedes much of the recovery logic for type-P.)
- **H2**: Is the estimator drag-down at L1498-1504 helpful or harmful for
  type-P? Rig-verify direction and adjust comment/code. (Likely deleted for
  type-P under D9 — the continuous estimator removes the need.)
- **PSF_ZONE_DEADBAND = 0.1**: Is this the right default? Revisit after
  first rig session.
- **Gain signs (D11)**: Confirm `Kp`/`Kd` signs on rig — depends on physical
  buffer/gear orientation.
- **PSF_CTRL_DEADBAND vs jitter floor (D11/D12)**: Measure actual ADC jitter
  on rig; size dead zone and `KD_PSF` against it.
- **Loop rate (D16)**: Is 50Hz actually the bottleneck for fast-move catching,
  or is motor accel the limit? Measure before bumping `PSF_TICK_MS`.
- **Tip-shaping (D17)**: Does continuous catching meet the acceptance test for
  dropping host retract triggers? Stretch — rig-gated.
- **compression_recovery / soft-wall overlap**: With Layer 2 soft walls + Layer
  3 catch, does the shared `compression_recovery` cap still add value for
  type-P, or is it redundant? Decide on rig.
