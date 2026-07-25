# Host Sync Simulation Harness

## Why

Sync control law = 3259 LOC across `sync.c`, `sync_buf.c`, `sync_relay.c`,
`sync_analog.c`. Zero automated coverage. Every change validated only on physical
rig: reflash + print + observe. Loop measured in tens of minutes, serialized on one
device.

Shipped bug history is dominated by logic defects a rig is a poor detector for:

- `2b35bc9` — spurious RL `FOLLOW_JAM` + infinite type-P runout fault-hold loop
- stale fault timers accumulate while sync OFF → spurious `FAULT_HOLD` + deadlock
- audit-reliability H1 — type-P RELOAD sign-flip
- audit-reliability H2 — dead dry-spin interlock
- type-P RELOAD false jams with idle extruder

All state-machine / sign / timer-scoping defects. None need filament physics to
reproduce. All reproduce deterministically against a crude plant model.

Secondary cost: tuning experiments burn rig cycles. Memory records FAILED+reverted
attempts (mid-band estimator, EST-escalation, decaying-floor, EST-clamp) — each one
a reflash-and-print.

## What Changes

Host-compiled simulation of sync control law. New `tests/host/` builds `flare_sim`
binary linking real firmware sync sources against fake actuators, fake clock, fake
sensors, and a kinematic buffer plant. Python `unittest` declares scenarios, runs
binary, asserts on emitted CSV trace.

Empirically verified before proposing: all four sync translation units compile clean
on host clang, `-std=c11`, zero warnings, **zero firmware source edits**, using four
shim headers (`pico/stdlib.h`, `pico/types.h`, `hardware/pio.h`, `hardware/adc.h`,
~15 lines total). Precedent exists — `firmware/cmake/include_patches/`.

Link surface measured by `nm -u`, exact: 104 undefined symbols.

- **72 tunable globals** (`g_sync_kp_sps`, `g_buf_max_travel_mm`, `g_relay_*`,
  `g_sync_tension_probe_*`, …) and **8 state globals** (`g_now_ms`, `g_lane_l1`,
  `g_active_lane`, …). All 145 `g_` definitions live in `main.c:127+`, initialized
  from `CONF_*` macros. Satisfied by a build-time generated `sim_globals.c` extracted
  from `main.c` — same precedent as `gen_config.py` generating `tune.h`.
- **24 functions faked**: 6 actuators, 6 queries, 2 event emitters, 2 clock, 2 ADC,
  3 pure helpers, 3 lane/toolhead callbacks.

`settings_store.c` is also linked (RAM-backed flash shim) so `settings_defaults()`
and the persistence round-trip are the real code paths, not reimplementations.
Measured correction worth recording: `settings_store.c` *assigns* to the tunables but
defines only 4 symbols — it does not own them.

## Scope

Host-only. No firmware source change. No hardware validation required — firmware
binary is byte-identical before and after.

In scope: the whole control path — `sync.c`, `sync_buf.c`, `sync_relay.c`,
`sync_analog.c`, `motion.c`, `toolchange.c`, `cutter.c`, `settings_store.c`.
`sim_main.c` replaces `main.c` and replicates its tick order.

Rationale for taking the full path rather than sync alone: `main.c:569` runs
`lane_tick` and `tc_tick` before `sync_tick`. A sync-only harness would leave lane and
toolchange state machines frozen, making RELOAD and toolchange scenarios test a
system that does not exist. RELOAD defects are a large share of the shipped bug
history, so excluding them would gut the value.

Measured host-compile status: `toolchange.c`, `protocol_status.c`, `protocol_tmc.c`,
`settings_store.c` compile clean already; `motion.c` needs ~8 lines of PWM shim;
`protocol.c` needs one `pico/bootrom.h` shim.

Out of scope for this change: `tmc2209.c` and `neopixel.c` (both need generated PIO
headers), and wire-level `protocol.c` tests. Shim and fake layout is chosen so those
can be added later without rework.

Both buffer sensor types are covered — type-D and type-P, selectable per scenario.
`bl-retract-catch-hardening` was raised from a type-P observation, but the same
defect class may affect type-D; running scenarios against both is how that gets
answered.

## Sequencing

This change lands before `bl-retract-catch-hardening`. That change is 0/55 and its
subject — retract catch, prime bound, retract exceeding buffer half travel — is
precisely what the simulation tests. Implementing it against the simulation is both
the first real use of the harness and the proof it was worth building.

## Authority Boundary

Non-negotiable, and specified normatively:

- Sim is authority on: deadlock, unreachable state, sign error, timer scoping, NaN,
  unbounded saturation, event storms, fault-path reachability.
- Rig remains sole authority on: tuning quality, control gains, noise behavior.
- A sim pass MUST NOT check off any `HW:` task.

Rationale: plant model omits filament compliance and motor pull-in limit. Those two
dominate whether a gain transfers to hardware. Sim screens candidates; rig judges
them.

## Impact

- Added capability: `host-sync-simulation`
- Modified capability: `static-regression-validation` — gate builds and runs sim
- New: `tests/host/**`, `scripts/test_sync_sim.py`
- No change: `firmware/src/**`, `firmware/include/**`
