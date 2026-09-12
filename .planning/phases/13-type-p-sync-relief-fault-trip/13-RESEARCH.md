# Phase 13: Type-P Sync Relief & Fault Trip - Research

**Researched:** 2026-09-12
**Domain:** Embedded firmware (RP2040 C) — analog-buffer sync control loop, fault-trip telemetry, host simulation
**Confidence:** HIGH (all load-bearing claims verified by reading current on-disk source this session; no external packages involved)

## Summary

Phase 13 is a firmware-only change confined to `firmware/src/sync.c` (+ small touches in
`sync_buf.c`, `protocol.c`, `protocol_status.c`, `settings_store.c`, `tune.h`/`tune_internal.h`,
`config.ini(.example)`, `MANUAL.md`, and `tests/host/{sim_scenario.c,sim_plant.c}`). All 32
decisions in `13-CONTEXT.md` are already locked; this research verifies the exact current-state
code those decisions modify, and surfaces two load-bearing findings CONTEXT.md's decisions assume
but don't spell out:

1. **The mm-accumulator D-07/D-08 describes already exists.** `g_sync_refill_effort_mm`
   (`firmware/src/sync_buf.c:964`) already accumulates commanded-MMU mm while
   `g_buf.state == BUF_TENSION`, already resets to `0.0f` on every buffer-state transition
   (`sync_buf.c:833`, inside `buf_update()`), and is already exposed in the `ST:` tail as
   `SYNC_REFILL_MM:` (`protocol_status.c:78`) and via `GET:SYNC_REFILL_MM` (`protocol.c:530`).
   It already drives a **warn-only** event at `CONF_SYNC_CANNOT_REFILL_MM` (default **50.0 mm**,
   `tune.h:70`) via `cmd_event("SYNC", "cannot_refill")` (`sync_buf.c:966-969`). Phase 13's new
   `SYNC_TENSION_STOP_MM` (default 32 mm per D-08) is a **second, lower threshold checked against
   this same accumulator**, not a new counter — D-16's "single accumulator shared with the mm
   trip" is this variable. Since 32 < 50, the new hard trip fires and resets the accumulator
   (via the same `sync_fault_hold()` → eventual `sync_rearm_active()` → `buf_update()` reset path)
   **before** the existing 50 mm warn can ever cross under default settings — see Pitfall 1.
2. **The existing ms trip (`SYNC_TENSION_STOP_MS`) is `FLARE_DEV_TUNING`-gated and non-persisted**,
   not a full T1/T2 knob. D-28 requires the **new** `SYNC_TENSION_STOP_MM` and
   `SYNC_PSF_RELIEF_MULT` to be the fuller kind — release-build `SET:`/`GET:`, `config.ini`,
   flash-persisted (TLV tag) — which is a *different, longer* touchpoint list than copying the
   ms trip's existing pattern. See Pitfall 2 and the verified touchpoint template below (modeled
   on `SYNC_KP_RATE`, which *is* persisted).

Everything else in this document is confirmatory: line-cited current source for every integration
point CONTEXT.md's `<code_context>` names, the exact urgent-refill branch to replace, the exact
reset sites for the new arm flag, and the sim/test harness commands to run. No new external
packages are introduced (embedded C firmware); the Package Legitimacy Audit and Runtime State
Inventory sections are marked not-applicable below with the reasoning shown.

