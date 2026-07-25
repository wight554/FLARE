# Design — Host Sync Simulation Harness

## Research Findings

Measured, not assumed.

### Hardware coupling per translation unit

| File | LOC | pico/hardware includes |
|---|---|---|
| `sync.c` | 1994 | 0 |
| `sync_buf.c` | 982 | 1 |
| `sync_relay.c` | 149 | 0 |
| `sync_analog.c` | 134 | 0 |

`sync_buf.c` hardware contact is three lines in one function: `sync_buf.c:380`
`to_ms_since_boot(get_absolute_time())`, `sync_buf.c:381` `adc_select_input`,
`sync_buf.c:384` `adc_read`.

### Three properties that make this cheap

1. **Time already injected.** `now_ms` is explicit parameter through `sync_init`,
   `buffer_stabilize_tick`, `baseline_update_on_settle`, `sync_buffer_lock_prime`,
   `sync_buffer_lock_locked`, `handle_bl_watchdog_timeout`,
   `sync_retract_assist_release`. One residual wall-clock call (`sync_buf.c:380`).
2. **Sensors are struct reads.** `lane_in_present` / `lane_out_present` are
   `static inline` on `L->in_sw.state` (`controller_shared.h:151-158`). `lane_t` is
   plain struct (`controller_shared.h:64-95`). Nothing to stub.
3. **Actuator seam is narrow.** `motor_set_rate_sps`, `motor_enable`, `motor_set_dir`,
   `motor_stop`, `lane_start`, `lane_stop`.

### Measured link surface

`nm -u` over the four compiled sync objects, libc removed: **104 symbols**.

| Group | Count |
|---|---|
| Tunable globals | 72 |
| State globals (`g_now_ms`, `g_lane_l1`, `g_lane_l2`, `g_active_lane`, `g_tc_ctx`, `g_auto_mode`, `g_reload_mode`, `g_y_split`) | 8 |
| Functions | 24 |

Functions, complete list: `adc_read`, `adc_select_input`, `clamp_f`, `clamp_i`,
`cmd_event`, `cmd_event_critical`, `cutter_busy`, `get_absolute_time`, `lane_ptr`,
`lane_start`, `lane_stop`, `lane_to_idx`, `manual_unload_active`,
`motion_clamp_rate_sps`, `motor_enable`, `motor_set_dir`, `motor_set_rate_sps`,
`motor_stop`, `reload_trigger`, `set_active_lane`, `set_toolhead_filament`,
`sps_to_mm_per_min`, `tc_state`, `to_ms_since_boot`.

### Header chain blocker

`sync_internal.h` → `buf_signal.h` → `controller_shared.h:9` → `pico/stdlib.h`
(supplies `uint`, `absolute_time_t`) and `controller_shared.h:10` → `tmc2209.h` →
`hardware/pio.h`, `pico/types.h`.

Resolved by shim include dir, ordered before `firmware/include`. No firmware edit.

### Sensor injection points

- **Type-D**: `buf_state_raw()` reads `on_al(&g_buf_tension_din)` /
  `on_al(&g_buf_compression_din)` (`sync_buf.c:495-496`). Inject by writing `.state`
  on those `debounced_input_t` globals. Bypasses `motion.c` debouncer — sim tests
  sync tolerance of noisy state, not debounce itself. Stated limitation.
- **Type-P**: `buf_state_raw()` compares `g_buf_pos` against `psf_goal_norm() ± 0.1`
  (`sync_buf.c:485-492`); `g_buf_pos` derived from `adc_read()` at `sync_buf.c:384`.
  Inject via ADC fake.

Both converge on `g_buf_pos`. Sign convention: `+` compression, `−` tension.

### Host-compile status of the full firmware

Measured with the shim set below.

| Unit | Status |
|---|---|
| `sync.c`, `sync_buf.c`, `sync_relay.c`, `sync_analog.c` | clean |
| `toolchange.c`, `protocol_status.c`, `protocol_tmc.c`, `settings_store.c` | clean |
| `motion.c` | 5 errors, all PWM config API (`pwm_config`, `pwm_init`, `pwm_gpio_to_channel`, `pwm_set_chan_level`) |
| `protocol.c` | 1 error, missing `pico/bootrom.h` |
| `tmc2209.c`, `neopixel.c` | 1 error each, generated PIO headers — out of scope |

