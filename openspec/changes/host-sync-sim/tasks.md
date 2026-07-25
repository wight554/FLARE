# Tasks — Host Sync Simulation Harness

Host-only change. No `firmware/src/**` or `firmware/include/**` edits in any task.
Any task requiring a firmware edit is a design failure — stop and revise `design.md`.

## 1. Build skeleton

- [ ] `tests/host/shims/pico/stdlib.h` — declare `uint` (`unsigned int`),
      `absolute_time_t` (`uint64_t`), `to_ms_since_boot`, `get_absolute_time`,
      `sleep_ms`. Accept: header compiles standalone under `cc -std=c11 -fsyntax-only`.
- [ ] `tests/host/shims/pico/types.h` — include `pico/stdlib.h` shim only.
- [ ] `tests/host/shims/hardware/pio.h` — opaque `typedef struct pio_hw *PIO;`.
      Required by `tmc2209.h` via `controller_shared.h:10`.
- [ ] `tests/host/shims/hardware/adc.h` — `adc_select_input(uint32_t)`,
      `adc_read(void) -> uint16_t`.
- [ ] `tests/host/shims/hardware/flash.h` — `FLASH_PAGE_SIZE`, `FLASH_SECTOR_SIZE`,
      `flash_range_erase`, `flash_range_program`.
- [ ] `tests/host/shims/hardware/sync.h` — `save_and_disable_interrupts`,
      `restore_interrupts`.
- [ ] `tests/host/shims/pico/flash.h` — `PICO_FLASH_SIZE_BYTES`, `XIP_BASE`,
      `flash_safe_execute`. Accept: `cc -c -std=c11 -I firmware/include -I tests/host/shims firmware/src/settings_store.c`
      compiles clean (verified during design).
- [ ] `tests/host/shims/hardware/gpio.h` — `GPIO_IN`, `GPIO_OUT`, `GPIO_FUNC_PWM`,
      `gpio_init/set_dir/get/put/pull_up/pull_down/set_function/disable_pulls`.
- [ ] `tests/host/shims/hardware/pwm.h` — `pwm_config`, `pwm_init`,
      `pwm_gpio_to_slice_num`, `pwm_gpio_to_channel`, `pwm_set_chan_level`,
      `pwm_set_gpio_level`, `pwm_set_enabled`, `pwm_set_wrap`, `pwm_set_clkdiv`.
      Required by `motion.c` (5 measured errors) and `cutter.c`.
- [ ] `tests/host/shims/hardware/clocks.h` — `clock_get_hz`, `clk_sys`.
- [ ] `tests/host/CMakeLists.txt` — standalone project, no `pico_sdk_init()`. Target
      `flare_sim`. Include order: `tests/host/shims` then `firmware/include`. Sources:
      `firmware/src/{sync,sync_buf,sync_relay,sync_analog,motion,toolchange,cutter,settings_store}.c`
      plus `tests/host/sim_*.c`. `main.c` MUST NOT be listed.
      Flags `-std=c11 -Wall -Wextra`.
      Accept: `cmake -S tests/host -B build_sim && ninja -C build_sim` links `flare_sim`.
      Measured baseline: `toolchange.c` and `settings_store.c` already compile clean;
      only `motion.c` and `cutter.c` need the PWM shim.

## 2. Fakes

Undefined-symbol surface is authoritative. Regenerate it rather than trusting this
list:
`nm -u build_sim/CMakeFiles/flare_sim.dir/**/*.o | sort -u`

- [ ] `tests/host/sim_fakes.c` actuators — `motor_set_rate_sps`, `motor_enable`,
      `motor_set_dir`, `motor_stop`, `lane_start`, `lane_stop`. Record-only: store last
      commanded rate/dir/enable into sim state, no logic. Accept: plant reads feed from
      recorded rate.
- [ ] `tests/host/sim_fakes.c` queries — `lane_ptr`, `lane_to_idx`, `cutter_busy`,
      `tc_state`, `manual_unload_active`, `motion_clamp_rate_sps`. `lane_ptr` returns
      pointer to sim-owned `lane_t`.