**Primary recommendation:** Implement D-01–D-32 exactly as locked in `13-CONTEXT.md`, routing the
new mm trip and probe through the **existing** `g_sync_refill_effort_mm` accumulator rather than
adding a parallel counter, and give `SYNC_TENSION_STOP_MM`/`SYNC_PSF_RELIEF_MULT` the **persisted**
7-touchpoint path (TLV tag + `tune.h` `CONF_*` + `gen_config.py` `DEFAULTS`), explicitly diverging
from the ms trip's `FLARE_DEV_TUNING`-only, non-persisted pattern.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Bounded relief snap (SC#1) | Firmware control loop (`sync.c` urgent-refill branch) | — | Real-time (20 ms tick) motor-rate decision; no host round-trip budget |
| Distance-based fault trip (SC#2) | Firmware control loop (`sync_buf.c` accumulator + `sync.c` trip check) | Firmware telemetry (`protocol_status.c` `ST:` tail) | Trip must fire within one tick of crossing threshold; telemetry is read-only export |
| Feed probe (SC#3) | Firmware control loop (passive observation window) | Firmware telemetry (`PR:` field) + host `(mode × filament_present)` resolution (existing, out of phase scope) | Probe *decision* is firmware-local per D-20; host only *reads* the result |
| Rail-relative thresholds (carried caveat) | Firmware control loop (`g_sync_tension_extreme` tracking, mirrors `g_bl_lock_extreme`) | — | Same reasoning as the `51bdca8` BL fix: a rig-specific calibration constant cannot be hard-coded |
| Sim coverage (SC#4) | Test/host tier (`tests/host/{sim_scenario.c,sim_plant.c}`, `scripts/test_sync_sim.py`) | — | Deterministic pre-rig validation; explicitly ordered before any `HW:` item per D-32 |
| Knob surface (D-28/D-29) | Firmware config tier (T1/T2 persisted vs T3 compiled) | Host tooling (`gen_config.py`, `flare_cmd.py --dump`) | Config-surface-tiers spec governs storage; this is not a host/daemon concern |

No browser/CDN/database tiers exist in this project; FLARE is embedded firmware + a Python host
daemon + Klipper mock. Nothing in this phase touches the daemon (`flare_daemon.py`) or
`klipper/mmu.py` — confirmed by `13-CONTEXT.md`'s `<domain>` "Out of scope: daemon/Klipper/UI
logic (Phases 14–15)".

## Phase Requirements

`.planning/REQUIREMENTS.md` and `.planning/ROADMAP.md` both list Phase 13's requirements as
**TBD**, to be derived from `.planning/research/2026-09-11-happy-hare-borrow-scan.md` §1.1, §1.2,
§1.4. Following the naming convention already used for Phase 14 (`REQ-klipper-status-parity-*`),
this research proposes the following requirement IDs for the planner to register in
`REQUIREMENTS.md` (mirroring how 14-01-PLAN.md "registers the six phase REQ IDs"):

| ID | Description | Research Support |
|----|-------------|------------------|
| `REQ-type-p-sync-relief-bounded-refill` | The urgent-refill branch in the type-P TENSION soft wall SHALL target `min(max_sps, max(est × SYNC_PSF_RELIEF_MULT, baseline_sps))`, never `max_sps` directly, applied through the existing distance-EMA/slew path with a doubled slew cap while pegged. | HH borrow-scan §1.1 (`mmu_sync_controller.py:1170-1182`); verified current code `sync.c:2012-2025` (see Code Examples) |
| `REQ-type-p-sync-relief-distance-trip` | A new `SYNC_TENSION_STOP_MM` SHALL accumulate raw lane feed travel while `BUF_TENSION` and trip `FAULT_HOLD` alongside the existing ms dwell trip, both arming only after the first observed buffer-state transition in the active-sync window. | HH borrow-scan §1.2 (`mmu_sync_controller.py:807-940`); verified current accumulator `sync_buf.c:963-969`, current ms trip `sync.c:1638-1672` |
| `REQ-type-p-sync-relief-feed-probe` | A bounded, passive firmware-local probe SHALL distinguish "home rail, no consumer" from "starved" at type-P +1.0 tension using the same accumulator, evaluated at `PROBE_MM` before the mm trip fires. | HH borrow-scan §4 (`mmu_filament_movement.py:3655-3700`); verified reusable pattern `g_bl_lock_extreme`/`BL_BREAK_DELTA_NORM` in `sync.c:932-964`, `sync_internal.h:33-35` |
| `REQ-type-p-sync-relief-rail-relative-triggers` | Every new threshold (relief snap, mm trip, probe) SHALL be expressed relative to an observed per-window tension extreme, never as an absolute `±0.xx` compare, mirroring the `51bdca8` BL fix. | Carried caveat in `ROADMAP.md` Phase 13; verified precedent `sync.c:921-934,950-965` |
| `REQ-type-p-sync-relief-sim-coverage` | `flare_sim` SHALL cover refill-without-overshoot, mm-trip vs ms-trip ordering, and probe outcomes at both `type_p_rail_scale` 1.0 and 0.7 (plus a 0.5 tripwire), with no regression in the existing type-P scenario set. | HH borrow-scan §8; verified harness `tests/host/sim_scenario.c:576-591`, `scripts/test_sync_sim.py` (see Validation Architecture) |
| `REQ-type-p-sync-relief-no-relay-reintroduction` | Nothing from the type-D relay path (confident estimator, mid-band estimator, EST pivots) SHALL be reintroduced; `HW:` items remain unchecked until rig validation. | Memory `typed-not-portable-to-typep`; `13-CONTEXT.md` D-32 |

## Standard Stack

Not applicable in the conventional sense — this is a firmware-only phase adding no new libraries,
frameworks, or external dependencies. The "stack" is the existing FLARE C codebase and its host
simulation harness, both already in place. There is no `npm install`/`pip install` step for this
phase.

### Core
| Component | Version | Purpose | Why Standard |
|-----------|---------|---------|---------------|
| FLARE firmware C sources | working tree (this repo) | Sync control loop, telemetry, persistence | Existing project code; no substitute |
| `arm-none-eabi-gcc` toolchain | 15.2.1 (Arm GNU Toolchain 15.2.Rel1) `[VERIFIED: arm-none-eabi-gcc --version, this session]` | Cross-compile `firmware/` for RP2040 | Project's existing build target |
| `pico-sdk` | vendored under `build_local/pico-sdk` `[VERIFIED: ls build_local, this session]` | RP2040 HAL | Existing dependency, already fetched |
| Ninja + CMake | Ninja 1.13.2, CMake 3.31.10 `[VERIFIED: ninja --version / cmake --version, this session]` | Build system for both `firmware/` and `tests/host/` | Existing project tooling |
| `flare_sim` host harness | `tests/host/` (this repo) | Links real `sync*.c`/`motion.c`/`toolchange.c` against fakes for deterministic scenario testing | Existing project tooling; the only test surface for SC#4 |

### Supporting
None — no new supporting libraries are needed. Python host scripts used for regression (`scripts/
validate_regression.py`, `scripts/test_sync_sim.py`, `scripts/gen_config.py`) are stdlib-only per
`REQ-cross-platform-script-tooling-all-operat` and already exist.

### Alternatives Considered
Not applicable — no library/vendor choice exists in this phase; every mechanism is a firmware
control-flow change inside code that already exists.

**Installation:** None required.

## Package Legitimacy Audit

**Not applicable.** This phase installs no external packages (no `npm`, `pip`, or `cargo`
dependencies). It is a pure firmware C + existing-host-tooling change. The Package Legitimacy Gate
protocol is skipped per its own scope ("whenever this phase installs external packages").

## Architecture Patterns

### System Architecture Diagram

```
                         g_now_ms tick (main.c:569 order, replicated in sim_main.c)
                                     |
                                     v
                        buf_sensor_tick() [sync_buf.c]
              (reads ADC -> buf_analog_update -> buf_read_stable)
                                     |
                     state changed? -+-> buf_update(new_state, now_ms)   [sync_buf.c:790]
                                     |         |
                                     |         +-> reset g_sync_refill_effort_mm = 0
                                     |         |   reset g_sync_relieve_effort_mm = 0     <-- D-07 reset site
                                     |         +-> sync_on_transition(prev, new, now_ms)  [sync.c:1291]
                                     |               sets/clears g_sync_tension_pin_since_ms
                                     |               (NEW: also (re)sets g_sync_tension_extreme, arm flag)
                                     |
                     do_pos tick -->-+-> if BUF_TENSION: g_sync_refill_effort_mm += delta_mm
                                             (existing accumulator, sync_buf.c:963-969)
                                             (NEW: probe eval at PROBE_MM=16mm; trip check at
                                              SYNC_TENSION_STOP_MM=32mm, both gated on arm flag)
                                     |
                                     v
                          sync_tick() [sync.c:2060]
                                     |
                     sync_tick_calculate_target()  -- raw_target, error_norm, psf_control_law()
                                     |
                     sync_check_tension_dwell_and_ramp()  -- existing ms trip (DEV_TUNING knob)
                     (NEW: mm trip check added in the same block, D-09)
                                     |
                     sync_tick_apply_rate(target_sps, ...)
                                     |
                     +--------------------------------------------------------------+
                     | urgent-refill branch (sync.c:2012-2025) -- REPLACED (D-01/D-02)|
                     |   old: g_sync_current_sps = target_sps  (snaps to soft-wall max)|
                     |   new: relief_target = min(max_sps, max(est*MULT, baseline))    |
                     |        applied via sync_apply_type_p_smoothing() with 2x slew   |
                     +--------------------------------------------------------------+
                                     |
                                     v
                     sync_apply_to_active()  -- writes lane->target_sps, motor_set_rate_sps()
                                     |
                                     v
                          protocol_status.c cmd_handle_status_dump()
                     (NEW: TM:/ARM:/PR: fields appended to extended ST: tail)
```

### Recommended Project Structure
No new files. Touch points (all verified this session):
```
firmware/
├── src/sync.c              # urgent-refill branch (D-01/D-02/D-22), ms+mm trip block (D-09),
│                            #   arm-flag/extreme reset sites (D-10/D-12/D-22), probe hook (D-15/D-16)
├── src/sync_buf.c           # existing g_sync_refill_effort_mm accumulator (D-07 reuses it);
│                            #   buf_update() reset site
├── src/sync_analog.c        # psf_control_law() -- LEFT ALONE this phase (D-24)
├── src/protocol.c           # SET/GET for SYNC_PSF_RELIEF_MULT, SYNC_TENSION_STOP_MM, PROBE: cmd
├── src/protocol_status.c    # ST: tail additions (TM:/ARM:/PR:)
├── src/settings_store.c     # TLV tag + defaults + save/load for the two new persisted knobs
├── include/tune.h           # AUTO-GENERATED -- new CONF_SYNC_PSF_RELIEF_MULT, CONF_SYNC_TENSION_STOP_MM
├── include/tune_internal.h  # PROBE_MM / SOFT_WALL_MARGIN_NORM as T3 constants (D-29)
├── include/settings_store.h # new TAG_* enum entries (append after TAG_FLASH_ERASE_COUNT = 64)
└── include/sync_internal.h  # new BL_BREAK_DELTA_NORM-style constants if needed (D-23 reuses existing)

config.ini / config.ini.example / scripts/gen_config.py   # new DEFAULTS entries (NOT DEPRECATED_KEYS)
MANUAL.md                                                   # new knob rows + PROBE: command doc
tests/host/sim_scenario.c, sim_plant.c                      # new scenarios, type_p_rail_scale twins
scripts/test_sync_sim.py                                    # new test methods
```

### Pattern 1: Distance-based, arm-after-transition trip (existing precedent to extend)
**What:** A counter that accumulates only while a specific buffer state holds, resets on leaving
it, and is gated by an "armed" condition to avoid boot-time / stale-state false trips.
**When to use:** Any new fault-trip threshold in the type-P sync loop (this phase's mm trip and
probe both use this shape).
**Example — the accumulator this phase extends, verified current source:**
```c
// Source: firmware/src/sync_buf.c:963-977 (verbatim, read this session)
if (g_buf.state == BUF_TENSION) {
    g_sync_refill_effort_mm += delta_mm;
    if (!g_sync_cannot_refill_warned &&
        g_sync_refill_effort_mm >= CONF_SYNC_CANNOT_REFILL_MM) {
        g_sync_cannot_refill_warned = true;
        cmd_event("SYNC", "cannot_refill");
    }
} else if (g_buf.state == BUF_COMPRESSION) {
    g_sync_relieve_effort_mm += delta_mm;
    if (!g_sync_cannot_relieve_warned &&
        g_sync_relieve_effort_mm >= CONF_SYNC_CANNOT_RELIEVE_MM) {
        g_sync_cannot_relieve_warned = true;
        cmd_event("SYNC", "cannot_relieve");
    }
}
```
```c
// Source: firmware/src/sync_buf.c:790,833 (verbatim, read this session) -- the reset site
void buf_update(buf_state_t new_state, uint32_t now_ms) {
    if (new_state == g_buf.state)
        return;
    ...
    g_sync_refill_effort_mm = 0.0f;
    g_sync_relieve_effort_mm = 0.0f;
    g_sync_cannot_refill_warned = false;
    g_sync_cannot_relieve_warned = false;
```
D-07's new mm trip and D-15's probe both read `g_sync_refill_effort_mm` at their respective
thresholds (16 mm probe, 32 mm trip) rather than introducing a parallel counter — this is the
"single accumulator shared with the mm trip" D-16 specifies.

### Pattern 2: Deepest-observed-extreme, relative-delta break detection (existing precedent to clone)
**What:** Track the deepest reading seen at a rail since arming; treat "moved
`BL_BREAK_DELTA_NORM` back toward neutral from that extreme" as the break/probe signal, instead of
an absolute `pos <= -0.75`-style compare.
**When to use:** D-22 (rail-relative soft-wall entry), D-23 (probe "moved off rail" test) — this is
the exact fix `51bdca8` applied to `BL:` lock-break detection after the `UNLOAD_TIMEOUT` root
cause (a rig whose rail reads shallower than `-0.75` never engaged the old absolute test).
**Example — verified current source:**
```c
// Source: firmware/src/sync.c:950-965 (verbatim, read this session)
if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
    /* Track the deepest reading at the rail (the EMA keeps settling
       after the prime motor stops), then break once the buffer has
       moved BL_BREAK_DELTA_NORM back toward neutral from it. */
    if (g_bl_target_state == BUF_TENSION) {
        if (g_buf_pos < g_bl_lock_extreme)
            g_bl_lock_extreme = g_buf_pos;
        lock_broken = (g_buf_pos > g_bl_lock_extreme + BL_BREAK_DELTA_NORM);
    } else if (g_bl_target_state == BUF_COMPRESSION) {
        if (g_buf_pos > g_bl_lock_extreme)
            g_bl_lock_extreme = g_buf_pos;
        lock_broken = (g_buf_pos < g_bl_lock_extreme - BL_BREAK_DELTA_NORM);
    }
}
```
```c
// Source: firmware/include/sync_internal.h:33-35, controller_shared.h:15 (verbatim, read this session)
#define PSF_BREAK_THRESHOLD_NORM 0.75f /* break boundary: detect retract pulling toward neutral */
/* Type-P BL break: travel back toward neutral from the deepest reading seen at the
   locked rail before the lock counts as broken. 1.0 - PSF_BREAK_THRESHOLD_NORM keeps the
   historical -0.75 boundary on a rail that reads a full -1.0. */
#define BL_BREAK_DELTA_NORM (1.0f - PSF_BREAK_THRESHOLD_NORM)   /* = 0.25f */
```
D-23 explicitly reuses `BL_BREAK_DELTA_NORM` (0.25) for both "pinned" and "moved off the rail" —
no new magic number. D-22's `g_sync_tension_extreme` is a new variable but the *pattern* (deepen
while active, reset on rearm, compare via delta) is a direct clone of `g_bl_lock_extreme`.

### Pattern 3: The urgent-refill branch this phase replaces
**Example — verified current source (to be replaced per D-01/D-02):**
```c
// Source: firmware/src/sync.c:2009-2025 (verbatim, read this session)
if (fast_brake_active) {
    g_sync_current_sps = 0;
    g_psf_target_filt = 0.0f;
} else if (g_buf_sensor_type == BUF_SENSOR_TYPE_P &&
           buf_pos_norm() < -CONF_PSF_SOFT_WALL_START && target_sps > g_sync_current_sps) {
    /* Urgent refill: the buffer is starved into the TENSION soft-wall zone and
       the distance-EMA below is far too slow to ramp feed before it slams the
       rail (cannot_refill). Feed-up into tension is the safe+urgent direction
       (worst case is a brief overfeed once recovered, which COMPRESSION-side
       smoothing handles), so snap straight to the soft-wall target. Once the
       buffer climbs back out of the wall, the smoothing path resumes. */
    g_sync_current_sps = target_sps;
    /* Seed the smoothing target at DEMAND, not the wall's max_sps: once the
       buffer climbs out of the wall the smoothing resumes from here, so feed
       eases to the extruder rate instead of staying pinned at max and
       overshooting into COMPRESSION. */
    g_psf_target_filt = g_extruder_est_sps;
} else if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
    float dt_s = (float)g_sync_tick_ms / MS_PER_SECOND_F;
    g_sync_current_sps = sync_apply_type_p_smoothing(target_sps, dt_s);
}
```
`buf_pos_norm() < -CONF_PSF_SOFT_WALL_START` is the **absolute** compare D-22 replaces
(`CONF_PSF_SOFT_WALL_START = 0.800f`, `tune.h:134`) — this is the entry condition the carried
caveat's rail-relative fix must change, since a rig whose tension rail reads shallower than
`-0.80` (the `51bdca8` narrative measured `-0.36` cold-extruder, `-0.68` mid-print) may never
cross this literal threshold at all, or may cross it far later than intended.
`g_sync_current_sps = target_sps` is the literal "snap to soft-wall target" D-01 replaces (note:
`target_sps` here is *not* `max_sps` directly — `psf_control_law()`'s soft-wall blend
(`sync_analog.c:51-64`, Pattern 4 below) has already blended it toward `max_sps` as
`abs_pos -> 1.0`; D-24 leaves that blend alone and caps its *output* with the new relief bound).

### Pattern 4: `psf_control_law`'s existing soft-wall blend (left alone, D-24)
**Example — verified current source, confirms D-24's framing:**
```c
// Source: firmware/src/sync_analog.c:48-64 (verbatim, read this session)
/* Layer 2 Soft Walls */
float pos_norm = buf_pos_norm();
float abs_pos = fabsf(pos_norm);
if (abs_pos > CONF_PSF_SOFT_WALL_START) {
    float wall = (abs_pos - CONF_PSF_SOFT_WALL_START) / (1.0f - CONF_PSF_SOFT_WALL_START);
    if (wall > 1.0f) wall = 1.0f;
    if (wall < 0.0f) wall = 0.0f;
    if (pos_norm > 0.0f) {
        target = (int)((float)target * (1.0f - wall));
    } else {
        /* TENSION (-): blend toward max_sps to urge refill */
        target = (int)((float)target + (float)(max_sps - target) * wall);
    }
}
return clamp_i(target, 0, max_sps);
```
This confirms `target_sps` reaching `sync_tick_apply_rate()` already trends toward `max_sps` as
the buffer approaches the tension rail — D-01's relief bound must clamp the *output* of this
function (in the urgent-refill branch), not modify this function itself.

### Anti-Patterns to Avoid
- **Absolute normalized-position compares for any new threshold:** the `51bdca8` root cause
  (`UNLOAD_TIMEOUT` on a shallow-reading rail) is exactly this mistake, made three times in one
  day (`76b4b73`/`e0df203`) before being reverted. D-22/D-23/D-25 exist specifically to prevent
  Phase 13 from repeating it.
- **Adding a second, parallel mm accumulator** instead of reusing `g_sync_refill_effort_mm` —
  would silently duplicate distance integration every tick and risk the two counters drifting
  apart (exactly the failure mode D-12 calls out for the ms/mm arm flags).
- **Copying the ms trip's `FLARE_DEV_TUNING` gating for the new mm trip** — D-28 requires the new
  knobs in the release-build `SET:`/`GET:` surface and flash-persisted; the ms trip's existing
  pattern is the wrong template for these two knobs specifically (see Pitfall 2).
- **Reintroducing type-D relay mechanics** (confident estimator, mid-band estimator, EST pivots)
  under the guise of "shared sync logic" — memory `typed-not-portable-to-typep` and D-32 both
  forbid this; type-P sees demand directly via analog PD, type-D infers it, and the two control
  laws are not interchangeable.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Distance-accumulated "how much have we fed while stuck" tracking | A new `g_tension_relief_mm` counter | Reuse `g_sync_refill_effort_mm` (`sync_buf.c:92,964`) | Already ticks every `do_pos` cycle, already resets on state transition, already has a warn threshold and `ST:`/`GET:` exposure — a parallel counter duplicates all of that and can drift |
| Deepest-observed-rail tracking for a relative delta | A bespoke tension-extreme tracker with new comparison logic | Clone the `g_bl_lock_extreme`/`BL_BREAK_DELTA_NORM` pattern (`sync.c:121,932-964`) | Hardware-validated on this exact rig (`51bdca8`, HW confirmed 2026-09-12); the delta constant (`0.25`) is already the right physical scale for "moved off a rail" |
| Slew-limited feed transitions | A second EMA/slew implementation for the relief path | Route the bounded relief target through the existing `sync_apply_type_p_smoothing()` (`sync.c:1857-1906`) with a doubled `g_sync_psf_slew_per_mm`, per D-02 | The function already handles the distance-clock freeze-on-stopped-extruder case (memory `typep-sync-overfeed-frozen-clock`) correctly; a second path would have to re-derive that logic or risk reintroducing the frozen-clock bug |
| Config knob persistence | Hand-rolled flash read/write for the two new knobs | The existing TLV tag + `settings_defaults()`/`settings_save()`/`settings_load_tlv_tag()`/`settings_apply()` machinery (`settings_store.c`), following the `TAG_SYNC_KP_SPS` template | `scripts/test_settings_parity.py` already enforces tag/default/dump-rebuild parity automatically; a hand-rolled path would fail that gate or silently go untested |

**Key insight:** Every mechanism this phase needs a "new" piece of infrastructure for already has
a working, hardware-validated analogue in the codebase (the mm accumulator, the extreme-tracking
pattern, the distance-EMA smoothing, the TLV persistence path). The actual engineering work is
wiring a *second threshold* and a *rail-relative comparison* into each of these existing
mechanisms, not building new ones.

## Common Pitfalls

### Pitfall 1: The new mm trip and the existing 50 mm warn-only threshold can starve each other
**What goes wrong:** `CONF_SYNC_CANNOT_REFILL_MM` (50.0 mm, `tune.h:70`) drives a **warn-only**
`cmd_event("SYNC", "cannot_refill")` off the exact same accumulator (`g_sync_refill_effort_mm`)
the new `SYNC_TENSION_STOP_MM` (default 32 mm) will trip a **hard** `FAULT_HOLD` from. Since
32 < 50 by default, `FAULT_HOLD` fires and the accumulator resets (via `sync_fault_hold()` →
eventual re-entry into `SYNC_ACTIVE` → `buf_update()`) before `cannot_refill` can ever fire under
default config — the existing warn-only diagnostic silently goes dead for type-P once this phase
ships, unless a lane leaves `BUF_TENSION` between the two thresholds enough times that they
diverge.
**Why it happens:** Both thresholds are unconditionally compared against the same monotonically-
accumulating variable, so whichever is numerically smaller always wins the race deterministically.
**How to avoid:** Flag this reconciliation explicitly in the plan — either (a) note that
`cannot_refill` becoming type-P-practically-unreachable at defaults is an accepted, documented
side effect (it remains reachable if the operator raises `SYNC_TENSION_STOP_MM` above 50, or
disables it with `0`), or (b) leave `CONF_SYNC_CANNOT_REFILL_MM` for type-D only going forward.
Do NOT silently leave this unaddressed — a plan reviewer or later debugger will otherwise be
confused why `cannot_refill` never fires on a type-P rig.
**Warning signs:** `EV:SYNC:cannot_refill` never appears in type-P `flare_sim` traces or rig logs
once `SYNC_TENSION_STOP_MM` is enabled at its default.

### Pitfall 2: The ms trip's existing SET/GET pattern is `FLARE_DEV_TUNING`-gated and non-persisted — the wrong template for the new knobs
**What goes wrong:** `SYNC_TENSION_STOP_MS`/`SYNC_TENSION_RAMP_MS` (the existing ms-based trip
knobs D-09 says the new mm trip runs "the same block" as) are wrapped in `#ifdef
FLARE_DEV_TUNING` in **both** the GET handler (`protocol.c:571-590`) and the SET handler
(`protocol.c:1098-1136`), and have **no TLV tag** in `settings_store.h` — confirmed no
`TAG_SYNC_TENSION*` entry exists in the full 64-tag enum. `MANUAL.md`'s own dev-tuning-gating note
(lines 111-129) lists `SYNC_TENSION_STOP_MS`/`SYNC_TENSION_RAMP_MS` explicitly among the keys "a
normal release build does not expose." If the new `SYNC_TENSION_STOP_MM`/`SYNC_PSF_RELIEF_MULT`
copy this pattern (natural, since they sit in the same code block and file region), they will
ship dev-build-only and non-persisted — directly contradicting D-28's explicit requirement
("Live, flash-persisted, in `config.ini` + `config.ini.example` + SET/GET + `--dump` + MANUAL.md").
**Why it happens:** Proximity bias — the nearest existing SET/GET code for "a sync tension
threshold" is the ms trip's, and copy-pasting its shape is the path of least resistance.
**How to avoid:** Use the **persisted** template instead: `SYNC_KP_RATE`/`g_sync_kp_sps`
(`TAG_SYNC_KP_SPS = 39`, verified full round-trip in `settings_store.c:90,201,440,767-769,951`,
`protocol.c:533-534,1066-1067` — ungated by `FLARE_DEV_TUNING`) or `SYNC_PSF_SLEW_PER_MM`
(live+release-exposed but explicitly "not persisted" per `MANUAL.md:160` — closer to D-28's "live"
half but missing the "flash-persisted" half). D-28's two new knobs need the **full** persisted
path: `gen_config.py` `DEFAULTS` entry → `tune.h` `CONF_*` → `settings_t` struct field + `tlv_emit`
+ TLV load `case` + `settings_apply()` line → `controller_shared.h` extern → release-build (no
`#ifdef`) SET/GET in `protocol.c` → `config.ini`/`config.ini.example` → `MANUAL.md` row.
**Warning signs:** `scripts/test_settings_parity.py`'s "Dump Rebuild Parity" and "TLV Tag Parity"
checks will fail if the new knobs are added only as `#ifdef FLARE_DEV_TUNING` overrides without a
TLV tag — this is a fast, automatic catch if the wrong template is used, but only if a plan step
actually re-runs that parity test.

### Pitfall 3: `STATUS_LINE_MAX` budget for the extended `ST:` tail
**What goes wrong:** `protocol_status.c:16` defines `STATUS_LINE_MAX = CMD_LINE_MAX - 8`, and the
extended tail (`RT:,TT:,CT:,SK:,CF:,ES:,TPX:,CB:,BPV:,MK:,SYNC_REFILL_MM:,SYNC_RELIEVE_MM:,TF:,
FL_RATE:,UL_RATE:`) is already fairly long (verified `protocol_status.c:68-80`). Adding `TM:`,
`ARM:`, and `PR:` (D-11, D-19) grows the line further.
**Why it happens:** `snprintf` into a fixed buffer silently truncates on overflow rather than
erroring; a truncated `ST:` line would drop the last field(s) — likely `UL_RATE:` or the new
fields themselves if appended last — without any compile or runtime error.
**How to avoid:** After adding the new fields, verify the formatted line length against
`STATUS_LINE_MAX` (checkable at plan/build time — not a runtime probe requiring rig access) e.g.
by constructing a worst-case value string and checking length, or by adding a static assertion /
test that exercises the longest expected line and asserts `blen < (int)sizeof(b)` returned true
from `snprintf`.
**Warning signs:** `scripts/flare_cmd.py --dump` or a watch/parse script silently missing the new
`TM:`/`ARM:`/`PR:` fields even though the firmware code that emits them looks correct.

### Pitfall 4: TLV tag number collision
**What goes wrong:** `settings_store.h`'s tag enum currently ends at `TAG_FLASH_ERASE_COUNT = 64`
(verified, full enum read this session). The new tags for `SYNC_PSF_RELIEF_MULT` and
`SYNC_TENSION_STOP_MM` must use `65` and `66` (or whatever is next free at execution time — verify
again just before editing, since other in-flight work may have appended tags) — reusing any
existing tag number silently corrupts a different persisted field on the next flash write.
**Why it happens:** The tag enum has no sparse/gap convention; every value is sequential and
manually assigned.
**How to avoid:** Grep `TAG_` in `settings_store.h` for the current max value immediately before
adding new tags, in the plan's execution step (not just at research time — this file changes as
other phases land).
**Warning signs:** `scripts/test_settings_parity.py`'s "TLV Tag Parity" check catches a missing
serialize/deserialize pairing, but does NOT catch two tags sharing the same numeric value if both
are otherwise correctly wired — a silent data-corruption bug that only manifests after a flash
round-trip on real hardware.

## Code Examples

Verified patterns from the current on-disk source (all read this session, cited with exact
paths/lines):

### Tension-pin/dwell reset sites the new arm flag and `g_sync_tension_extreme` must piggyback on (D-12)
```c
// Source: firmware/src/sync.c — all four sites verified this session
// :78  declaration
uint32_t g_sync_tension_pin_since_ms = 0;
// :1154 sync_disable()
    g_sync_tension_pin_since_ms = 0;
// :1192 sync_rearm_active()
    g_sync_tension_pin_since_ms = was_tension ? now_ms : 0;
// :1297,:1305 sync_on_transition()
    if (now_state == BUF_TENSION) {
        g_sync_tension_pin_since_ms = now_ms;
        ...
    } else if (prev == BUF_TENSION) {
        g_sync_tension_pin_since_ms = 0;
        g_bl_autostart_suppressed = false;
    }
// :1518 sync_tick_auto_start_stop() — AUTO_START-specific restart, see comment there for WHY
    g_sync_tension_pin_since_ms = (g_buf.state == BUF_TENSION) ? now_ms : 0;
```
CONTEXT.md's `<code_context>` cites these as `:1154, :1192, :1297, :1305, :1518` — all five
confirmed present at those exact lines in the current tree.

### Existing ms trip block the new mm trip is added alongside (D-09)
```c
// Source: firmware/src/sync.c:1638-1672 (verbatim, read this session)
static int sync_check_tension_dwell_and_ramp(lane_t *lane, buf_state_t s, int target_sps,
                                             uint32_t now_ms) {
    if (s == BUF_TENSION && g_sync_tension_pin_since_ms != 0) {
        uint32_t tension_dwell_ms = now_ms - g_sync_tension_pin_since_ms;
        if (g_buf_sensor_type != BUF_SENSOR_TYPE_D && g_sync_tension_dwell_stop_ms > 0 &&
            tension_dwell_ms >= (uint32_t)g_sync_tension_dwell_stop_ms) {
            if (sync_try_runout_escalation(lane, now_ms)) {
                return -1;
            }
            sync_fault_hold();
            g_extruder_est_last_update_ms = now_ms;
            sync_apply_to_active();
            cmd_event("SYNC", "FAULT_HOLD");
            return -1;
        }
        if (g_sync_tension_ramp_delay_ms > 0 &&
            tension_dwell_ms >= (uint32_t)g_sync_tension_ramp_delay_ms) {
            int max_sps = sync_clamp_max_sps(g_sync_max_sps);
            if (target_sps < max_sps)
                target_sps = max_sps;
        }
    }
    return target_sps;
}
```
Note the `g_buf_sensor_type != BUF_SENSOR_TYPE_D` guard on the fault-hold branch — this is the
existing "type-D relay TENSION contact is the normal refill signal, not a fault" exclusion D-14
says the new mm trip must carry too. The `g_sync_tension_ramp_delay_ms` ramp-to-`max_sps` branch
(lines 1664-1669) is exactly the path D-04 requires be capped by the same relief bound — currently
it raises `target_sps` all the way to `max_sps` unconditionally once the ramp delay elapses.

### Persisted-knob template to copy for `SYNC_PSF_RELIEF_MULT` / `SYNC_TENSION_STOP_MM` (D-28)
```c
// Source: verified full round-trip for SYNC_KP_RATE / g_sync_kp_sps this session
// firmware/include/settings_store.h:59
TAG_SYNC_KP_SPS = 39,
// firmware/src/settings_store.c:90 (struct field), :201 (default), :440 (emit), :767-769 (load), :951 (apply)
int sync_kp_sps;                                    // struct field
g_sync_kp_sps = CONF_SYNC_KP_SPS;                   // settings_defaults()
tlv_emit_i32(&w, TAG_SYNC_KP_SPS, g_sync_kp_sps);   // settings_save()
case TAG_SYNC_KP_SPS:
    if (len == sizeof(int))
        memcpy(&g_sync_kp_sps, val, sizeof(int));
    break;                                            // settings_load_tlv_tag()
g_sync_kp_sps = s->sync_kp_sps;                      // settings_apply() from v63 struct
// firmware/src/protocol.c:533-534 (GET), :1066-1067 (SET) — NOT inside #ifdef FLARE_DEV_TUNING
else if (!strcmp(param, "SYNC_KP_RATE"))
    snprintf(out, out_len, "SYNC_KP_RATE:%.1f", (double)sps_to_mm_per_min(g_sync_kp_sps));
else if (!strcmp(base_param, "SYNC_KP_RATE"))
    g_sync_kp_sps = clamp_i(mm_per_min_to_sps(fv), 0, MAX_RUN_RATE_SPS);
// scripts/gen_config.py:553 — tune.h generation
f"#define CONF_SYNC_KP_SPS        {mm_min_to_sps(get('sync_kp_rate'), l1)}",
```
The new knobs should follow this exact shape (not the `#ifdef FLARE_DEV_TUNING`-gated
`SYNC_TENSION_STOP_MS` shape shown in Pitfall 2).

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| Type-P urgent refill snaps `g_sync_current_sps = target_sps` directly, bypassing the distance-EMA smoothing entirely | Bounded relief target (`min(max_sps, est*1.33)`) routed through the same smoothing path with a doubled slew cap | This phase (D-01/D-02) | Eliminates the "snap to compression rail, relief-pause, restart" hunting cycle confirmed in `baseline-capture.md` (2.4-3.4s cadence, dozens of cycles in 111s) |
| Fault trip is time-only (`SYNC_TENSION_DWELL_STOP_MS`, changes meaning with print speed) | Distance-based trip (`SYNC_TENSION_STOP_MM`) as the primary trip, ms as slow-flow fallback | This phase (D-08/D-09), following HH v4's FlowGuard (`mmu_sync_controller.py:807-940`) | Trip threshold now scales with actual filament moved, not wall-clock, matching HH's already-shipped mechanism |
| `+1.0` type-P tension resolved only by host-side `(mode × filament_present)` heuristic | Same heuristic PLUS a firmware-local bounded feed probe result (`PR:`) as a third input | This phase (D-15/D-18), following HH's proportional-sensor active probe (`mmu_filament_movement.py:3655-3700`) | Distinguishes "home rail, no consumer" from "starved" without requiring host/Klipper round-trip |
| Absolute normalized-position thresholds (`-0.75`, `-0.80`) for rail-adjacent decisions | Rail-relative deltas off an observed per-window extreme | `51bdca8` (2026-09-12, for `BL:`) — this phase extends the same fix to relief/trip/probe | A rig whose rail reads shallower than the old literal threshold no longer silently disables the feature entirely |

**Deprecated/outdated:** The type-D relay path's confident estimator, mid-band estimator, and EST
pivots (all previously reverted per memories `relay-confident-estimator-bimodal-bangbang`,
`typed-buffer-no-midband-groundtruth`) remain deprecated and are explicitly out of scope — D-32/
SC#5 forbid reintroducing any of them under the guise of "shared sync mechanics."

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|----------------|
| A1 | The exact scenario count behind ROADMAP.md's "16 PSF scenarios" (SC#4) does not map cleanly onto any single grep of `tests/host/sim_scenario.c` or `scripts/test_sync_sim.py` this session — `grep -c "(type-P only)" TEST_CASES.md` finds 11 explicitly labeled entries, `grep -c 'sensor_type="p"'` in `scripts/test_sync_sim.py` finds 20 test-method invocations (some inside loops that also run additional scenarios), and a `TEST_CASES.md` note separately cites "psf-type-p-sensor (16 requirements, 48 scenarios)" for spec-level history, not a current scenario-file count. `[ASSUMED]` — the planner should treat "16" as ROADMAP's own baseline number and verify by running `python3 -m unittest scripts.test_sync_sim -v` before touching any type-P code, recording the pass count, then diffing after Phase 13 changes land, rather than trying to independently re-derive "16" from source. | Validation Architecture / State of the Art | Low — the regression check ("no new failures, same or greater pass count") does not actually depend on knowing the precise historical number; only the before/after diff matters |
| A2 | `SOFT_WALL_MARGIN_NORM`'s exact value is Claude's discretion per CONTEXT.md, within 0.10-0.20 — this research did not narrow that range further than what CONTEXT.md already states. `[ASSUMED]` | Architecture Patterns, Pattern 2 | Low — CONTEXT.md already delegates this to implementation discretion; no plan-blocking risk |
| A3 | The proposed `REQ-type-p-sync-relief-*` requirement IDs in the Phase Requirements section are this research's naming proposal, not yet present in `.planning/REQUIREMENTS.md`. `[ASSUMED]` (naming convention derived from Phase 14's precedent, not a locked decision) | Phase Requirements | Low — the planner can rename freely; the important content is the description-to-evidence mapping, not the exact ID string |

