## Context

The current tip-forming and unload-toolhead sequence relies on a single blind
MMU retract issued from Klipper:

```gcode
RUN_SHELL_COMMAND CMD=flare PARAMS="MV:-{mmu_tip_retract}:{park_speed*60*0.2}:I"
```

Sized to ~117mm (`dist_sensor_to_extruder + dist_extruder_to_meltzone +
dist_meltzone_to_nozzle_tip`), this asynchronously spans two extruder retracts
that straddle an `M400`:

1. `_FLARE_TIP_FORMING` final park: `G0 E-{park_distance}` ≈ 30mm at
   `park_speed` = **140mm/s**.
2. `_FLARE_UNLOAD_TOOLHEAD` gear clear: `G1 E-{gear_retract}` ≈ 43mm at
   `speed_hub_to_extruder` = **50mm/s**.

It works because `:I` lets the buffer swing fully COMPRESSION↔TENSION without
faulting. It is closed-loop nowhere, hard-codes distances/feedrates in the
macro, and is the last hand-tuned magic number in tip handling.

A prior reactive replacement (`f19f41a sync: add retract assist mode`) was
gutted (now a no-op shell at `firmware/src/sync.c:1283`) because it triggered
at the **compression switch** — by then the buffer was already at the failure
edge, and the soft sync ramp (`SYNC_RAMP_UP_SPS` = 273 sps/tick ≈ 33mm/s²)
could not catch a 140mm/s extruder.

Buffer geometry (from `tune.h`/`config.ini`):

| | mm |
|---|---|
| `BUF_MAX_TRAVEL_MM` | 25 |
| `BUF_SWITCH_SPAN_MM` (switch-to-switch, deadband edges) | 10 |
| half-travel (neutral → one hard end) | 12.5 |
| usable runway (deep tension hard end → deep compression hard end) | ≈ 20 |
| `BUF_HYST_MS` (sensor settling) | 30 |
| `SYNC_TICK_MS` | 20 |

Detection latency from deep tension departure to MMU drive start is therefore
≈ `BUF_HYST_MS + SYNC_TICK_MS` = ~50ms.

## Goals / Non-Goals

**Goals:**
- Replace the blind, hard-coded Klipper retract with a closed-loop firmware
  primitive that pre-charges the full buffer runway and rides the printer-side
  retract without faulting.
- Make the macro-side surface a simple arm/release call — no magic distances
  or feedrates leaking into Klipper.
- Cover **both** retracts (`_FLARE_TIP_FORMING` park, `_FLARE_UNLOAD_TOOLHEAD`
  gear clear) with **two separate** prime → lock → catch cycles — each gets
  a freshly emptied buffer rather than relying on one async cover.
- Restore the gutted reactive infrastructure (instant slam SPS drive, no soft
  PD ramp) as the catch engine, but trigger it from the *armed* edge (tension
  switch departure) rather than the *failed* edge (compression switch reached).
- Document the HW survival envelope so the catch is provably correct against
  the configured `global_max_rate`, or known to require a slower park.

**Non-Goals:**
- Type-P analog buffer support. This change is type-D only. (Type-P sees
  continuous position and dispenses with the runway/slam pattern entirely;
  see the `psf-analog-rig` change.)
- General closed-loop feed-forward of arbitrary extruder moves. `BL` is
  scoped to scripted printer-side retract events the macro can pre-announce.
- Estimator/drift/sigma rework. Lock and catch SHALL preserve those exactly
  as `SYNC_RELIEF_PAUSE` does today.
- Bumping `global_max_rate` itself. This change documents the envelope; the
  actual ceiling change (if needed) is a tuning decision per HW.

## Decisions

### D1 — Command surface: `BL` (Buffer Lock)

`BL:<state>` arms the active lane to drive the buffer to a specific extreme
and hold there. `<state>` ∈ {`T` (tension), `C` (compression)}. Default is
`T` if omitted, since the only current use case is "pre-empty before an
extruder retract." Status reports include `BL:T`, `BL:C`, or `BL:0` for the
disarmed state.

`BS` (buffer stabilize, existing) remains the manual unlock/release. No new
unlock command — keep the surface small.

