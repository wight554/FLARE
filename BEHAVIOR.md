# FLARE – Behavioral Reference

This document describes *what the firmware does and why* — state transitions,
failure modes, interlocks, and recovery paths. For the command syntax see
`MANUAL.md`; for hardware pin assignments see `HARDWARE.md`.

---

## Firmware Scope

FLARE is the firmware engine for lane automation and reload behavior. It owns
the controller-side decisions: lane motion, preload/unload/load tasks,
toolchange phases, cutter sequencing, buffer sync, fault handling, and RELOAD
handoff. It does not name a complete hardware assembly; hardware references in
this repo describe known tested builds and wiring assumptions.

---

## Filament states

Each lane tracks filament position inferred from its two sensors.

| IN | OUT | Meaning |
|----|-----|---------|
| 0  | 0   | Absent — filament clear of both sensors (or in transit window) |
| 1  | 0   | Pre-loaded — filament parked between IN and OUT (drive gear engaged) |
| 1  | 1   | Loaded — filament past OUT, in bowden or extruder |
| 0  | 1   | Tail between sensors — tip just cleared IN, body still at OUT |
| 0  | 0*  | In-transit — tail cleared both but within 1.2x DIST_IN_OUT of IN-clear point |

Pre-loaded is the normal parked state after `LO:` or autopreload completes.

---

## Boot sequence

1. Hardware init (GPIOs, PWM, TMC2209 UART).
2. `settings_load()` — restores runtime parameters from flash.
3. **Sensor settling** — `din_update()` is spun for 25 ms so the 10 ms
   debounce threshold can commit correct stable values.
4. **Active lane detection** — two-pass:
   - First pass: if exactly one lane has OUT triggered, that lane is active.
   - Fallback: if no OUT is triggered, check IN sensors — first lane with
     IN=1 and OUT=0 is selected (pre-loaded state).
   - If neither pass finds a lane, `active_lane` stays 0 (unknown).
5. `prev_*_in_present` is initialised from current sensor state so that
   autopreload does **not** re-trigger for filament already present at boot.
6. **Background buffer stabilization** — in dual-endstop mode, if the buffer
  starts in `TENSION` or `COMPRESSION`, firmware nudges it toward `NEUTRAL` at
  `BUF_STAB_RATE` in the normal main loop. This no longer blocks USB command
  handling or the rest of the control loop during boot.

---

## Autopreload

Fires automatically when an IN sensor rises (filament freshly inserted).

**Conditions to start:**
- Lane is IDLE, no toolchange in progress, cutter not busy.
- That lane's OUT sensor is currently clear (not pre-loaded already).
- `AUTO_PRELOAD` runtime toggle is on (default: 1).

**What it does:**
- Starts `TASK_AUTOLOAD` at `AUTO_RATE` — drives filament forward until OUT
  triggers.
- On OUT trigger: reverses by `RETRACT_MM` (default 10 mm) and stops, leaving
  the tip just before OUT (pre-loaded state).
- Sets `active_lane` to this lane if the other lane's OUT is clear.

---

## Load commands

### `LO:` — Lane autoload (to pre-loaded state)

Runs `TASK_AUTOLOAD` at `AUTO_RATE` until OUT triggers, then retracts by
`RETRACT_MM`. Parks filament just before OUT.

### `FL:` — Full load to toolhead

Runs `TASK_LOAD_FULL` at `FEED_RATE` continuously until `TS:1`, the
`TS_BUF_MS` sustained-COMPRESSION fallback, or distance-checked buffer geometry
reports the lane loaded. OUT sensor is a non-stopping checkpoint.

**Interlocks checked before starting:**
- Active lane must be set — `ER:NO_ACTIVE_LANE`.
- IN sensor must be present — `ER:NO_FILAMENT`.
- Other lane must not be idle at OUT (filament blocking the path) —
  `ER:OTHER_LANE_ACTIVE`.

**Failure detection during `FL:`:**

| Condition | Timeout | Event |
|-----------|---------|-------|
| IN goes low >1 s after start | 1.2x DIST_IN_OUT | `EV:RUNOUT:<lane>` (waits for transit) |
| OUT never seen after 10 s | 10 s | `EV:RUNOUT:<lane>` |
| Buffer holds COMPRESSION after OUT for `TS_BUF_MS` | `TS_BUF_MS` | `EV:LOADED:<lane>` (fallback) |
| Buffer reaches sane TENSION/COMPRESSION geometry after OUT | distance-checked | `EV:LOADED:<lane>` |
| Load task exceeds travel limit | `LOAD_MAX` distance | `EV:LOAD_TIMEOUT:<lane>` |