Everything except `main.c` is reachable. `main.c` stays excluded by design: it owns
`main()`, GPIO/PIO init, LEDs, and the real loop. `sim_main.c` replaces it.

### Main loop order (must be replicated)

From `main.c:569`:

```
debounced_input_update x7   -> sim writes sensor state from plant instead
cmd_poll(g_now_ms)          -> skipped (USB)
buffer_stabilize_tick(g_now_ms)
cutter_tick(g_now_ms)
tc_tick(g_now_ms)
autopreload_tick(g_now_ms)
lane_tick(&g_lane_l1, g_now_ms)
lane_tick(&g_lane_l2, g_now_ms)
buf_sensor_tick(g_now_ms)
sync_tick(g_now_ms)
neopixel_tick(g_now_ms)     -> skipped (PIO)
```

`lane_tick` and `tc_tick` drive lane ramping, task state, and toolchange
sequencing. A sync-only harness omitting them would freeze those state machines,
so RELOAD and toolchange scenarios would be meaningless. This is why the change
compiles `motion.c`, `toolchange.c`, and `cutter.c` rather than sync alone.

## Architecture

```
tests/host/
  CMakeLists.txt        standalone project, no pico_sdk_init()
  shims/pico/stdlib.h   uint, absolute_time_t, clock decls
  shims/pico/types.h
  shims/hardware/pio.h  opaque PIO typedef
  shims/hardware/adc.h
  shims/hardware/flash.h  FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE, erase/program
  shims/hardware/sync.h   save_and_disable_interrupts / restore_interrupts
  shims/pico/flash.h      PICO_FLASH_SIZE_BYTES, XIP_BASE, flash_safe_execute
  sim_globals.c         GENERATED from main.c g_ definitions (not hand-edited)
  sim_fakes.c           actuators, queries, events, clock, ADC
  sim_plant.c           kinematic buffer model + fault injection
  sim_scenario.c        scenario table, demand profiles
  sim_trace.c           CSV emission + global invariant checks
  sim_main.c            tick loop, CLI arg parse
scripts/test_sync_sim.py  unittest: declares scenarios, runs binary, asserts trace
```

Include order: `tests/host/shims` before `firmware/include`.

Sources linked: the four sync `.c` files + `settings_store.c`, unmodified, from
`firmware/src/`.

### One scenario per process

Key decision. Each invocation runs exactly one scenario, then exits.

Consequence: globals start zero-initialized every run. `sync.c` has 654 `g_`
references, `sync_buf.c` has 316. No `sync_test_reset()` needed, no parity test to
guard one, no cross-test state leakage possible. Process spawn cost is ~ms; scenario
count is the only thing that scales.

Boot sequence per run: zero-init → `settings_defaults()` → scenario overrides →
`sync_init(0)` → tick loop.

### Globals: generated, not hand-written

Measured, and it corrected an earlier assumption. `firmware/src/settings_store.c`
compiles on host but defines only **4** symbols — it *assigns* to the tunables
without owning them. All 145 `g_` definitions live in `main.c` (`main.c:127`
`int g_sync_kp_sps = CONF_SYNC_KP_SPS;` and following), initialized from `CONF_*`
macros supplied by `config.h` / `tune.h`.

`main.c` cannot be linked into the sim — 11 pico/hardware includes, plus `main()`,
GPIO init, PIO, LEDs, and the real main loop.

Resolution: build-time generator extracts the top-level `g_` definitions from
`main.c` and emits `tests/host/sim_globals.c` with the initializers copied verbatim.
Precedent: `scripts/gen_config.py` already generates `firmware/include/tune.h` as a
CMake custom command. The same regex-over-C technique is already in-tree in
`scripts/test_settings_parity.py`.

Why generation rather than a hand-written list:

- initializers are copied verbatim, so a wrong value is not expressible
- a tunable added to `main.c` appears in the sim on the next build
- a tunable removed from `main.c` produces a link error, not silent divergence

`settings_store.c` is still linked against a RAM-backed flash shim, so
`settings_defaults()` (`settings_store.c:265`) and the save/load round trip are
exercised as real code rather than reimplemented.