- [ ] `tests/host/sim_fakes.c` events — `cmd_event`, `cmd_event_critical` append the
      formatted string to a per-tick event buffer consumed by the trace writer.
      Accept: an emitted event appears in the trace `events` column on the tick emitted.
- [ ] `tests/host/sim_fakes.c` clock — `get_absolute_time` / `to_ms_since_boot` return
      simulated `g_now_ms`. No wall-clock read anywhere in the sim.
- [ ] `tests/host/sim_fakes.c` ADC — `adc_select_input` no-op, `adc_read` returns counts
      mapped from plant slack. Accept: type-P `g_buf_pos` tracks plant slack.
- [ ] `tests/host/sim_fakes.c` helpers — `clamp_f`, `clamp_i`, `sps_to_mm_per_min`,
      `reload_trigger`, `set_active_lane`, `set_toolhead_filament`.
      Accept: `ninja -C build_sim` links with zero undefined symbols.

## 2b. Global generator

`firmware/src/settings_store.c` does NOT define the tunables — it only assigns to
them. All 145 `g_` definitions live in `firmware/src/main.c:127+`. `main.c` is not
linkable (11 pico/hardware includes, `main()`, GPIO/PIO init, main loop).

- [ ] `scripts/gen_sim_globals.py` — parse top-level `g_` definitions out of
      `firmware/src/main.c`, emit `sim_globals.c` with initializers copied verbatim.
      Reuse the brace-matching / assignment-regex approach already in
      `scripts/test_settings_parity.py`. Accept: emitted file compiles, and every one
      of the 80 globals in the measured link surface resolves.
- [ ] `tests/host/CMakeLists.txt` — wire the generator as a custom command depending on
      `firmware/src/main.c`, mirroring how `firmware/CMakeLists.txt` drives
      `scripts/gen_config.py` → `tune.h`. Accept: touching `main.c` regenerates.
- [ ] `.gitignore` — add the generated `sim_globals.c`; it is never hand-edited.
- [ ] Verify no `g_` definition is hand-written in `sim_fakes.c`.
      Accept: `grep -c '^[a-z_].*g_[a-z_]* *=' tests/host/sim_fakes.c` returns 0.

## 3. Plant

- [ ] `tests/host/sim_plant.c` — `slack_mm` integrator per `design.md` Plant Model.
      Clamp to `±g_buf_max_travel_mm/2`, set saturation flag on clamp.
      Accept: constant overfeed reaches compression rail and flags saturation.
- [ ] `tests/host/sim_plant.c` — type-D sensor emission: write
      `g_buf_tension_din.state` / `g_buf_compression_din.state` from slack vs threshold.
      Accept: `buf_state_raw()` returns `BUF_TENSION`/`BUF_COMPRESSION` at the expected
      slack values.
- [ ] `tests/host/sim_plant.c` — type-P emission: linear map slack → ADC counts.
      Accept: `g_buf_pos` sign matches slack sign (`+` compression, `−` tension).
- [ ] `tests/host/sim_scenario.c` — demand profiles `steady`, `step_up`, `burst`,
      `idle_zero`, `retract`, `long_retract`. Piecewise over simulated time.

## 4. Fault injection

- [ ] `tests/host/sim_plant.c` — `feed_gain`, `demand_gain`, `retract_gain`, each
      time-scheduled. Accept: gain applied from its scheduled timestamp, not t=0.
- [ ] `tests/host/sim_plant.c` — `sensor_force`: per-sensor override supporting
      stuck-asserted, single-tick chatter, both-asserted.
      Accept: both-asserted yields `BUF_FAULT` (`sync_buf.c:498`).
- [ ] `tests/host/sim_scenario.c` — `switch_script`: timed IN/OUT transitions writing
      `lane_t` debounced input state.

## 5. Trace and invariants

- [ ] `tests/host/sim_trace.c` — CSV writer, columns `ts_ms,bp_mm,zone,feed_sps,
      demand_mm_s,sync_state,sat,events`. Header row first.