---

## Unload commands

### `UL:` — Unload from extruder

Runs reverse at `REV_RATE` until OUT clears. If `CUTTER=1` and `UNLOAD_CUT=1`,
`UL:` first unloads past OUT, runs the same fed cutter sequence used by load
cutting, then unloads past OUT again.
**Requires OUT to be triggered before starting** — returns `ER:NOT_LOADED` if
OUT is already clear.

If the printer blocks retraction and the buffer stays in `BUF_TENSION`, `UL:`
stops with `EV:UNLOAD_BLOCKED` after `UNLOAD_TENSION_BLOCK_MS`.

### `UM[:lane]` — Unload from MMU

`UM` and `UM:` run on the active lane. They reverse at `REV_RATE` until IN
clears. If OUT is present at entry and `CUTTER=1` / `UNLOAD_CUT=1`, the command
first runs the full `UL:` sequence (clear OUT, cut, clear OUT again), then
continues reverse until IN clears. If cutting is disabled, it runs the non-cut
OUT clear leg before continuing to IN clear. If OUT is already clear at entry,
it does not run a cutter phase; if the Y-splitter is still present, it performs
the non-cut clear/retract leg before continuing to IN clear.

`UM:1` and `UM:2` target an explicit lane. If the target is the active lane,
behavior matches `UM:`. If the target is inactive, it is treated as a standby
preload eject: the lane must be idle with `IN=1` and `OUT=0`, and the command
retracts only until that lane's IN clears. Inactive eject does not change the
active lane, clear toolhead filament state, disable active-lane sync, inspect
the shared Y-splitter, or run the cutter.

---

## Toolchange — `TC:<lane>`

Full automated cycle. Emits phase events at each step.

```
TC_IDLE
  → TC_UNLOAD_WAIT_TH   (wait for TS:0 from host or timeout before lane unload, if TC_TH_MS > 0 and TH:1)
  → TC_UNLOAD_REVERSE   (start TASK_UNLOAD on current lane)
  → TC_UNLOAD_WAIT_OUT  (wait for OUT to clear; lane task is bounded by `UNLOAD_MAX`)
  → TC_UNLOAD_CUT       (if CUTTER=1 and UNLOAD_CUT=1: run cutter sequence)
  → TC_UNLOAD_WAIT_CUT
  → TC_UNLOAD_REVERSE   (post-cut clear)
  → TC_UNLOAD_WAIT_OUT
  → TC_UNLOAD_WAIT_Y    (wait for Y-splitter to clear, if TC_Y_MS > 0)
  → TC_UNLOAD_DONE
  → TC_SWAP             (set active_lane = target)
  → TC_LOAD_START       (clear toolhead state for new lane; check Y-splitter clear; start TASK_LOAD_FULL)
  → TC_LOAD_WAIT_OUT    (non-stopping checkpoint)
  → TC_LOAD_WAIT_TH     (wait for TASK_LOAD_FULL loaded result; lane task is bounded by `LOAD_MAX`)
  → TC_LOAD_DONE        → EV:TC:DONE:<lane>
```

`TC_UNLOAD_WAIT_CUT` waits for the cutter state machine to finish. Cutter feed
and servo phases use their own `CUT_FEED_MS` / `CUT_SETTLE_MS` guards; the
outer `TC_CUT_MS` watchdog is extended automatically when the configured cutter
feed distance, repeat count, and servo settle time require longer than the
stored value.

---

## Buffer Lock — `BL:<T|C>`

`BL:T` (tension) or `BL:C` (compression) arms a closed-loop buffer-lock
sequence that replaces the legacy blind `RA`-gated retract. The lifecycle has
four sub-states inside `SYNC_RETRACT_ASSIST`:

1. **PRIME** — the active-lane motor drives toward the requested extreme. Type-D
   (switch) primes at `SYNC_MAX_SPS` — the switch click is bang-bang, so full
   speed stops instantly at the extreme. Type-P (analog) primes at `BUF_STAB_SPS`:
   the PSF reading is EMA-filtered and lags, so a full-speed prime would overshoot
   `PSF_HOME_THRESHOLD_NORM` (0.90) and slam the `±1.0` rail before the motor reads
   the threshold and stops. The prime is bounded to `BUF_MAX_TRAVEL_MM / 2` of
   travel. When the target extreme is reached or the deadline elapses the state
   advances to LOCKED and emits `EV:BL,PRIME_BOUND` on deadline or `EV:BL,LOCKED`
   on success.
