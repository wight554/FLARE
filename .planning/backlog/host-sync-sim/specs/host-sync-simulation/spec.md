## Purpose
Host-compiled simulation of the firmware sync control law, enabling deterministic
automated testing of sync state machines, fault handling, and control-loop
invariants without physical hardware — while keeping the physical rig as the sole
authority on tuning quality.

## ADDED Requirements

### Requirement: Host Simulation Builds Unmodified Firmware Sources
The host simulation MUST compile the firmware control sources directly from the
firmware tree, without modification and without a firmware-side
conditional-compilation branch. The compiled set MUST include `sync.c`,
`sync_buf.c`, `sync_relay.c`, `sync_analog.c`, `motion.c`, `toolchange.c`,
`cutter.c`, and `settings_store.c`.

`main.c` MUST NOT be compiled into the simulation; the simulation supplies its own
entry point and main loop.

The simulation main loop MUST invoke the firmware tick functions in the same order
as `firmware/src/main.c`, so that the simulated system is the system under test:
sensor update, `buffer_stabilize_tick`, `cutter_tick`, `tc_tick`,
`autopreload_tick`, `lane_tick` per lane, `buf_sensor_tick`, `sync_tick`.

Omitting `lane_tick` or `tc_tick` would leave lane and toolchange state machines
frozen, so RELOAD and toolchange scenarios MUST NOT be authored against a loop that
skips them.

Hardware headers absent on the host MUST be satisfied by shim headers under
`tests/host/shims/`, placed earlier in the include path than `firmware/include`. A
shim MUST declare only the types and functions the firmware sources actually
reference.

The simulation MUST NOT require the Pico SDK.

The simulation MUST support both buffer sensor types — type-D dual-endstop
(`BUF_SENSOR_TYPE=0`) and type-P analog (`BUF_SENSOR_TYPE=1`) — selectable per
scenario, so a defect suspected on one sensor type can be tested against both.

#### Scenario: Firmware sources are shared, not forked
- **WHEN** a developer edits `firmware/src/sync.c`
- **THEN** the host simulation compiles that edited source on its next build
- **AND** no copy of the sync sources exists under `tests/host/`

#### Scenario: A scenario runs against both sensor types
- **WHEN** a scenario is registered for both sensor types
- **THEN** it executes once per type
- **AND** each run reports its sensor type in the trace

#### Scenario: Firmware binary is unaffected
- **WHEN** the host simulation is added or changed
- **THEN** the firmware build output is byte-identical to the build before the change
- **AND** no file under `firmware/src/` or `firmware/include/` is modified

### Requirement: Simulation Runs Without Hardware
The simulation MUST run on any machine with a C11 toolchain, CMake, and Python 3.
It MUST NOT require an MMU, a serial port, a network connection, the FLARE daemon,
a Klipper host, or any Raspberry Pi hardware.

The simulation MUST NOT read wall-clock time; all timekeeping derives from the
simulated tick counter.

#### Scenario: Simulation runs on a machine with no MMU attached
- **WHEN** a developer with no FLARE hardware runs the scenario suite
- **THEN** every scenario executes and reports results
- **AND** no serial device, daemon, or network resource is opened

Runs MUST be deterministic on a given machine and toolchain: the same scenario run
twice MUST produce an identical trace. No result may depend on host timing,
scheduling, process ordering, or uninitialized memory.

Bit-identical traces across different machines MUST NOT be required. The sync sources
call `exp`, `expf`, and `sqrtf`; `sqrtf` is correctly rounded under IEEE-754, but
`exp` and `expf` are not, and differ in their final bits between libm
implementations. Assertions MUST therefore compare floating-point values with
explicit tolerances rather than exact equality.

#### Scenario: A scenario is repeatable on one machine
- **WHEN** the same scenario runs twice on the same machine and toolchain
- **THEN** both traces are identical

#### Scenario: Assertions survive a libm difference
- **WHEN** the same scenario runs on two machines with different libm implementations
- **THEN** control decisions, state transitions, and emitted events agree
- **AND** no assertion fails solely because a float differs in its final bits