- [ ] `tests/host/sim_main.c` — per-scenario tick ceiling with a default; exceeding it
      terminates the run and fails the scenario as inconclusive. A scenario overriding
      the default must record why. Accept: a runaway scenario terminates rather than
      hanging the suite.
- [ ] `tests/host/sim_main.c` — assert no wall-clock read anywhere: all timekeeping
      comes from the simulated tick counter. Accept: `grep -rn 'time(\|clock_gettime\|gettimeofday' tests/host/`
      returns only the clock fake, which returns `g_now_ms`.
- [ ] `tests/host/sim_trace.c` — global invariants 1-7 per spec, evaluated every tick.
      Violation prints a diagnostic line and exits non-zero.
      Liveness (3) applies only to `SYNC_RETRACT_ASSIST` / `SYNC_RELIEF_PAUSE`;
      `SYNC_OFF` and `SYNC_ACTIVE` are exempt. Backstop is one universal value, not a
      per-state table.
      Accept: a deliberately stuck `SYNC_RELIEF_PAUSE` fails invariant 3 with the state
      named; a scenario holding `SYNC_ACTIVE` throughout reports no violation.
- [ ] `tests/host/sim_trace.c` — invariants 4 (fault quiescence: `SYNC_FAULT_HOLD`
      implies zero commanded feed and no further emission) and 5 (non-oscillation:
      per-state entry count ceiling). These two, not a duration ceiling, are what
      detect the shipped fault-hold deadlock loop.
- [ ] `tests/host/sim_main.c` — CLI: `--scenario NAME`, `--stress`, `--stress-lag-ms N`,
      `--ticks N`, `--sensor-type {d,p}`. Boot order per spec: zero-init → `settings_defaults()` →
      overrides → `sync_init(0)` → tick loop.
- [ ] `tests/host/sim_main.c` — tick loop replicating `main.c:569` order:
      plant sensor write → `buffer_stabilize_tick` → `cutter_tick` → `tc_tick` →
      `autopreload_tick` → `lane_tick(&g_lane_l1)` → `lane_tick(&g_lane_l2)` →
      `buf_sensor_tick` → `sync_tick`. Skip `cmd_poll` (USB) and `neopixel_tick` (PIO).
      Accept: order matches `main.c`; a diff of the two call sequences is recorded in
      the commit message. `autopreload_tick` lives in `main.c` — if it is not linkable,
      stub it and note the behavioral gap in `design.md` Known Limitations.

## 6. Stress mode

- [ ] `tests/host/sim_plant.c` — first-order mechanical transport lag and
      commanded-rate slew ceiling, both gated behind `--stress`, default off.
      Lag value comes from `--stress-lag-ms`; slew ceiling reads `g_ramp_step_sps` /
      `g_ramp_tick_ms` at runtime, never hardcoded.
      Accept: identical scenario with and without `--stress` differs only in those two
      dynamics; changing `CONF_RAMP_STEP_SPS` moves the ceiling with no `tests/host/`
      edit.
- [ ] Verify the stress lag does not duplicate the sensing EWMA. `sync_buf.c:413`
      (`g_buf_analog_alpha`, default 0.20, ~90 ms at a 20 ms tick) already runs inside
      the sim. Stress lag models mechanical transport only.
      Accept: a comment at the lag implementation states this, citing `sync_buf.c:413`.
- [ ] `scripts/test_sync_sim.py` — stress suite sweeps `--stress-lag-ms` over
      {0, 20, 50, 100, 200} and reports, per scenario, the first value at which it
      fails. Floor of 20 ms because lag below one `g_sync_tick_ms` is invisible to the
      controller. Accept: output is a per-scenario lag-margin table, not a pass/fail
      against one constant.

## 7. Python harness

- [ ] `scripts/test_sync_sim.py` — `unittest.TestCase` subclass. Builds binary path
      via `scripts/path_utils.py` convention; skips with a clear message when
      `flare_sim` is absent. Runs one subprocess per scenario, parses CSV, asserts.
      Accept: `python3 -m unittest scripts.test_sync_sim -v` passes.
      Note: `unittest discover` silently skips non-`TestCase` files — confirm the
      new tests actually appear in the run count, do not assume.