**Alternatives considered:** extending `MV` with a "drive-to-state-and-hold"
mode (rejected: `MV` is a distance/feedrate primitive, semantics differ);
keeping `RA:1` as the host surface (rejected: `RA` is the gutted "quiet
gate" name and the new behavior must drive the motor — the slot is reused,
but the host-facing command is renamed to `BL` and the `RA` token is
removed outright since no caller uses it).

### D2 — Half-max-travel bounded prime

The prime move is **bounded to `BUF_MAX_TRAVEL_MM / 2`** (12.5mm) regardless
of starting position. If the buffer crosses the target raw switch
(`BUF_TENSION` / `BUF_COMPRESSION`) before reaching the bound, the lane stops
immediately. If the bound is hit first, the lane stops anyway and emits a
soft warning — likely the buffer is already pinned in the opposite direction
or the sensor is wrong.

**Why half-travel:** from any starting position the buffer is at most
half-travel from either extreme; capping the prime move at half-travel
guarantees it terminates either by hitting the target switch or by the
bound, never by an infinite drive into a stuck mechanism.

**Alternatives considered:** drive until switch with no distance cap
(rejected: a stuck switch becomes an unbounded retract / grind); full
max-travel cap (rejected: looser than necessary, easier to mask a real
fault).

### D3 — Lock = stepper holding torque, zero net feed

The "lock" state energizes the MMU motor at the prime endpoint with zero
commanded velocity. This holds the filament against buffer-spring force.
Locked state is monitored on the buffer state machine: any departure from
the target `BUF_*` raw state while locked is treated as a non-MMU external
force = lock-break.

**Alternatives considered:** active feedback to maintain the endstop
(rejected: introduces a control loop and risk of oscillation against the
spring, and we *want* the buffer to leave on external force — that's the
catch trigger).

### D4 — Lock-break trigger = raw buffer departure from target

The lock-break edge is the raw buffer state leaving the target side, not a
debounced edge. Latency budget is tight (see D6); we cannot afford the full
`BUF_HYST_MS` window. The catch engine itself respects hysteresis on its
own internal transitions, but the lock-break is the unfiltered raw edge.

**Alternatives considered:** wait full `BUF_HYST_MS` (rejected: burns
~30ms × V_e = 4.2mm of runway at 140mm/s); host-driven trigger from a
sentinel command (rejected: round-trip latency worse than sensor).

### D5 — Catch = instant slam SPS, asymmetric safety

On lock-break, the active lane is driven in the **mirror direction** at
`min(GLOBAL_MAX_SPS, SYNC_MAX_SPS)` using an instant `current_sps = target`
write (no `SYNC_RAMP_UP_SPS`). This is the exact mechanism the deleted
`retract_assist_drive` used at `f19f41a:firmware/src/sync.c:962`. The
soft PD ramp is bypassed for the catch; effective acceleration is
motor-limited, not firmware-limited.

This is safe by asymmetry: armed at deep tension, an *over-drain* drives
the buffer back toward tension — the direction we were just at, mechanically
recoverable, no grind. The *only* failure mode is the compression slam
the catch is sized to prevent.

**Alternatives considered:** PD ramp at higher gain (rejected: even
8×`SYNC_RAMP_UP_SPS` cannot survive 140mm/s in 10mm runway — see budget);
proportional catch (rejected: same problem — asymmetric safety means
bang-bang is the right shape).

### D6 — Survival envelope (HW limitations)

For arm-to-deep-tension with runway `R = 20mm` (deep tension hard end to
deep compression hard end), constant extruder retract velocity `V_e`, move
distance `D`, instant MMU slam to `V_m`, and detection latency `t_lat`:

```
excursion = V_e · t_lat + max(0, (V_e − V_m)) · (D / V_e − t_lat)
required: excursion ≤ R
ideal (t_lat → 0):  V_m ≥ V_e · (1 − R / D)
realistic (t_lat = 50ms):  V_m ≥ (V_e · D − R · V_e + V_e² · t_lat) / (D − V_e · t_lat)
```