2. **LOCKED** — motor stays energized at zero feed, holding the buffer at the
   extreme. Any external force (printer-side retract) that breaks away from the
   target extreme (raw flip for type-D, `g_buf_pos` crossing `PSF_HOME_THRESHOLD_NORM`
   for type-P) fires the follow-on if one was armed (`BL:<T|C>:<follow_mm>:<rate>`).
3. **FOLLOW** — open-loop concurrent retract in the prime direction, feeding
   `follow_mm` at `follow_rate` to mass-balance the extruder move, then returning
   to LOCKED (`EV:BL,FOLLOW_DONE`). For type-P the open-loop feed is **position-gated**:
   if `g_buf_pos` reaches `PSF_FOLLOW_RAIL_NORM` (0.95) the feed stops early and
   drops back to LOCKED (`EV:BL,FOLLOW_GATED`) so it never slams the armed rail; if
   backflow later pushes the buffer off the extreme the lock re-breaks and the
   follow re-fires. Type-D has no analog position and relies on the elapsed-distance
   budget alone. A watchdog caps total arm time (`BL_WATCHDOG_DEFAULT_MS`, default
   30 s); timeout emits `EV:BL,WATCHDOG` and releases.
4. **Release** — `BS` sent by the host (or watchdog expiry) calls
   `sync_retract_assist_release` which drops back to `SYNC_ACTIVE` and
   re-enables normal sync.

The `RA:<0|1>` command is removed. `GET:BL` returns the current arm state
(`BL:T`, `BL:C`, or `BL:0`). The status field previously named `BL` (baseline
flow) is renamed to `BF` in the serial protocol and all host tooling.

---

## Motor acceleration ramp

All lane tasks start at `RAMP_STEP_RATE` (default 17 mm/min) and increment by
`RAMP_STEP_RATE` every `RAMP_TICK_MS` (default 5 ms) until the target rate is
reached.

### Buffer sync speed control

The sync controller runs every `SYNC_TICK_MS` (20 ms). In dual-endstop mode it
tracks a virtual buffer position in millimeters instead of treating `NEUTRAL` as
the steady-state target. The controller still uses the extruder-rate estimator,
but it now drives toward a buffered-reserve target on the compression side.
The baseline and compression-bias inputs come from `flow_param(extruder_est_sps)`.
With no schedule table this is the exact scalar `BASELINE_RATE` /
`COMPRESSION_BIAS_FRAC` fallback, and with `[flow_schedule.v1]` present it is a
clamped linear interpolation across flow breakpoints. The effective reserve
bias is floored by `SYNC_COMPRESSION_BIAS_FRAC`, and the baseline control floor is
floored by `BASELINE_RATE`, so schedule interpolation and live learning may
only strengthen the reserve/baseline safety floor, not weaken it.

```
target = extruder_est_sps
  + reserve_correction_sps
  + zone_bias_sps
  + slope_bias_sps
  - overshoot_trim_sps
  + PRE_RAMP_RATE  (if predict_tension_coming)

target = sync_apply_scaling(...)
target = clamp(target, SYNC_MIN_RATE, SYNC_MAX_RATE)
```

#### Type-P output smoothing (distance-based)

For type-P (`BUF_SENSOR_TYPE == 1`) the PD/feedforward target above is **not**
applied to the motor directly. The extruder rate is *estimated* from the buffer
arm (`extruder_mm_s = mmu_mm_s + arm_vel`), so the raw target is noisy and would
snap the feed every tick if applied as-is. Two distance-based stages smooth it,
both keyed to filament distance moved this tick (`move = |extruder_est| · dt`),
not wall-clock — mirroring Happy-Hare's `rd_filter_len_mm` / `rd_rate_per_mm`:

1. **Target EMA over distance** — `alpha = 1 - exp(-move / SYNC_PSF_FILTER_MM)`;
   larger `SYNC_PSF_FILTER_MM` (default 25 mm) = slower, smoother target.
2. **Slew limit over distance** — the applied `sync_current_sps` moves toward the
   filtered target by at most `SYNC_PSF_SLEW_PER_MM · move` per tick (default
   1500 sps/mm), **clamped so it never overshoots** the target (overshoot is what
   made the old fixed time-ramp 2-tick bang-bang).

Because both stages scale with flow, feed changes are gentle at low flow and go
to ~0 when the printer is idle. `fast_brake` still forces an instant stop (and
resets the filter). The old time-ramp (`SYNC_RAMP_UP/DN_SPS`) and the brief
direct-apply both applied only to non-type-P or were replaced by this path. Both
knobs are live-tunable (`SET:SYNC_PSF_SLEW_PER_MM` / `SET:SYNC_PSF_FILTER_MM`),
runtime-only (not persisted; re-seeded from defaults each boot).