- [ ] `scripts/test_sync_sim.py` — bug-history regression scenarios, each named for
      the defect it guards: spurious `FOLLOW_JAM`, infinite type-P runout fault-hold,
      stale fault timers while sync OFF, type-P RELOAD sign, RELOAD with idle consumer,
      frozen distance-clock overfeed on abrupt extruder stop.
- [ ] `scripts/test_sync_sim.py` — fault-injection scenarios covering the full
      Class-1 table in `design.md`.
- [ ] `scripts/test_sync_sim.py` — compare floats with explicit tolerances, never exact
      equality. `exp`/`expf` are not correctly rounded and differ between libm
      implementations; `sqrtf` is exact. Assert on control decisions, state transitions,
      and events — not on last-bit float values.
      Accept: `grep -n 'assertEqual' scripts/test_sync_sim.py` shows no float comparison.
- [ ] `scripts/test_sync_sim.py` — same-machine determinism check: run one scenario
      twice, assert byte-identical traces. Catches uninitialized memory and any
      surviving wall-clock or ordering dependence.
- [ ] `scripts/test_sync_sim.py` — retain trace output only for failing scenarios;
      discard on pass. Accept: a fully passing suite leaves no trace files behind.
- [ ] `scripts/test_sync_sim.py` — one subprocess per scenario, never reused across
      scenarios. Add a comment stating that sharding, not process reuse, is the
      sanctioned remedy if the suite gets slow — process isolation is what removes the
      need to reset ~970 global references.
- [ ] `scripts/test_sync_sim.py` — verify the suite runs with no hardware present:
      no serial device, no daemon, no network. Accept: passes on a machine with no MMU
      attached. Measured budget: ~31 ms/run, ~264 runs, ~8 s total.
- [ ] `scripts/test_sync_sim.py` — run every scenario against both sensor types
      (`--sensor-type d` and `--sensor-type p`) unless a scenario is inherently
      type-specific. Accept: type-specific scenarios are explicitly marked with the
      reason; everything else runs twice.

## 8. Gate integration

- [ ] `scripts/validate_regression.py` — build `flare_sim` and run the scenario suite;
      fail the gate on build error, scenario failure, or invariant violation. Report a
      simulation build break as a firmware source defect, not a skipped test.
      Accept: `python3 scripts/validate_regression.py` exercises the sim and fails when
      a scenario is deliberately broken.
- [ ] `.gitignore` — add `build_sim/`.

## 9. Documentation

- [ ] `AGENTS.md` — add `tests/host/` to Key Files with read mode `[lookup]`; state the
      authority boundary in one line (sim screens, rig judges, sim never satisfies `HW:`).
- [ ] `TEST_CASES.md` — document the scenario catalogue and the five global invariants.
- [ ] `README.md` — one line under developer tooling on building and running the sim.

## Readiness and Delivery Checks

- [ ] Dev-tuning superset firmware build green:
      `cmake -S firmware -B build_local -DFLARE_DEV_TUNING=ON && ninja -C build_local`
- [ ] Firmware output unchanged by this change — confirm no file under `firmware/src/`
      or `firmware/include/` is modified: `git diff --name-only main -- firmware/` empty
- [ ] `python3 -m py_compile scripts/*.py`
- [ ] `python3 scripts/validate_regression.py` green, and the sim scenario count in its
      output is non-zero
- [ ] Simulation builds clean with `-Wall -Wextra`: `ninja -C build_sim` no warnings
- [ ] Documentation sync verified — `AGENTS.md`, `TEST_CASES.md`, `README.md`
- [ ] `openspec validate host-sync-sim --strict` passing
- [ ] `openspec validate --specs --strict` passing
- [ ] Observation appended to `memories/repo/host-sync-sim.md` — 3-5 compressed lines
      covering: shim set that proved sufficient, symbols that had to be faked beyond the
      measured 104, any scenario that exposed a real defect, deviations from `design.md`
- [ ] Archive with a real `## Purpose` on the new `host-sync-simulation` spec — the
      archive step stamps a placeholder that fails the spec-readability tripwire