Applied to the two scripted retracts at `V_e` = 150mm/s, `R` = 20mm:

| move | D | V_e | min V_m ideal | min V_m @ 50ms latency |
|---|---|---|---|---|
| park retract (30mm @ 150) | 30 | 150 | 50 mm/s (3000 mm/min) | 67 mm/s (4000 mm/min) |
| gear retract (50mm @ 150) | 50 | 150 | 90 mm/s (5400 mm/min) | 106 mm/s (6360 mm/min) |

At the current configured ceiling `global_max_rate = 4000 mm/min` (66 mm/s)
the 30mm/150mm/s move is right at the margin and the 50mm/150mm/s move
fails by a wide gap. **The 50mm gear retract at 150mm/s is the binding
case.** The actual gear move runs at `speed_hub_to_extruder = 50mm/s` (well
under ceiling), so today's geometry is comfortable; but the proposal's
envelope must be sized against the *worst case* the macro might emit.

**Tuning levers** (any one resolves the worst case):

1. Bump `global_max_rate` to ≥ 6500 mm/min (≈110 mm/s) — covers both moves
   at 150mm/s extruder. Requires hardware that can sustain it.
2. Bound the extruder-side retract feedrate to ≤ ~50 mm/s for the moves
   guarded by `BL:T`. No HW change; macro responsibility.
3. Cut `BUF_HYST_MS` for the locked-state edge specifically (D4) —
   reclaims up to 4–5mm of runway at 140mm/s.

Levers 2 and 3 stack with whatever ceiling we end up with.

### D7 — State home: `SYNC_BUFFER_LOCK` (rename `SYNC_RETRACT_ASSIST`)

The existing `SYNC_RETRACT_ASSIST` enum slot is the right home — it already
exists in the lifecycle, has `RA` host-command wiring, and is currently a
no-op shell. We rename it to `SYNC_BUFFER_LOCK` (or keep the symbol and
redefine semantics; final naming TBD in implementation). The four phases
(prime, locked, catch, settle) are internal sub-states of the lock state,
not separate top-level lifecycle states — they share the gate's
"learning paused, estimator preserved" contract.

**Alternatives considered:** add `SYNC_BUFFER_LOCK` as a 6th lifecycle state
(rejected: the existing slot is already this gate, just gutted; adding a
new one duplicates protocol/status surface).

### D8 — Klipper macro changes

`_FLARE_TIP_FORMING` and `_FLARE_UNLOAD_TOOLHEAD` change as follows:

- Remove `mmu_tip_retract` variable and the `RUN_SHELL_COMMAND CMD=flare
  PARAMS="MV:-{mmu_tip_retract}:{park_speed*60*0.2}:I"` line.
- Before each gated extruder retract, emit `RUN_SHELL_COMMAND CMD=flare
  PARAMS="BL:T"` immediately followed by a fixed settle pause `G4 P1000`
  (~1 second). The pause ensures the half-travel prime has driven the
  buffer to deep tension and the lock has energized before the printer
  retract begins — without it, a fast `G0`/`G1` can fire during the prime
  and the runway is not actually pre-charged.
- The two gated retracts are:
  ```gcode
  ; _FLARE_TIP_FORMING (replaces the blind MV:...:I)
  RUN_SHELL_COMMAND CMD=flare PARAMS="BL:T"
  G4 P1000
  G0 E-{park_distance-dist_to_meltzone_now} F{park_speed*60}

  ; _FLARE_UNLOAD_TOOLHEAD (around the existing gear clear)
  RUN_SHELL_COMMAND CMD=flare PARAMS="BL:T"
  G4 P1000
  G1 E-{gear_retract} F{v.speed_hub_to_extruder*60}
  ```
- After each retract completes (`M400` for ordering), emit
  `RUN_SHELL_COMMAND CMD=flare PARAMS="BS"` to release the lock cleanly,
  or rely on lock-break auto-release once buffer settles (final TBD, see
  Open Questions).