#### Type-D Standalone Relay Control Law

For Sync-Feedback Sensor type D (`BUF_SENSOR_TYPE == 0`), FLARE overrides the continuous PI/EKF estimator-driven target with a two-level hysteretic relay control law matched directly to the physical microswitches:

- **`BUF_TENSION` (empty/starved)**: Commands a strong fixed catch-up rate based on the configured baseline rate (`baseline_control_floor_sps() * RELAY_CATCHUP_FRAC`) to ensure rapid buffer refill, completely independent of the velocity estimator.
- **`BUF_COMPRESSION` (full reserve)**: Commands a **true zero feed** (0 SPS) instead of `SYNC_MIN_SPS`, so feed stops rather than pushing filament forward into a full buffer (feeding `SYNC_MIN` forward deepened the buffer past the switch for ~5 s at end of feed). The extruder's draw pulls the buffer back off the compression wall; recovery uses the existing relieve / `SYNC_AUTO_STOP_MS` path.
- **`BUF_NEUTRAL` (neutral zone)**: Dynamically tracks estimated extruder demand (`extruder_est_sps * RELAY_NEUTRAL_FRAC`), clamped to the range `[SYNC_MIN_SPS, baseline_control_floor_sps()]`. This prevents the buffer from slamming either wall during steady consumption.

Zero feed in `BUF_COMPRESSION` does not deadlock the relay: the flip out keys on the physical `NEUTRAL` crossing (extruder draw), and `relay_min_flip_mm` defaults to `0` (time-based hysteresis).

#### Velocity estimator

Whenever the buffer changes zone, firmware measures the dwell time in the old
zone and converts the switch-threshold travel into an estimated arm velocity.
Combined with the MMU speed averaged during that dwell, this yields an
instantaneous extruder-rate estimate.

- `BUF_SWITCH_SPAN / 2` is the switch distance from `NEUTRAL`.
- `BUF_MAX_TRAVEL / 2` is the physical half-travel used to clamp the virtual
  position beyond the switch.
- `NEUTRAL→TENSION`, `TENSION→NEUTRAL`, `NEUTRAL→COMPRESSION`, `COMPRESSION→NEUTRAL` use the switch
  threshold distance.
- `TENSION→COMPRESSION` and `COMPRESSION→TENSION` use twice the switch threshold.
- Half the hysteresis window is subtracted from dwell time before computing arm
  velocity so the estimate is not biased late.
- The instantaneous estimate is clamped to `GLOBAL_MAX_RATE` and merged into
  `extruder_est_sps` with an adaptive EMA bounded by `EST_ALPHA_MIN` and
  `EST_ALPHA_MAX`.
- On type-P sensors a fast `TENSION→COMPRESSION` transition overwrites the
  estimator directly so a sudden demand collapse is reflected immediately. On
  type-D (dual-endstop) this travel is *modeled* (`2×threshold ÷ dwell`), so a
  short/partial transition could fabricate a huge value; there the update is
  blended through the adaptive EMA instead of overwritten, so a single modeled
  transition cannot spike the estimator (and over-feed the next `NEUTRAL`).

If the buffer stays in NEUTRAL for > 2 s, the estimator decays gently toward the
current MMU speed. This keeps the feed-forward term sane during long steady
sections where no new transitions arrive.

The `EA:` field in the `?:` status response exposes the estimator age in
milliseconds since the last meaningful update. `ES:` and `EC:` expose the current
estimator sigma (uncertainty in mm) and confidence percentage. A large `EA:`
value while the arm is in `BUF_NEUTRAL` is normal; a large `EA:` while in `BUF_TENSION`
may indicate the bleed path is the only update source.

FLARE includes a residual drift observer. At every `NEUTRAL → TENSION` zone
transition the virtual position (`g_buf_pos`) is measured
against the known switch threshold *before* the position is snapped to that
threshold. The difference `BPR = g_buf_pos − switch_pos_mm` is the literal
mismatch between the virtual model and the physical arm at the one moment
per cycle where the physical position is exactly known. These per-crossing
residuals are accumulated into a slow EWMA (`BPD`, time constant
`BUF_DRIFT_TAU_MS`). A stable non-zero `BPD` value indicates systematic
virtual-position bias — the estimator is underestimating the net arm travel
per cycle.