## Plant Model

Single state variable, `slack_mm`, sign convention matching `g_buf_pos`.

```
feed_mm_s   = last_motor_rate_sps * g_mm_per_step * dir_sign * feed_gain
              (zero when motor_stop or motor_enable(false))
demand_mm_s = profile(t) * (demand_gain if profile>0 else retract_gain)
slack_mm   += (feed_mm_s - demand_mm_s) * dt
slack_mm    = clamp(slack_mm, +/- g_buf_max_travel_mm/2)     // saturation flagged
```

Saturation events are flagged in trace, not silently clamped — several known defects
live at the rails (rail-break, overshoot off saturated tension rail).

Tick advances `g_now_ms` by `g_sync_tick_ms`.

### Demand profiles

`steady`, `step_up` (slow→fast transition behind type-D step-skip), `burst`
(tip-form start/stop), `idle_zero` (frozen distance-clock case), `retract`
(negative), `long_retract` (negative, magnitude > half travel).

## Fault Injection API

Entire Class-1 failure table reduces to five time-scheduled knobs.

| Knob | Range | Models |
|---|---|---|
| `feed_gain` | 0..1 | filament stuck, upstream jam (0 = seized) |
| `demand_gain` | 0..1 | underextrusion, extruder grind, partial clog |
| `retract_gain` | 0..1 | filament will not leave toolhead on retract |
| `sensor_force` | per-pin | stuck asserted, chatter, both-switches-asserted |
| `switch_script` | timed | runout, Y-splitter transitions |

Time-scheduled, not set at t=0: faults arriving mid-scenario are what break state
machines.

Coverage mapping:

| Failure | Injection |
|---|---|
| upstream jam | `feed_gain=0` at t |
| grind / slip | `demand_gain=0` at t |
| underextrusion | `demand_gain=0.3..0.8` |
| long retract > half travel | `long_retract` profile |
| filament stuck in toolhead | `retract_gain=0` |
| runout mid-print | `switch_script` |
| sensor chatter | `sensor_force` single-tick flips |
| sensor stuck asserted | `sensor_force` frozen |
| both switches asserted | `sensor_force` both → `BUF_FAULT` (`sync_buf.c:498`) |
| extruder abrupt stop | `idle_zero` profile |

## Stress Mode

`--stress` flag, off by default. Adds first-order mechanical transport lag on feed
response plus slew ceiling on commanded rate — crude stand-ins for filament
compliance and motor pull-in limit.

Not a fidelity claim. Purpose: robustness margin. Off by default so ordinary failures
stay attributable; stress suite reruns the same scenarios with only that flag changed.

### Transport lag is swept, not chosen

A single default was drafted at 80 ms and rejected on inspection. Provenance of that
number: the `typep-stabilize-overshoot-compression` observation records BS stabilize
EMA lag at ~80 ms. It checks out — `FLARE_INT_BUF_ANALOG_ALPHA = 0.20`
(`tune_internal.h:81`) applied at `sync_buf.c:413` at `CONF_SYNC_TICK_MS = 20` gives

```
tau_ema = -dt / ln(1 - alpha) = -20 / ln(0.8) ~= 90 ms
```

But that is **sensing** lag, and it already runs inside the sim — the sim compiles
`sync_buf.c:413` verbatim. Stress lag models **mechanical transport**: motor step ->
filament compliance and bowden slack takeup -> buffer slack actually moves.
Physically separate, additive.

Using 80 ms would double-count: ~170 ms total in sim against ~90 ms sensing plus an
unknown mechanical term on hardware. Stress failures would be pessimistic by an
unmeasured margin — worse than no stress mode, because unattributable.

Resolution: sweep `tau` over {0, 20, 50, 100, 200} ms, report where each scenario
first breaks. Output is a margin — "tolerates transport lag to 50 ms" — so no single
value needs justifying. Floor of 20 ms because lag below one control period
(`g_sync_tick_ms`) is invisible to the controller.

Alternatives for a single value, if one is ever wanted:

- **Measure on rig** — step commanded rate, log buffer position, fit first-order.
  Infrastructure exists (`flare_daemon.py` telemetry, analyzer CSV). Small `HW:` task.
  Valid for one bowden length and filament, not universal.