### Requirement: Suite Runtime And Trace Volume
Each scenario MUST declare a tick ceiling bounding its simulated duration. A scenario
requiring a ceiling beyond the default MUST state why.

Traces MUST be discarded for passing scenarios and retained only for failing ones.

The suite MUST NOT be made faster by abandoning process-per-scenario isolation. When
suite runtime becomes a problem, the permitted remedy is sharding scenarios across
parallel processes, because scenarios share no state. Reusing a single process across
scenarios is prohibited: it reintroduces cross-scenario global state leakage and makes
results order-dependent.

#### Scenario: A long scenario is bounded
- **WHEN** a scenario would simulate longer than its declared tick ceiling
- **THEN** the run terminates at the ceiling and the scenario fails as inconclusive

#### Scenario: Passing runs leave no trace files
- **WHEN** the full suite passes
- **THEN** no trace output is retained

#### Scenario: Speed is bought with sharding, not shared state
- **WHEN** suite runtime needs reducing
- **THEN** scenarios are distributed across parallel processes
- **AND** no two scenarios execute within the same process

### Requirement: Scenario Isolation By Process
Each simulation scenario MUST execute in a freshly spawned process, running exactly
one scenario before exiting.

Scenario startup MUST proceed in this order: zero-initialized globals,
`settings_defaults()`, scenario-specific setting overrides, `sync_init()`, then the
tick loop.

The simulation MUST NOT rely on a hand-maintained global-reset function to isolate
scenarios.

#### Scenario: No state leaks between scenarios
- **WHEN** two scenarios run in sequence
- **THEN** the second scenario observes no residual global state from the first
- **AND** scenario execution order does not affect results

### Requirement: Runtime Globals Generated From Firmware Definitions
Runtime global definitions consumed by the sync law MUST be generated at build time
by extracting the top-level `g_` definitions from `firmware/src/main.c`, copying
their initializers verbatim.

The generated source MUST NOT be committed in edited form or maintained by hand.

`firmware/src/settings_store.c` MUST be linked against a RAM-backed flash shim so
that `settings_defaults()` and the settings save/load round trip execute as real
firmware code rather than simulation reimplementations.

#### Scenario: A changed default propagates without simulation edits
- **WHEN** a tunable's initializer changes in `firmware/src/main.c`
- **THEN** the simulation uses the new value on its next build
- **AND** no file under `tests/host/` is edited by hand

#### Scenario: A removed global fails loudly
- **WHEN** a global is removed from `firmware/src/main.c` while the sync sources
  still reference it
- **THEN** the simulation build fails with a link error rather than silently
  diverging

### Requirement: Buffer Plant Model
The simulation MUST model buffer slack as a single integrated state whose sign
convention matches `g_buf_pos`: positive is compression, negative is tension.

Slack MUST integrate the difference between commanded motor feed and extruder demand
over each tick, and MUST be clamped to the configured physical half-travel in both
directions.

Clamping at either rail MUST be recorded in the trace as a saturation event rather
than applied silently.

Feed MUST derive from the most recent `motor_set_rate_sps` call, signed by
`motor_set_dir`, and MUST be zero while the motor is stopped or disabled.

#### Scenario: Overfeed drives the buffer to the compression rail
- **WHEN** commanded feed exceeds extruder demand continuously
- **THEN** slack rises toward the compression rail
- **AND** reaching the rail is recorded as a saturation event in the trace

#### Scenario: Demand exceeding feed drives tension
- **WHEN** extruder demand exceeds commanded feed continuously
- **THEN** slack falls toward the tension rail

### Requirement: Fault Injection
The simulation MUST support time-scheduled injection of the following, each
activating at a specified simulated timestamp rather than only at scenario start:

- `feed_gain` — scales realized feed against commanded feed; models filament stuck
  and upstream jam
- `demand_gain` — scales realized positive demand against commanded demand; models
  underextrusion, extruder grind, and partial clog
- `retract_gain` — scales realized negative demand; models filament failing to leave
  the toolhead on retract
- `sensor_force` — overrides buffer sensor state; models a stuck-asserted sensor,
  sensor chatter, and both switches asserted simultaneously