By default, `BUF_DRIFT_THR_MM=2.0` enables correction only after meaningful
observed drift; setting it to `0.0` disables correction. When
`BUF_DRIFT_THR_MM > 0` and at least one sample has accumulated, the
controller substitutes `bp_eff = g_buf_pos − scaled_clamp(BPD, ±BUF_DRIFT_CLAMP)`
in place of raw `g_buf_pos` for all control-law decisions in `sync_tick()`
(reserve error, near-target check, taper scaling). The correction ramps in
linearly until `BUF_DRIFT_MIN_SMP` samples have accumulated; for example, with
`BUF_DRIFT_MIN_SMP=3`, the first sample applies one third of the clamped
correction and the third sample applies full correction. The integration loop
and re-anchoring always use the raw position; only the controller's *reaction*
to the current position is corrected. Correction is also sign-aware tapered
near the opposite physical endstop: negative drift correction fades out as the
raw position approaches `COMPRESSION`, and positive drift correction fades out as
the raw position approaches `TENSION`. This keeps drift compensation from
masking a real wall contact. `RDC:` (0–100) shows the final correction activity
after sample ramp, clamp, confidence gating, and wall taper. The observer state
resets on sync stop, `EST_FALLBACK`, and sensor hot-swap, emitting
`EV:BUF,DRIFT_RESET`.

#### Zone bias and recovery behavior

In dual-endstop mode, firmware anchors the virtual position to the switch edge
on each transition, then integrates the mismatch between estimated extruder
draw and commanded MMU feed inside the physical travel envelope.

The normal sync target is not `NEUTRAL`. It is a buffered-reserve target on the
compression side set by `SYNC_RESERVE_PCT`, expressed as a percentage of
half of `BUF_SWITCH_SPAN`. The effective flow-schedule bias is
`max(SYNC_COMPRESSION_BIAS_FRAC, schedule_bias)`, so a schedule can deepen reserve
but cannot reduce it below the scalar safety cushion. Firmware also keeps a
small built-in center guard on top of that percentage target so steady sync
stays slightly farther away from the tension-side switch. This keeps reserve in
the buffer without hard-coding a deep hidden-margin target into firmware.

`ZONE_BIAS_BASE` and `ZONE_BIAS_RAMP` provide a bounded reserve-recovery pull:

- If the virtual position is more depleted than the target, sync adds positive
  correction to refill the buffer.
- If the virtual position is fuller than the target, sync removes speed and can
  apply extra compression-side trim.
- The total bias is capped by `ZONE_BIAS_MAX`.
- `SYNC_OVERSHOOT_PCT` adds extra braking after reserve overshoots into the
  full/compression side, and `SYNC_OVERSHOOT_NEUTRAL_EXT` extends that trim into
  `BUF_NEUTRAL` while reserve is below the deadband.

This bias keeps the arm near the desired reserve target when the estimator is
slightly wrong, while the estimator remains the dominant term.

When normal sync is active and the arm is still physically in `BUF_NEUTRAL`,
firmware also applies a NEUTRAL-only anti-tension floor if the estimator looks
stale, low-confidence, or has collapsed below the learned baseline-derived
floor while reserve is already near or deeper than the compression-side target.
This prevents long NEUTRAL dwell from decaying command speed into the next
tension hit. The floor is not active in `BUF_COMPRESSION`, so compression braking,
collapse recovery, fast brake, and fault-hold behavior keep full authority.

FLARE supports **neutral-zone creep** for active wall-seek. If the arm dwells in the `NEUTRAL` zone longer than `NEUTRAL_CREEP_TIMEOUT_MS`, a synthetic push velocity is gradually added (`NEUTRAL_CREEP_RATE`) to gently force the arm back to the compression wall to restore confidence. This creep is capped by `NEUTRAL_CREEP_CAP` (% of the measured extruder rate) and resets immediately if the arm reaches an endstop.

The `RT:` and `RD:` fields in `?:` status expose the current reserve target
and deadband in mm, so tuning of `SYNC_RESERVE_PCT`, `BUF_SWITCH_SPAN`, and
`SYNC_KP_RATE` can be observed in real time. `TT:` and `CT:` expose how long
the arm has been continuously pinned at the tension or compression endstop. `CW:`
shows estimated time-to-compression-wall in ms (99999 when not applicable).

A low-gain integral centering term (`RI:`) can correct for slow
rate mismatches that could otherwise settle the arm near the tension side over
long runs. This term is active only in `BUF_NEUTRAL` when estimator confidence is
high. It is capped by `SYNC_INT_CLAMP` and frozen during pin events, toolchanges,
or low-confidence dwells. `RC:` shows the active gain percentage (0% = disabled
or frozen). If the integral saturates toward the tension side,
`EV:SYNC,TENSION_DWELL_WARN` is emitted as an upstream warning before an tension
pin occurs.