## Open Questions

1. **Exact numeric TLV tag values for the two new knobs**
   - What we know: `settings_store.h`'s enum currently ends at `TAG_FLASH_ERASE_COUNT = 64`
     (verified this session).
   - What's unclear: whether any other in-flight phase/branch appends tags before Phase 13's
     plan executes, which would shift the "next free" number.
   - Recommendation: the plan's execution step (not this research) should re-grep `TAG_` in
     `settings_store.h` immediately before adding the two new tags (Pitfall 4).

2. **Whether `CONF_SYNC_CANNOT_REFILL_MM` (50 mm warn) should change alongside the new 32 mm trip**
   - What we know: the default 32 mm trip fires before the 50 mm warn under default settings
     (Pitfall 1).
   - What's unclear: whether this is an intentional design point (D-08 doesn't mention the
     interaction) or an oversight in the locked decisions.
   - Recommendation: surface this explicitly in the plan and let the plan either document it as
     accepted behavior or add a one-line note in `MANUAL.md`'s `cannot_refill` row clarifying it is
     now effectively type-D-only at default type-P settings.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|--------------|-----------|---------|----------|
| `arm-none-eabi-gcc` | Firmware cross-compile (`ninja -C build_local`) | ✓ `[VERIFIED: arm-none-eabi-gcc --version, this session]` | 15.2.1 (Arm GNU Toolchain 15.2.Rel1) | — |
| `pico-sdk` | Firmware HAL | ✓ `[VERIFIED: ls build_local/pico-sdk, this session]` | vendored in `build_local/` | — |
| Ninja | Build system | ✓ `[VERIFIED: ninja --version, this session]` | 1.13.2 | — |
| CMake | Build system | ✓ `[VERIFIED: cmake --version, this session]` | 3.31.10 | — |
| Python 3 | Host scripts, `flare_sim` test harness | ✓ `[VERIFIED: python3 --version, this session]` | 3.14.5 | — |
| `ruff` | Python lint gate (`scripts/validate_regression.py` step 5) | ✓ `[VERIFIED: ruff --version, this session]` | 0.16.7 | — |
| `build_sim/flare_sim` binary | `scripts/test_sync_sim.py` scenario runner | ✓ pre-built `[VERIFIED: ls build_sim, this session]` | current tree | rebuild via `cmake -S tests/host -B build_sim -G Ninja && ninja -C build_sim` |
| Physical Type-P rig | `HW:` acceptance items (SC#5, D-30/D-31) | Not applicable to this research session (no rig access) | — | Sim-first per D-32; rig validation is the last, human-gated step and out of scope for planning |

**Missing dependencies with no fallback:** None — every build/test dependency needed to plan and
implement the firmware+sim portions of this phase is present and pre-built in this working tree.

**Missing dependencies with fallback:** Physical rig access (needed only for the final `HW:`
acceptance items, D-30) — not needed for planning or for Wave 1 sim-only implementation per D-32's
"sim first, filament last" ordering.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | `tests/host/flare_sim` (custom C harness, real firmware sources linked against fakes) + `scripts/test_sync_sim.py` (Python `unittest` runner/asserter) `[VERIFIED: tests/host/CMakeLists.txt, scripts/test_sync_sim.py, this session]` |
| Config file | `tests/host/CMakeLists.txt` (generates `tune.h` from `config.ini` the same way `firmware/CMakeLists.txt` does, per its own header comment) |
| Quick run command | `build_sim/flare_sim --scenario <NAME> --sensor-type <d\|p>` (single scenario, verified args in `sim_main.c:73-96`) |
| Full suite command | `python3 -m unittest scripts.test_sync_sim -v` (verified invocation pattern in `scripts/validate_regression.py:95-99`) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|---------------------|--------------|
| `REQ-type-p-sync-relief-bounded-refill` | Urgent refill never commands raw `max_sps` while pegged, on any rail scale | sim invariant (new scenario or trace assertion) | `build_sim/flare_sim --scenario sem_psf_relief_bound --sensor-type p` (new) | ❌ Wave 0 — scenario does not exist yet, needs adding to `sim_scenario.c` + `scripts/test_sync_sim.py` |
| `REQ-type-p-sync-relief-distance-trip` | mm trip fires before/instead of ms trip at typical flow; ordering vs probe is structural | sim | new scenario(s) covering mm-trip-vs-ms-trip ordering | ❌ Wave 0 |
| `REQ-type-p-sync-relief-feed-probe` | Probe distinguishes `CONSUMER`/`NO_CONSUMER` correctly | sim | new scenario(s) for both probe outcomes | ❌ Wave 0 |
| `REQ-type-p-sync-relief-rail-relative-triggers` | `EV:SYNC:RELIEF_ON` fires at `type_p_rail_scale` 0.5 (the exact `51bdca8` failure class) | sim | new scenario using `type_p_rail_scale` plant knob (existing knob, `sim_plant.c:178-179`, `sim_scenario.h:145`) | ❌ Wave 0 (knob exists; scenario using it for this purpose does not) |
| `REQ-type-p-sync-relief-sim-coverage` | No regression in existing type-P scenario set | sim | `python3 -m unittest scripts.test_sync_sim -v` (baseline run before changes, diff after) | ✅ harness exists; baseline pass count needs capturing at plan/execution start (see Assumption A1) |

### Sampling Rate
- **Per task commit:** `build_sim/flare_sim --scenario <new-or-touched-scenario> --sensor-type p`
  (single-scenario quick check, seconds)
- **Per wave merge:** `python3 -m unittest scripts.test_sync_sim -v` (full suite)
- **Phase gate:** Full suite green (no new failures, pass count for type-P scenarios not decreased)
  before rig validation (D-30) and before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] New `sem_psf_relief_bound`-style scenario(s) in `tests/host/sim_scenario.c` asserting the
      bounded-relief invariant (D-01/D-04's "no path may command raw `max_sps` while pegged at
      tension" — a single assertable invariant per `13-CONTEXT.md`'s `<specifics>`)
- [ ] New scenario(s) covering mm-trip-vs-ms-trip ordering and both probe outcomes
      (`CONSUMER`/`NO_CONSUMER`)
- [ ] `type_p_rail_scale` 0.5 tripwire scenario asserting `EV:SYNC:RELIEF_ON` still fires
      (D-25) — reuses the existing `bl_retract_immediate`/`type_p_rail_scale` pattern
      (`sim_scenario.c:576-591`, `sim_plant.c:178-179`) but for the relief path, not `BL:`
- [ ] Every new scenario run twice (`type_p_rail_scale` 1.0 and 0.7) per D-25
- [ ] Baseline capture of the current `python3 -m unittest scripts.test_sync_sim -v` pass count
      before any Phase 13 code change, to make the "no regression" comparison concrete (Assumption
      A1)

## Security Domain

### Applicable ASVS Categories

FLARE is embedded motor-control firmware over a single-client serial (USB CDC) link with no
network exposure, no authentication concept at the firmware layer (auth lives in the daemon per
Phase 9's `REQ-daemon-klipper-mirror-*`/security spec, out of scope here), and no user data. Most
ASVS web/app categories do not apply; the relevant subset is input validation on the new `SET:`
surface and the physical-safety implications of a mis-tuned threshold.

| ASVS Category | Applies | Standard Control |
|----------------|---------|--------------------|
| V2 Authentication | No | Firmware has no auth concept; serial link auth (if any) is a daemon-layer concern (Phase 9, out of scope) |
| V3 Session Management | No | No sessions at the firmware layer |
| V4 Access Control | No | Single-client serial device; no multi-principal access model |
| V5 Input Validation | Yes | Existing pattern: every `SET:<PARAM>:<value>` handler clamps the parsed value via `clamp_i`/`clamp_f` before storing (verified pattern throughout `protocol.c`, e.g. `g_sync_tension_dwell_stop_ms = clamp_i(iv, 0, 30000);` at `protocol.c:1115`). The two new knobs (`SYNC_PSF_RELIEF_MULT`, `SYNC_TENSION_STOP_MM`) MUST follow this same clamp-on-set pattern with physically sane bounds (e.g. relief mult clamped so it can never exceed 1.0, per D-01's `min(max_sps, ...)` outer clamp already providing a hard ceiling regardless) |
| V6 Cryptography | No | No cryptographic operations in this phase |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|------------------------|
| Malformed/out-of-range `SET:SYNC_TENSION_STOP_MM:<value>` over serial | Tampering (of runtime config) | Existing `clamp_i`/`clamp_f` pattern in every SET handler; apply identically to the two new knobs |
| A mistuned relief multiplier or trip threshold causing sustained over-torque against a hard mechanical stop | Denial of Service (mechanical, not network) | This is the actual physical-safety concern for this phase — not a classic ASVS category but the domain-appropriate analogue: D-01's `min(max_sps, ...)` outer bound and D-31's HW acceptance pass bar ("no rail hits during print sync") are the mitigations already designed into the locked decisions |
| Flash TLV corruption from a duplicate tag number (Pitfall 4) | Tampering (of persisted config) | `scripts/test_settings_parity.py`'s automated tag-parity check, run before commit per `AGENTS.md`'s "build must pass before every commit" rule |

## Sources

### Primary (HIGH confidence — verified by reading current on-disk source this session)
- `firmware/src/sync.c` (full read of lines 1-160, 900-1140, 1130-1340, 1480-1620, 1600-1760,
  1759-1844, 1840-2039, 2039-2135) — urgent-refill branch, BL lock-extreme pattern, tension-pin
  reset sites, ms dwell trip, `sync_apply_type_p_smoothing`
- `firmware/src/sync_buf.c` (lines 790-980) — `buf_update()`, `g_sync_refill_effort_mm`/
  `g_sync_relieve_effort_mm` accumulation and reset, `cannot_refill`/`cannot_relieve` warn events
- `firmware/src/sync_analog.c` (full file) — `psf_control_law()` soft-wall blend
- `firmware/src/protocol_status.c` (full file) — `ST:` line format and extended tail
- `firmware/src/protocol.c` (lines 555-635, 1090-1170) — SET/GET handlers, `FLARE_DEV_TUNING`
  gating for `SYNC_TENSION_STOP_MS`/`SYNC_TENSION_RAMP_MS`, ungated `SYNC_KP_RATE`/
  `SYNC_PSF_SLEW_PER_MM` templates
- `firmware/src/settings_store.c` (lines 80-100, 420-450, 755-775, 940-960) — TLV struct/emit/
  load/apply round-trip for `TAG_SYNC_KP_SPS`
- `firmware/include/settings_store.h` (full tag enum) — current max tag `TAG_FLASH_ERASE_COUNT = 64`
- `firmware/include/tune.h` (full file, auto-generated) — `CONF_PSF_SOFT_WALL_START`,
  `CONF_SYNC_CANNOT_REFILL_MM`, `CONF_BUF_MAX_TRAVEL_MM`, etc.
- `firmware/include/tune_internal.h` (grep) — `FLARE_INT_SYNC_TENSION_DWELL_STOP_MS`
- `firmware/include/sync_internal.h` (lines 1-60) — `BL_BREAK_DELTA_NORM`, `BL_CATCH_ERR_SPAN_NORM`
- `firmware/include/controller_shared.h` (grep) — `PSF_BREAK_THRESHOLD_NORM`, extern declarations
- `scripts/gen_config.py` (lines 1-130) — `DEPRECATED_KEYS` (T3 demotion list, confirms
  `sync_tension_dwell_stop_ms` is there), `DEFAULTS` dict pattern
- `scripts/test_sync_sim.py` (lines 1-470 grep) — scenario catalogue structure, `sensor_type="p"`
  usage count
- `tests/host/sim_scenario.c` (grep for scenario names, lines 576-591) — `type_p_rail_scale`,
  `bl_retract_immediate`/`bl_retract_paused` twin pattern
- `tests/host/sim_plant.c` (lines 178-179) — `type_p_rail_scale` plant application
- `tests/host/sim_scenario.h` (lines 144-145) — `buf_max_travel_override`, `type_p_rail_scale`
  field comments
- `tests/host/sim_main.c` (lines 65-96) — `flare_sim` CLI args (`--scenario`, `--sensor-type`)
- `MANUAL.md` (lines 100-230) — dev-tuning gating note (explicit list including
  `SYNC_TENSION_STOP_MS`/`SYNC_TENSION_RAMP_MS`), full knob table
- `TEST_CASES.md` (lines 254-340) — `(type-P only)` scenario labels, `psf-type-p-sensor` spec
  history note ("16 requirements, 48 scenarios")
- `AGENTS.md` (lines 95-130) — build/test gate commands, `flare_sim`'s role and authority boundary
- `.planning/specs/config-surface-tiers/spec.md` (full) — T0-T3 tier definitions
- `config.ini`, `config.ini.example` (grep + read of relevant sections) — commented-out
  documentation-only entries for demoted T3 keys
- `scripts/validate_regression.py` (lines 85-105) — the 4-stage validation gate including host sim
- `scripts/test_settings_parity.py` (lines 1-50) — the 4 automated persistence-parity invariants
- `.planning/phases/13-type-p-sync-relief-fault-trip/13-CONTEXT.md` — all 32 locked decisions
- `.planning/phases/13-type-p-sync-relief-fault-trip/baseline-capture.md` — real-print baseline
  (hunting cadence, `UNLOAD_TIMEOUT` root-cause narrative, `51bdca8` HW validation)
- `.planning/research/2026-09-11-happy-hare-borrow-scan.md` §1.1, §1.2, §1.4, §4, §8 — HH `ef8431c`
  mechanics this phase borrows
- `.planning/ROADMAP.md` (Phase 13 section, lines 172-184) — success criteria, carried caveat
- `.planning/STATE.md` — project status, confirms Phase 13 is next after Phase 14 completion
- `.planning/config.json` — confirms `nyquist_validation: true`, no `security_enforcement` override
  (defaults to enabled)

### Secondary (MEDIUM confidence)
- HH `ef8431c` source citations as relayed through `2026-09-11-happy-hare-borrow-scan.md` (this
  research did not re-clone Happy-Hare; it trusts the prior research pass's file:line citations,
  which were themselves verified against a cloned checkout per that document's own header)

### Tertiary (LOW confidence — flagged, see Assumptions Log)
- ROADMAP.md's "16 PSF scenarios" exact count (A1)
- Exact `SOFT_WALL_MARGIN_NORM` numeric value within the 0.10-0.20 discretion range (A2)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new stack; existing toolchain fully present and verified this session
- Architecture: HIGH — every integration point cited with verbatim, line-numbered, this-session
  source reads; the two headline findings (accumulator reuse, DEV_TUNING gating asymmetry) are
  independently verifiable by any future reader via the exact `grep`/`Read` commands shown
- Pitfalls: HIGH — all four pitfalls derived from directly-observed code interactions (accumulator
  race, gating mismatch, status-line budget, tag-numbering), not speculation
- Package legitimacy: N/A — no external packages in this phase

**Research date:** 2026-09-12
**Valid until:** Until `firmware/src/sync.c`, `sync_buf.c`, or `settings_store.h` change again on
`main` (this is an actively-developed control loop; re-verify line numbers before executing the
plan if significant time has passed or other phases have landed commits touching these files)