The 1-second pause is a safe upper bound: the prime is capped at
`BUF_MAX_TRAVEL_MM / 2` = 12.5mm at `BUF_STAB_SPS` ≈ 4092 sps ≈ 10mm/s,
so the prime completes in ≤ ~1.25s in the worst case but usually
much faster (buffer is rarely a full half-travel away from `BUF_TENSION`).
If bench data shows the prime consistently completes faster, the pause
can be tightened later; the value lives in the macro, not firmware.

The macro retains no distance/feedrate constants for the MMU side.

## Risks / Trade-offs

- **HW envelope mismatch** → If the hardware cannot deliver the min `V_m`
  in D6 for the worst declared extruder retract, the catch fails (compression
  slam = grind). **Mitigation:** validate ceiling on rig before enabling
  `BL` in the macro path; keep blind `MV:...:I` as a fallback macro branch
  guarded by a flag for the first release.

- **Motor stall on instant slam** → Bypassing `SYNC_RAMP_UP_SPS` means the
  effective accel is motor-limited; TMC may lose steps if asked to jump
  from 0 to >X sps too fast. **Mitigation:** characterize the motor's
  practical accel ceiling on the rig; if needed, replace the
  unconditional slam with a "stepped slam" (e.g., one-tick ramp at a much
  larger step than `SYNC_RAMP_UP_SPS`).

- **Prime move pushes filament backward through hub geometry** → The 12.5mm
  half-travel cap could drag filament past hub/Y-split features. **Mitigation:**
  the prime is a retract that the existing `MV` retract path already supports;
  cap is conservative; emit a warning if the cap is hit (suggests sensor or
  mechanical fault).

- **Lock held indefinitely if extruder never moves** → A misordered macro could
  arm and never trigger lock-break or `BS`. **Mitigation:** add a watchdog
  timeout in the locked state (configurable, default 30s) that auto-releases
  and emits `EV:BL:TIMEOUT`; do not silently fail.

- **Interaction with `MV` fault guards** → The catch is, mechanically, an
  MMU retract while the buffer transits through TENSION. The existing
  `motion.c:436` guard `MMU-retract + TENSION = FAULT:MOVE_TENSION` would
  fire. **Mitigation:** the catch runs as a sync-owned drive, not a `TASK_MOVE`,
  and is exempt from the `MV` task's `move_ignore_buffer` flag check by
  virtue of running on a different task path; the spec delta on `motion-safety`
  must state this explicitly.

- **Concurrent `BL` and active sync** → If `SYNC_ACTIVE` is running when `BL:T`
  arrives, draining to tension during a print is dangerous. **Mitigation:**
  `BL` is only accepted when sync is OFF or in the gate state; otherwise
  reply `ER:BUSY`. Matches existing `BS` semantics.

## Migration / Rollout Plan

1. Land `BL` command + lifecycle + slam catch + spec deltas as one firmware
   change. Keep the macro on the blind `MV:...:I` path.
2. Add a Klipper-side feature flag (`variable_use_buffer_lock: 0` default).
   Operators flip to `1` on benched HW only.
3. Bench: measure MMU sustained top speed and instant-slam accel headroom.
4. If HW clears D6 envelope, flip the macro to `BL`-based path; remove blind
   `MV:...:I` and `mmu_tip_retract` once a release window confirms no
   regressions.
5. Archive the change.

## Open Questions

1. **Release semantics on extruder move completion.** Two options: (a)
   explicit `BS` from the macro after `M400`; (b) firmware auto-detects
   move end (e.g., buffer state stable for N ticks after catch) and releases
   itself. (a) is more deterministic; (b) is more "natural" (matches the
   proposal's framing). Pick during implementation.
2. **Whether to allow `BL:C`** (compression-side arm). No current macro
   uses it, but it's symmetric and may help future forward-extrusion catches.
   Default: implement the protocol but only wire `BL:T` from Klipper for
   now.
3. **Locked-state watchdog timeout value.** 30s is a guess; size against
   the longest realistic delay between `BL` and the gated extruder move.
4. ~~Whether to keep the `RA` protocol symbol.~~ **Resolved:** removed.
   `RA:1` / `RA:0` and the `RA` status field are deleted; `RA` was unused
   by Klipper and any external host, so no alias is needed. The new
   surface is `BL` only.