A transition-residual drift correction layer (`RDC:`) can also be enabled. When
enabled (`BUF_DRIFT_THR_MM > 0`), the effective position seen by the control
law is shifted by the signed EWMA of pre-snap residuals, ramping from the first
sample to full strength at `BUF_DRIFT_MIN_SMP`. The correction fades near the
opposite endstop, so a learned bias can help through the neutral-zone without
hiding a physical wall. When enabled and the integral is also active, the
integral operates on the corrected position so both terms do not double-correct
for the same bias. `TPX:` counts recent
tension-pin events; `EV:SYNC,TENSION_RISK_HIGH` fires when the density exceeds
`TENSION_RISK_THR` in the `TENSION_RISK_WINDOW` rolling window.

After a deep negative reserve excursion, firmware also latches a
positive-relaunch damp state. During that state, positive reserve correction
and positive zone bias stay reduced until two conditions are both true:

- the minimum relaunch hold time has elapsed
- reserve error has actually unwound back near the reserve target

This avoids a late refill re-acceleration when real print slowdowns or
retractions produce a long recovery that would otherwise outlive the old
fixed-time damp window.

The damp predicate is now stateless — it is derived purely from the hold
timer and current conditions, with no write-back on read. This makes it
safe to call from status dumps and other observers without affecting control
state.

#### Advance-dwell guard

If the buffer arm is continuously pinned at the tension endstop for longer
than `SYNC_TENSION_RAMP_MS`, the sync controller bypasses the estimator ceiling and
forces the target speed toward `SYNC_MAX_RATE`. The default is `0`, so this
estimator-bypass refill ramp is disabled; normal reserve control and the hard
tension stop remain active. Operators can re-enable the ramp as a runtime
escape hatch if hardware evidence supports it.

If the arm remains pinned for longer than `SYNC_TENSION_STOP_MS` (default 6000
ms), sync enters a non-destructive fault hold with `EV:SYNC,FAULT_HOLD`. This
is the safety net for genuine extruder-overload conditions where no amount of
speed increase will refill the buffer. Sync automatically recovers after
`CONF_SYNC_FAULT_HOLD_RECOVERY_MS` (default 5000 ms), emitting
`EV:SYNC,FAULT_HOLD_RECOVERY` and attempting to re-arm. `SYNC_TENSION_STOP_MS: 0`
disables the hard stop.

The `TT:` status field exposes the current tension-dwell timer in real time
for tuning and regression monitoring.

#### Scaling, brake, and baseline adaptation

`sync_apply_scaling()` is a limiter on top of the estimator target:

- In analog-buffer mode, `g_buf_pos` scales the target between
  `COMPRESSION_RATE` and the requested target.
- In dual-endstop mode, the virtual reserve target shapes the controller.
  If the estimated position moves past the target into “too full”, sync tapers
  the requested target down toward `COMPRESSION_RATE` across the remaining
  full-side virtual travel instead of dropping there in one step.
- The controller also computes a dynamic compression-wall time from remaining
  physical margin and current relative push. If time-to-wall collapses while
  sync is still driving toward `COMPRESSION`, firmware adds urgency trim and can
  immediately auto-stop AUTO sync instead of waiting for a long static compression
  dwell once the condition becomes critically unsafe.

On a direct `TENSION→COMPRESSION` transition, firmware arms a short fast-brake
window. During that window the sync target is forced to 0 before normal
COMPRESSION low-speed recovery resumes. Type-D `NEUTRAL→COMPRESSION` remains on
the normal ramp-down path; a trial instant-brake extension on that transition
regressed steady-print behavior by over-pausing the relay cycle.

The live baseline learner remains ephemeral and up-only. Once the settle,
variance, cooldown, distance, and `SYNC_ACTIVE` gates accept an update, the
accepted lift is stored only on the currently active flow segment. Reboot,
`LD:`, `RS:`, or scalar `SET:BASELINE_*` / `SET:COMPRESSION_BIAS_FRAC` refreshes
discard that segment delta and restore the generated schedule or scalar
one-point fallback.

When the buffer returns to NEUTRAL after a non-NEUTRAL dwell and settles there for
> 500 ms, the runtime control baseline drifts toward the current speed. The
configured `BASELINE_RATE` remains a separate bootstrap target and persistence
value; the learned runtime baseline cannot pull control below that configured
floor. AUTO start seeds sync from that floor and no longer overwrites the
configured baseline with `BUF_STAB_RATE`.

#### Buffer signal abstraction