- `switch_script` — timed IN/OUT switch transitions; models runout and Y-splitter
  events

#### Scenario: Mid-scenario jam is detected
- **WHEN** a scenario sets `feed_gain` to zero partway through a feed
- **THEN** commanded feed produces no slack change from that timestamp onward
- **AND** the firmware's jam response is observable in the trace

#### Scenario: Both switches asserted reaches the fault state
- **WHEN** `sensor_force` asserts tension and compression simultaneously on a
  type-D configuration
- **THEN** the buffer state resolves to `BUF_FAULT`

#### Scenario: Retract longer than half travel is exercised
- **WHEN** a scenario applies negative demand whose magnitude exceeds buffer half
  travel
- **THEN** the compression rail saturates
- **AND** the firmware's retract-catch behavior is observable in the trace

Correction from the original draft (which said the tension rail saturates):
negative demand means `slack_mm += (feed_mm_s - demand_mm_s) * dt` adds the
demand magnitude rather than subtracting it, driving slack toward the
compression rail — verified against this file's other two Buffer Plant Model
scenarios ("Overfeed drives the buffer to the compression rail", "Demand
exceeding feed drives tension"), which fix the model's sign convention and are
unchanged. A retract is realized here as filament arriving at the buffer
faster than it is drawn out, matching `retract_gain`'s description of a
retract that does not clear the toolhead — not as buffer-side tension.

### Requirement: Global Invariants Checked Every Tick
The simulation harness MUST evaluate the following invariants on every tick of every
scenario, without per-scenario authoring, and MUST fail the scenario on violation:

1. **Finiteness** — all published floating-point control values are finite; no NaN,
   no infinity
2. **Bounds** — commanded feed rate is non-negative and does not exceed the
   configured maximum. Correction from the original draft (which also
   required the configured minimum as a lower bound): `sync.c`'s final clamp
   is `[0, max_sps]`, not `[min_sps, max_sps]` — `g_sync_min_sps` is an
   intermediate target floor on specific paths (assist/demand floors, type-P
   smoothing ramp-up), not a hard floor on the emitted value, so transient
   sub-minimum values during ramp-up are legitimate and must not fail this
   invariant
3. **Liveness** — the transient sync states `SYNC_RETRACT_ASSIST` and
   `SYNC_RELIEF_PAUSE` exit within a universal simulated-time backstop.
   `SYNC_OFF` and `SYNC_ACTIVE` are exempt: both legitimately persist indefinitely.
4. **Fault quiescence** — while `g_sync_state` is `SYNC_FAULT_HOLD`, commanded feed
   is zero and event emission has ceased. The tick on which the state is first
   entered is exempt from the no-emission half of this check, since the
   firmware's own fault-announcement event (e.g. `SYNC,FAULT_HOLD`) fires on
   entry and is expected; it is emission on later ticks while still in
   `SYNC_FAULT_HOLD` that is the deadlock signature. A fault state that
   continues to command motion (on any tick, including entry) or continues to
   emit events past entry is a deadlock signature.
5. **Non-oscillation** — no sync state is entered more than a declared number of
   times within one scenario, catching re-entry loops.
6. **Saturation bound** — rail saturation does not persist beyond a declared bound.
7. **Event rate** — event emission rate stays below a declared ceiling.

The liveness backstop MUST be a single universal value, not a per-state table.
Per-state timeout contracts MUST be expressed as per-scenario assertions derived
from the governing runtime tunable, so that no simulation-local copy of a firmware
timeout exists.

#### Scenario: A newly added scenario inherits all invariants
- **WHEN** a developer adds a scenario without writing explicit assertions
- **THEN** every global invariant is still enforced for that scenario

#### Scenario: An infinite transient state hold fails the run
- **WHEN** the firmware enters `SYNC_RELIEF_PAUSE` and never leaves it within the
  backstop
- **THEN** the scenario fails and the trace identifies the stuck state

#### Scenario: Steady printing does not trip liveness
- **WHEN** a scenario holds `SYNC_ACTIVE` for its entire duration
- **THEN** no liveness violation is reported

#### Scenario: A fault that keeps commanding motion fails
- **WHEN** the firmware is in `SYNC_FAULT_HOLD` and commanded feed is non-zero
- **THEN** the scenario fails on the quiescence invariant

#### Scenario: A fault re-entry loop fails
- **WHEN** the firmware repeatedly enters and leaves `SYNC_FAULT_HOLD` beyond the
  declared entry count
- **THEN** the scenario fails on the non-oscillation invariant

### Requirement: Machine-Readable Trace Output
Each simulation run MUST emit a CSV trace on standard output containing at minimum
simulated timestamp, buffer position, buffer zone, commanded feed, demand,
sync state, saturation flag, and emitted events.

Calls to `cmd_event` and `cmd_event_critical` MUST be captured into the trace so
event emission is directly assertable.

#### Scenario: Spurious events are detectable
- **WHEN** the firmware emits an event a scenario does not expect
- **THEN** that event appears in the trace and the scenario can assert its absence

### Requirement: Optional Robustness Stress Mode
The simulation MUST provide an opt-in stress mode, disabled by default, adding a
first-order mechanical transport lag on feed response and a slew ceiling on commanded
rate.

Stress mode MUST be selectable without editing scenario definitions, so that a
stressed run differs from its baseline run only by that setting.

Stress mode is a robustness margin check. It MUST NOT be described or relied upon as
a fidelity model of filament compliance or motor pull-in behavior.

**The transport lag MUST be swept, not fixed.** Stress runs MUST execute a scenario
across a range of lag values and report the value at which the scenario first fails,
yielding a lag-margin figure rather than a pass or fail against a single chosen
constant. The swept range MUST start no lower than one control period
(`g_sync_tick_ms`), because lag shorter than one control period is not observable by
the controller.

**The stress lag MUST NOT reproduce the analog sensing filter.** The buffer position
EWMA at `firmware/src/sync_buf.c` (`g_buf_analog_alpha`) is sensing lag that already
executes inside the simulation, since the simulation compiles that source. Its time
constant is approximately 90 ms at the default alpha of 0.20 and a 20 ms tick. Stress
lag models mechanical transport — motor step to buffer slack actually moving — which
is physically separate and additive. Setting stress lag from the sensing figure would
double-count and render stress failures unattributable.

The slew ceiling MUST be derived from the firmware's own declared ramp rate
(`g_ramp_step_sps` per `g_ramp_tick_ms`) rather than a simulation-local constant.

#### Scenario: Stress run isolates the added dynamics
- **WHEN** a scenario is run with and without stress mode
- **THEN** the only difference between the two runs is the added transport lag and
  slew ceiling

#### Scenario: Stress reports a margin, not a verdict
- **WHEN** the stress suite runs a scenario across the lag sweep
- **THEN** it reports the lag value at which that scenario first fails
- **AND** it does not assert against a single fixed lag constant

#### Scenario: Ramp ceiling tracks a firmware change
- **WHEN** `g_ramp_step_sps` or `g_ramp_tick_ms` changes in the firmware
- **THEN** the stress slew ceiling changes with it, with no edit under `tests/host/`

### Requirement: Simulation Authority Boundary
Simulation results MUST be treated as authoritative only for: deadlock, unreachable
state, sign error, timer scoping, non-finite values, unbounded saturation, event
storms, and fault-path reachability.

The physical rig MUST remain the sole authority for tuning quality, control gain
selection, and sensor noise behavior.

A passing simulation run MUST NOT be used to check off a task prefixed `HW:`.

The simulation's unmodeled effects MUST be documented, and MUST include at minimum:
filament compliance and stretch, motor pull-in limit and step skip, spool tangle,
PTFE drag variation, filament diameter variance, thermal effects on flow, type-P
Hall sensor nonlinearity, ADC noise, and the `motion.c` debouncer.

#### Scenario: Hardware task is not satisfied by simulation
- **WHEN** a change includes a task prefixed `HW:`
- **THEN** a passing simulation run does not check off that task
- **AND** explicit user confirmation backed by real-hardware results is still required

#### Scenario: A tuning gain change is not certified by simulation
- **WHEN** a control gain change passes all simulation scenarios
- **THEN** the change is not thereby considered hardware-validated