- **Physics from first principles** — rejected; needs elastic modulus, bowden length,
  drive force.

### Slew ceiling has provenance the lag lacks

Asymmetry worth stating. The firmware declares its own ramp: `CONF_RAMP_STEP_SPS
= 5115` per `CONF_RAMP_TICK_MS = 5`, and `CONF_SYNC_RAMP_UP_SPS =
CONF_SYNC_RAMP_DN_SPS = 5729`. There is also a measured stall datum — `79806bc` fixed
a follow stall caused by an instant `motor_set_rate_sps` jump, resolved by ramping to
~83 mm/s.

So the ceiling reads `g_ramp_step_sps` / `g_ramp_tick_ms` at runtime rather than
hardcoding. A firmware ramp change moves the stress ceiling with it.

## Assertion Model

### Global invariants — every tick, every scenario

Checked by harness without per-test authoring. Highest value: these catch defects
nobody wrote a test for.

States are `SYNC_OFF`, `SYNC_ACTIVE`, `SYNC_RETRACT_ASSIST`, `SYNC_RELIEF_PAUSE`,
`SYNC_FAULT_HOLD` (`sync.h:6-12`). `OFF` and `ACTIVE` legitimately persist forever —
idle and steady printing — so a blanket per-state ceiling would false-fire on the two
most common states. Liveness applies only to the transient set.

1. **Finiteness** — no NaN / Inf in `g_buf_pos`, feed rate, estimator outputs
2. **Bounds** — commanded feed within `[g_sync_min_sps, g_sync_max_sps]` after clamping
3. **Liveness** — `SYNC_RETRACT_ASSIST` and `SYNC_RELIEF_PAUSE` exit within a
   universal backstop; `SYNC_OFF` / `SYNC_ACTIVE` exempt
4. **Fault quiescence** — in `SYNC_FAULT_HOLD`, commanded feed is zero and emission
   has ceased
5. **Non-oscillation** — no state entered more than N times per scenario
6. **Saturation bound** — rail saturation duration bounded
7. **Event rate** — `cmd_event` rate under ceiling

#### Why liveness, not per-state timeouts

Two different checks were conflated in the first draft:

- **Liveness** — "does this ever end?" A crude universal backstop suffices, costs
  nothing to maintain, cannot drift. Catches the infinite-fault-hold class.
- **Timing contract** — "does it end within its specified timeout?" Real, but
  per-state, and belongs in per-scenario assertions derived from the governing
  tunable (`g_sync_tension_dwell_stop_ms`, `g_neutral_creep_timeout_ms`,
  `g_psf_stab_rail_break_ms`, …).

A sim-local `{state, max_ms}` table was rejected: second source of truth, goes stale
silently when a tunable changes — too loose misses bugs, too tight false-fires — and
it looks precise enough to be trusted.

Invariants 4 and 5 exist because `SYNC_FAULT_HOLD` may legitimately be terminal until
an operator clears it, so "must exit" is the wrong contract. The shipped defect was a
deadlock *loop* — spurious re-entry with continued emission. Quiescence plus
non-oscillation detect that signature directly; a duration ceiling would not.

### Per-scenario assertions

Trace-based: expected `EV:` emitted, unexpected `EV:` absent (spurious `FOLLOW_JAM`),
terminal sync state reached, `slack_mm` band held.

### Trace format

CSV on stdout, columns mirroring analyzer shape where meaningful: `ts_ms`, `bp_mm`,
`zone`, `feed_sps`, `demand_mm_s`, `sync_state`, `sat`, `events`. `events` column
carries captured `cmd_event` / `cmd_event_critical` strings — makes event emission
directly assertable.

## Runtime Budget

Measured on a dev host, not estimated. Benchmark: 3000-tick binary emitting 3000 CSV
rows, driven exactly as the harness will drive it.

```
raw binary, 3000 ticks + 3000 rows           ~18 ms/run
python subprocess + CSV parse of 3000 rows   ~31 ms/run   <- dominant cost
```

Compute is free; process spawn plus parse dominates.