Each `buf_sensor_tick()` cycle produces a `buf_signal_t` snapshot in `g_buf_signal`. It carries normalized position (`pos_norm` in −1..+1), physical position in mm (`pos_mm`), a confidence score (0.0..1.0), the current zone, the source kind (`BUF_SRC_VIRTUAL_ENDSTOP` or `BUF_SRC_ANALOG`), and a fault flag.

- **Virtual-endstop sources**: Confidence is derived from a physics-based sigma model. Uncertainty (`ES`) grows as the square root of integrated motion steps. Re-anchoring at a switch threshold resets uncertainty to a baseline (0.05 mm). Confidence (`EC`) decays as uncertainty approaches `EST_SIGMA_CAP`.
- **Analog sources**: Confidence drops to 0.5 if the signal saturates at a rail for more than 250 ms; it returns to 1.0 once the signal moves off the rail.

The `SK:` field in `?:` status exposes the source kind. `CF:` exposes the current source confidence score, while `EC:` and `ES:` provide the internal estimator certainty.

The sensor kind may not be changed while sync is active, a toolchange is in progress, or either lane is running. The `SET:BUF_SENSOR:n` command returns `ER:BUSY` if any of these conditions are true at the time of the call (D4).

### RELOAD contact and follow

After the old lane tail clears `OUT` and the Y path is clear, firmware waits
`RELOAD_JOIN_MS` before `RELOAD:JOINING` starts. This RELOAD-only grace period
lets the printer pull the old tail clear of unsupported buffer geometry before
the new lane begins its join approach.

**`TC_RELOAD_APPROACH` — buffer-driven contact detection**

The motor runs at `JOIN_RATE` while the controller waits for the buffer to move
into `BUF_COMPRESSION`, which is treated as the first reliable sign that the new
lane has made contact and started pushing filament toward the extruder.

If contact never arrives, the approach phase still has hard escape paths: the
lane task has its configured travel limit and the RELOAD state machine has its
own timeout/abort logic, so RELOAD cannot run forever on a bad path or failed
sensor.

**`TC_RELOAD_FOLLOW` — pressure maintenance during bowden journey**

RELOAD follow no longer derives speed from driver-load telemetry.
It benefits from the same estimator and virtual-position updates, but its speed
policy stays deliberately compression-centric and does not inherit the normal-sync
reserve target:

```
target = extruder_est_sps × RELOAD_LEAN
```

- Target is clamped between `COMPRESSION_RATE` and `JOIN_RATE`.
- `RELOAD_LEAN` now defaults to `1.15` (over-feeds by 15%).
- While in `BUF_NEUTRAL`, `TC_RELOAD_FOLLOW` intentionally **over-feeds** to ensure the new tip pushes faster than the extruder pulls, actively closing the gap to the old tail.
- This causes the arm to gradually drift toward `BUF_COMPRESSION`.
- If it hits `BUF_COMPRESSION`, it drops to `COMPRESSION_RATE` (usually 0), allowing the extruder to pull it back to `NEUTRAL`, creating a solid bang-bang pressure cycle.
- RELOAD follow also watches geometry-aware compression-wall time. If the lane is
  still pushing deeper into the compression wall and the predicted remaining time
  collapses, `FOLLOW_JAM` is raised early instead of waiting only on the static
  `FOLLOW_TIMEOUT_MS` dwell.

Follow protection is now sensor- and timeout-driven: if the lane task faults or
the state exceeds `FOLLOW_TIMEOUT_MS`, RELOAD aborts instead of trying to infer
jam severity from driver load telemetry.

### Trailing behavior and auto-stop

`BUF_COMPRESSION` is now a valid low-speed recovery state, not an immediate hard
stop. In normal print sync, entering `BUF_COMPRESSION` latches a recovery phase:
sync caps speed below the estimator until the buffer returns to `NEUTRAL`, then
applies a brief re-acceleration bump. If compression recovery still persists, the
controller tightens that cap and ramps down more aggressively until sync hits
its compression floor. The hard compression-wall guard still remains the true stop
path if recovery cannot pull the buffer back safely.

`SYNC_AUTO_STOP_MS` is no longer a generic normal-sync compression dwell timeout.
Instead:

- tail-assist auto-starts still stop if `BUF_COMPRESSION` persists for
  `SYNC_AUTO_STOP_MS`;
- normal auto-started print sync requires **continuous `COMPRESSION` dwell** exceeding 
  `SYNC_AUTO_STOP_MS` **and** that the recovery speed has collapsed to the minimum 
  compression-floor speed (ignoring micro-fluctuations). The configured `SYNC_AUTO_STOP_MS` 
  applies directly without relying on an internal deadman multiplier since the 
  dwell timer no longer falsely resets.

When this fires the controller enters `SYNC_RELIEF_PAUSE` (feed 0), not a full
disable. On type-D it re-arms to `SYNC_ACTIVE` as soon as the buffer recovers to
**`NEUTRAL` or `TENSION`**, reseeding `g_buf_pos` to the reserve target and the
speed to the bootstrap floor, so a high-flow resume after a pause does not have to
drain the whole buffer to empty before sync feeds again. (Earlier behavior
re-armed only on `TENSION`, which starved the extruder during the drain.)
Boot/idle stabilization is excluded from this re-arm, so an end-of-print relieve
to `NEUTRAL` does not spuriously restart feed.

The same low-speed stabilization helper used at boot can also be run on demand
with `BS:` when the controller is idle.

In idle loaded states, firmware also runs a negative-sync / retract-sync flow:
if the raw buffer state is `COMPRESSION`, it can wait `POST_PRINT_STAB_MS`
(legacy name, now used as the idle compression delay), then reverse slowly until
the raw buffer reaches `NEUTRAL`. If the move somehow overshoots before the
control loop catches that center crossing, firmware falls back to the
tension-side handoff and then settles the buffer back toward `NEUTRAL`.

**AUTO sync sequence:**

1. `BUF_TENSION` auto-starts sync in `AUTO_MODE` and seeds the estimator from
   the current baseline.
2. If the active lane is in the `IN=0`, `OUT=1` tail-between-sensors state,
  that same auto-start acts as a temporary tail-clear assist so the printer's
  pull can drag the remaining filament past `OUT`.
3. Once `OUT` clears in that assist path, firmware disables sync immediately
  and then continues with the normal `RUNOUT` / optional RELOAD handling.
4. Normal sync runs from the estimator, bounded by buffer state.
5. During normal print sync, `BUF_COMPRESSION` enters a bounded recovery phase
  until the buffer returns to `NEUTRAL`; if that recovery persists, sync ramps down
  aggressively toward the compression floor.
6. During tail assist, sustained `BUF_COMPRESSION` for `SYNC_AUTO_STOP_MS`
  disables sync.
7. During normal auto-started print sync, sustained `BUF_COMPRESSION` only
   disables sync after the continuous dwell exceeds `SYNC_AUTO_STOP_MS`
   and the controller speed has collapsed to the compression-floor limit.
8. From `SYNC_RELIEF_PAUSE`, the next recovery to `BUF_NEUTRAL` or `BUF_TENSION`
   (type-D) re-arms sync; from a full-off state an eligible `BUF_TENSION` event
   bootstraps a fresh start.

---

## Sync mode auto-toggle

In `AUTO_MODE`, buffer state is the primary sync toggle. `TS:` still matters for
load completion and RELOAD handover, but it is not the main sync controller.

| Event | Sync state |
|-------|-----------|
| `BUF_TENSION` while sync is off | enabled and bootstrapped |
| `UL:`, active-lane `UM`, or `TC:` unload starts | disabled |
| tail-assist `BUF_COMPRESSION` for `SYNC_AUTO_STOP_MS` | disabled and estimator reset |
| normal-sync `BUF_COMPRESSION` at compression-floor speed for `SYNC_AUTO_STOP_MS` | `SYNC_RELIEF_PAUSE` (re-arms on `NEUTRAL`/`TENSION`, type-D) |
| `ST:` command | disabled |
---

## Dry Spin Protection

To prevent indefinite motor wear if filament is lost or snapped neutral-task, the firmware implements a global "Dry Spin" watchdog.

**Conditions for `FAULT:DRY_SPIN`:**
- Motor is spinning (`task != TASK_IDLE`).
- `IN` sensor is clear (no filament present at intake).
- Buffer is **not** in `BUF_TENSION` (the printer is not successfully pulling a remaining tail).
- This state persists for > 8 seconds.

**Effects:**
- Motor stops immediately.
- `EV:FAULT:DRY_SPIN` is emitted.
- The lane enters a sticky fault state.

**Interlocks:**
While in `FAULT_DRY_SPIN`, automatic background tasks are blocked:
- **Sync Mode**: `sync_apply_to_active` will not restart the motor if it is faulted.
- **RELOAD Follow**: `TC_RELOAD_FOLLOW` will not restart the motor if it is faulted.

**Clearing the Fault:**
- **Manual Override**: Any manual motion command (`LO:`, `FL:`, `FD:`, etc.) automatically clears the fault and starts the requested task.
- **Auto-Reset**: Inserting new filament (`IN` sensor trigger) clears the fault, allowing `AUTO_PRELOAD` or `AUTO_LOAD` to proceed.