| Tier | Runs | Projected |
|---|---|---|
| Baseline (~22 scenarios x 2 sensor types) | 44 | ~1.4 s |
| Stress sweep (5 lag values x 44) | 220 | ~6.8 s |
| Total | 264 | ~8.2 s |

Conclusion: no tier split needed. Both baseline and stress fit in the pre-commit gate.
The simulation requires no MMU, serial port, or daemon, so it runs on any developer
machine — the Pi-performance conditional does not apply.

Three controls, because these are what would actually break the budget:

1. **Per-scenario tick ceiling, not a suite-wide number.** 3000 ticks is 60 s
   simulated at `g_sync_tick_ms = 20`. A scenario simulating a 30-minute print is
   90,000 ticks — 30x the cost by itself. Bound it at the scenario level.
2. **Discard traces on pass.** 3000 rows x 264 runs is ~790k rows, ~47 MB retained.
   Keep only failing traces, which is also when they are wanted.
3. **Never trade process isolation for speed.** Process-per-scenario is what makes the
   970-global reset problem vanish. If the suite ever gets slow, shard it — scenarios
   share no state. Reusing one process reintroduces state leakage and order-dependent
   results, rotting every test silently. Specified as a prohibition.

## Known Limitations

Specified normatively so sim results are never over-read.

Not modeled, permanently out of scope for this change:

- filament compliance / stretch in long bowden — the phase lag the PD law fights;
  single largest reason a sim-tuned gain may not transfer
- motor pull-in limit and step-skip — sim accepts instant rate jumps a real motor
  stalls on (cf. `79806bc`)
- spool tangle / slip-knot; onset unpredictable
- PTFE drag varying with spool height, buffer fill, tube length
- filament diameter variance, thin sections
- blunted tip after cut snagging in PTFE
- hotend thermal → viscosity → real vs commanded flow
- type-P Hall nonlinearity, magnet drift, piston stiction
- ADC noise / EMI — injectable, but real spectrum unknown; modeling it would be
  theater
- `motion.c` debouncer (bypassed by `.state` injection)
- exact firmware float behavior. The sim links host libm; the firmware links picolibc
  on Cortex-M0+. `sqrtf` is correctly rounded under IEEE-754 and agrees, but `exp` and
  `expf` (one and two call sites in the sync sources) are not correctly rounded and
  differ in final bits between implementations. Immaterial for control decisions;
  it does mean the sim cannot certify bit-exact numeric behavior, and that assertions
  must use tolerances rather than float equality.

## Risks

| Risk | Mitigation |
|---|---|
| Sim pass mistaken for hardware validation | Normative authority boundary; sim pass MUST NOT check off `HW:` task |
| Plant too crude → false confidence in gains | Sim explicitly not authority on tuning quality; stress mode gives lag margin evidence |
| Fake drifts from real `motion.c` semantics | Fakes cover 6 actuator functions; behavior is record-only, no logic to drift |
| Shim drifts from Pico SDK | Shims declare only `uint`, `absolute_time_t`, opaque `PIO`, 2 ADC fns — all stable |
| Sim rots as sync evolves | Wired into `validate_regression.py` gate; breaks loudly |

## Alternatives Rejected

- **`sync_test_reset()` + in-process scenarios** — needs hand-maintained reset over
  970 global references plus a parity test to guard it. Process-per-scenario gets the
  same isolation for free.
- **Hand-writing the 80 global definitions in the sim** — duplicates a list that
  changes with every tuning change and drifts silently. Generation from `main.c`
  cannot drift: a removal becomes a link error, and initializers are copied verbatim.
- **Linking `main.c` for its globals** — impossible; 11 pico/hardware includes plus
  `main()`, GPIO/PIO init, and the real main loop.
- **Extracting globals into a new `firmware/src/runtime_globals.c`** — cleanest in
  principle and a mild improvement to `main.c`, but it is a firmware edit. Rejected to
  keep the zero-firmware-change property, which is what makes this change
  hardware-validation-free. Reconsider if the generator proves fragile.
- **Full-firmware host build now** — requires PIO header generation for `tmc2209.c` /
  `neopixel.c`. Deferred; layout does not preclude it.
- **Pure-C test binary with `assert()`** — loses unittest reporting and reuse of
  existing `scripts/test_*.py` convention and `validate_regression.py` discovery.
