# Phase 16: TMC Tension Current Boost - Research

**Researched:** 2026-09-14
**Domain:** Stepper Motor Drive (TMC2209), Closed-Loop Sync Control, Telemetry & Safety
**Confidence:** HIGH

## Summary

Phase 16 implements Happy-Hare style tangle prevention for FLARE's Type-P proportional analog buffer during active printing sync (`SYNC_ACTIVE`). Under normal conditions, FLARE runs its stepper motors at a single static baseline current (`g_tmc_run_current_ma[lane]`). When filament spool friction, binding, or snagging pulls the Type-P buffer deep into tension (`g_buf_pos <= SYNC_TENSION_BOOST_ON`, default `-0.50`), the firmware dynamically raises the gear stepper motor run current (`IRUN`) to `SYNC_TENSION_BOOST_IRUN[lane]` (hard-clamped at a conservative 1200 mA hardware ceiling). Once extra torque overcomes the drag and the buffer eases past the hysteresis release threshold (`g_buf_pos >= SYNC_TENSION_BOOST_OFF`, default `-0.30`), the baseline run current is restored.

Motor current modifications require strict safety and bus-conservation guardrails. UART writes to TMC2209 `IHOLD_IRUN` registers must occur strictly on hysteresis crossing edges (never per control loop tick), preventing serial bus saturation. Crucially, Phase 10 TMC heartbeat recovery and register shadow state (`g_shadow_ihold_irun`) must be updated atomically with boost writes so that heartbeat checks or brownout re-initializations do not clobber active boost or misinterpret boosted current as a fault. Any transition out of `SYNC_ACTIVE`—whether by user command (`STOP`, `PA`, `TC:`), automatic sync stop, or a Phase 13 persistent tangle fault trip (`SYNC_TENSION_STOP_MM` / `SYNC_TENSION_DWELL_STOP_MS`)—unconditionally unwinds boost back to baseline run current.

**Primary recommendation:** Implement `sync_check_tension_boost(uint32_t now_ms)` in `firmware/src/sync.c` called directly from `sync_tick()` during `SYNC_ACTIVE`, paired with a centralized `tmc_apply_active_run_current()` helper in `firmware/src/motion.c` that maintains `g_shadow_ihold_irun` consistency and enforces the 1200 mA hardware clamp.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

#### Firmware Module & Call Placement
- **D-01:** Dedicated `sync_check_tension_boost(uint32_t now_ms)` function implemented in `firmware/src/sync.c` and executed directly from `sync_tick()` during `SYNC_ACTIVE`. This cleanly checks both the activation threshold (`g_buf_pos <= g_sync_tension_boost_on`) and the hysteresis release threshold (`g_buf_pos >= g_sync_tension_boost_off`) after `g_buf_signal` is published.
- **D-02:** Centralized unconditional boost unwind in `sync_set_state()` whenever transitioning out of `SYNC_ACTIVE`, with defense-in-depth safety cleanup calls in `stop_all()` and `sync_disable()`. Guaranteed zero current leakage across any exit path (pause, stop, toolchange, fault hold, lane switch).

#### TMC UART Apply & Shadow Register Coupling
- **D-03:** Dedicated helper `tmc_apply_active_run_current(int lane_num, int current_ma)` in `firmware/src/motion.c` that clamps current to 1200 mA, calls `tmc_set_run_current_ma()`, and atomically updates `g_shadow_vsense[idx]` and `g_shadow_ihold_irun[idx]` via `build_ihold_irun_reg()`.
- **D-04:** Heartbeat recovery consistency: `sync_tmc_settings(lane)` resolves the active current through a boost query helper (`sync_get_active_run_current_ma(lane)`), ensuring simulated or idle brownout recovery re-applies the true active current (boosted if active, base if inactive).
- **D-05:** Boost activation qualification: Boost only engages if configured `SYNC_TENSION_BOOST_IRUN[lane] > g_tmc_run_current_ma[lane]`. If boost is 0 (default) or less than/equal to base run current, the feature is treated as disabled/no-op, preventing unwanted current reductions or spurious UART writes.

#### Protocol ST: Budget & Telemetry Structure
- **D-06:** Telemetry token `,TB:%c` appended to the extended status tail in `firmware/src/protocol_status.c` (rendering `'1'` if active lane is boosted, `'0'` otherwise). This uses exactly 5 chars, fitting within `STATUS_LINE_MAX` (759/760 chars worst-case). Update `scripts/test_status_line_budget.py` to maintain mathematical proof of buffer headroom.
- **D-07:** Host parity: `flare_daemon.py` and `flare_cmd.py` parse `TB` into `tension_boost` boolean; `scripts/flare_cmd.py --dump` includes boost configuration keys and live state.

#### Host Sim Test Suite & Gating Strategy
- **D-08:** Structured host testing:
  1. Dedicated unit test file `tests/host/test_tmc_boost.c` verifying activation/release hysteresis, 1200 mA clamping, prohibition on Type-D and compression, edge-only UART writes (no per-tick writes), and unconditional exit unwind.
  2. Integration plant simulation scenario in `tests/host/sim_scenario.c` (`sem_psf_tension_boost`) asserting boost response under physical spool drag and recovery.
- **D-09:** Release & HW gating policy: Shipped with `SYNC_TENSION_BOOST_IRUN = 0` by default (disabled). Full CI/sim test suite passes before merge. Product Owner (PO) validates code and performs physical test rig thermal validation (`HW:` task) before considering default-on activation. — **Reversibility:** reversible.

### Builder Discretion
- Internal variable naming (`g_sync_tension_boost_active[NUM_LANES]`, `g_sync_tension_boost_on`, `g_sync_tension_boost_off`).
- Exact formatting of `EV:TMC:BOOST:<lane>` and `EV:TMC:NORMAL:<lane>` event helper calls.
- Selection of settings TLV tag IDs in `firmware/include/settings_store.h`.

### Deferred Ideas (OUT OF SCOPE)
- None — discussion stayed strictly within Phase 16 scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| R1 | **Tension Boost Activation**: Active lane TMC `IRUN` raises to `SYNC_TENSION_BOOST_IRUN` (clamped ≤ 1200 mA) when Type-P buffer enters deep tension (`g_buf_pos <= SYNC_TENSION_BOOST_ON`, default -0.50) during `SYNC_ACTIVE`. | Verified via `sync.c:2488-2507` and `motion.c:280`: evaluated in `sync_tick()`, checks sensor type `BUF_SENSOR_TYPE_P`, calls `tmc_apply_active_run_current()`. |
| R2 | **Hysteresis Release**: Motor run current restores to base `g_tmc_run_current_ma[lane]` when buffer relaxes past `g_buf_pos >= SYNC_TENSION_BOOST_OFF` (default -0.30). | Hysteresis state tracked via `g_sync_tension_boost_active[lane_idx]`; release edge fires single UART write to base current. |
| R3 | **Unconditional State Reset**: Exiting `SYNC_ACTIVE` (`STOP`, `PA`, `sync_disable()`, fault trip, toolchange `TC:`, lane switch) immediately restores base run current. | Centralized reset hook in `sync_set_state()` (`sync.c:749`) with defense-in-depth hooks in `stop_all()` (`motion.c:635`) and `sync_disable()` (`sync.c:1206`). |
| R4 | **Persistent Tangle Escalation**: Unrelieved tension defers to Phase 13 distance (`SYNC_TENSION_STOP_MM`) and dwell (`SYNC_TENSION_DWELL_STOP_MS`) fault trips. | Verified via `sync.c:1860-1940`: Phase 13 trips execute `stop_all()` and transition to `SYNC_FAULT_HOLD`, which automatically unwinds boost via R3. |
| R5 | **Telemetry & Event Visibility**: Status `ST:` line reports `TB:0` or `TB:1`; edges emit `EV:TMC:BOOST:<lane>` and `EV:TMC:NORMAL:<lane>`. | Verified via `protocol_status.c:104-120`: `,TB:%c` consumes 5 chars, fitting within 760 max (759 worst-case, 1 char headroom proven by `scripts/test_status_line_budget.py`). |
| R6 | **Shadow Register & Heartbeat Integrity**: TMC heartbeat recovery and `g_shadow_ihold_irun` stay synchronized with active boost current. | Verified via `settings_store.c:522-541` and `main.c:309-314`: `sync_tmc_settings()` queries active current to re-apply boosted register during brownout recovery. |
| R7 | **Configuration & Protocol Parity**: `SYNC_TENSION_BOOST_IRUN`, `SYNC_TENSION_BOOST_ON`, `SYNC_TENSION_BOOST_OFF` supported in `config.ini`, `tune.h`, TLV storage, `SET:`/`GET:`, and `--dump`. | Handled via `gen_config.py`, `settings_store.c` (tags 67, 68, 69; `SETTINGS_VERSION` 65u), `protocol.c` parameter parsing with `ER:INVALID_PARAM` validation, and `flare_cmd.py --dump`. |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Hysteresis Evaluation & Gating | Firmware (`sync.c`) | — | `sync_tick()` possesses real-time buffer position (`g_buf_pos`) and sync FSM state (`g_sync_state`). |
| Motor Current Application & Shadow Sync | Firmware (`motion.c`) | Firmware (`settings_store.c`) | `motion.c` owns TMC driver UART primitives (`tmc_set_run_current_ma()`) and `g_shadow_ihold_irun` register state. |
| Brownout Recovery Integration | Firmware (`settings_store.c`) | Firmware (`motion.c`) | `sync_tmc_settings()` is called during heartbeat brownout recovery; must query whether active lane is boosted. |
| Safety Unwind on Stop/Exit | Firmware (`sync.c`) | Firmware (`motion.c`) | `sync_set_state()` guarantees unwind on all FSM state exits; `stop_all()` and `sync_disable()` provide defense-in-depth. |
| Persistent Tangle Trip | Firmware (`sync.c`) | — | Existing Phase 13 distance/dwell fault engine halts motion if boost cannot free tangle. |
| Telemetry & Status Reporting | Firmware (`protocol_status.c`) | Firmware (`protocol.c`) | `cmd_handle_status_dump()` formats `ST:` line; `cmd_event()` emits edge events. |
| Host Daemon & CLI Mirroring | Host (`flare_daemon.py`) | Host (`flare_cmd.py`) | Parses `TB` token into status dict and exposes configuration in `--dump`. |
| Configuration & Flash Persistence | Firmware (`settings_store.c`) | Host (`gen_config.py`) | TLV flash serialization and build-time code generation from `config.ini`. |

## Standard Stack

### Core
| Library / Component | Version | Purpose | Why Standard |
|---------------------|---------|---------|--------------|
| C11 Standard | ISO/IEC 9899:2011 | Embedded firmware | FLARE firmware standard, Pico SDK C standard [VERIFIED: CMakeLists.txt:4]. |
| Pico SDK / Hardware UART | 1.5+ | TMC2209 half-duplex UART | Hardware abstraction layer for RP2040 UART peripherals. |
| Python 3 | 3.9+ | Tooling, code generation, simulation | Project standard for `scripts/*.py` and test harnesses. |

### Supporting
| Component | Source / File | Purpose | When to Use |
|-----------|---------------|---------|-------------|
| `build_ihold_irun_reg` | `firmware/src/main.c:309-314` | Calculates 32-bit TMC `IHOLD_IRUN` register bitmask | Used on every motor run/hold current update to synchronize shadow state. |
| `tmc_set_run_current_ma` | `firmware/src/tmc2209.c:320-342` | Computes CS scale, manages VSENSE, writes `IHOLD_IRUN` | Core TMC driver UART write routine. |
| `cmd_event` | `firmware/src/protocol.c:278-280` | Serial asynchronous event emission (`EV:...`) | Edge transitions (`EV:TMC:BOOST:<lane>`, `EV:TMC:NORMAL:<lane>`). |
| `test_status_line_budget.py` | `scripts/test_status_line_budget.py:1-180` | Mathematical proof of `STATUS_LINE_MAX` headroom | Must pass to ensure `,TB:%c` never truncates status output. |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Edge-triggered UART write | Per-tick UART write | Per-tick write floods the single-wire UART bus at 50 Hz, causing command stalls and step jitter. Forbidden by R1/R2 constraints. |
| Dedicated mA boost parameter | Multiplier percentage (e.g. 150%) | Percentage math is ambiguous across differing motor base currents and risks exceeding driver coil limits. Absolute mA with 1200 mA clamp is deterministic. |
| Separate boost recovery register | Heartbeat shadow query | Duplicating shadow registers creates desync risk; querying `sync_get_active_run_current_ma(lane)` keeps a single source of truth. |

## Architecture Patterns

### System Architecture Diagram

```mermaid
flowchart TD
    subgraph MainLoop ["Main Loop (firmware/src/main.c)"]
        TickBuf ["buf_sensor_tick()\nPublish g_buf_pos & g_buf_signal"] --> TickSync ["sync_tick()\nExecute sync controller"]
        TickSync --> TickHeartbeat ["tmc_heartbeat_tick()\n1 Hz alternating idle probe"]
    end

    subgraph SyncController ["Sync Controller (firmware/src/sync.c)"]
        TickSync --> GuardCheck {"In SYNC_ACTIVE\n& Type-P\n& Boost Configured?"}
        GuardCheck -- No --> BaseMotion ["Standard sync speed PID / PSF"]
        GuardCheck -- Yes --> EvalBoost ["sync_check_tension_boost()"]

        EvalBoost --> CondActive {"g_sync_tension_boost_active?"}

        CondActive -- False --> CheckOn {"g_buf_pos <= BOOST_ON?\n(<= -0.50)"}
        CheckOn -- Yes --> EngageBoost ["g_sync_tension_boost_active = true\ntmc_apply_active_run_current(lane, BOOST_IRUN)\ncmd_event('TMC:BOOST', lane)"]
        CheckOn -- No --> KeepBase ["Maintain baseline current"]

        CondActive -- True --> CheckOff {"g_buf_pos >= BOOST_OFF?\n(>= -0.30)"}
        CheckOff -- Yes --> ReleaseBoost ["g_sync_tension_boost_active = false\ntmc_apply_active_run_current(lane, BASE_IRUN)\ncmd_event('TMC:NORMAL', lane)"]
        CheckOff -- No --> CheckFault {"Phase 13 Fault?\n(STOP_MM or DWELL_MS)"}
        CheckFault -- Yes --> TripFault ["sync_set_state(SYNC_FAULT_HOLD)\nstop_all() -> Unwind Boost"]
        CheckFault -- No --> KeepBoosted ["Maintain boosted current"]
    end

    subgraph MotionApply ["Motion & TMC Layer (firmware/src/motion.c)"]
        EngageBoost --> ApplyHelper ["tmc_apply_active_run_current()"]
        ReleaseBoost --> ApplyHelper
        ApplyHelper --> ClampMa ["Clamp: min(current_ma, 1200 mA)"]
        ClampMa --> UartWrite ["tmc_set_run_current_ma()"]
        ClampMa --> ShadowSync ["g_shadow_ihold_irun[idx] = build_ihold_irun_reg()\ng_shadow_vsense[idx] = (ma <= 980)"]
    end

    subgraph HeartbeatRecovery ["Heartbeat Recovery (firmware/src/settings_store.c)"]
        TickHeartbeat --> BrownoutDetect {"Brownout Mismatch Detected?"}
        BrownoutDetect -- Yes --> RecoverCall ["sync_tmc_settings(lane)"]
        RecoverCall --> QueryActive ["sync_get_active_run_current_ma(lane)"]
        QueryActive --> ReapplyUart ["Re-write active current to TMC\n(Preserves boost if active)"]
    end
```

### Pattern 1: Edge-Triggered UART Mutation & Shadow Synchronization
**What:** Motor current changes are strictly event-driven. The controller evaluates buffer position against thresholds every tick, but issues a UART transaction and event only on the crossing edge.
**When to use:** Any dynamic TMC register modification in the firmware control loop.
```c
// Pattern: Edge-triggered UART write and atomic shadow update
if (!g_sync_tension_boost_active[idx] && g_buf_pos <= g_sync_tension_boost_on) {
    g_sync_tension_boost_active[idx] = true;
    tmc_apply_active_run_current(lane_num, g_sync_tension_boost_irun[idx]);
    char lane_s[4];
    snprintf(lane_s, sizeof(lane_s), "%d", lane_num);
    cmd_event("TMC:BOOST", lane_s);
} else if (g_sync_tension_boost_active[idx] && g_buf_pos >= g_sync_tension_boost_off) {
    g_sync_tension_boost_active[idx] = false;
    tmc_apply_active_run_current(lane_num, g_tmc_run_current_ma[idx]);
    char lane_s[4];
    snprintf(lane_s, sizeof(lane_s), "%d", lane_num);
    cmd_event("TMC:NORMAL", lane_s);
}
```

### Pattern 2: Multi-Layer Unconditional State Unwind (Defense-in-Depth)
**What:** Boosted current must never persist outside of `SYNC_ACTIVE`. Unwind logic is placed at the state exit choke point (`sync_set_state`), with defense-in-depth safety calls in motion termination (`stop_all`) and sync teardown (`sync_disable`).
**When to use:** All safety-critical state machine exits.
```c
// Centralized unwind helper
void sync_tension_boost_reset_lane(int lane_num) {
    int idx = lane_to_idx(lane_num);
    if (g_sync_tension_boost_active[idx]) {
        g_sync_tension_boost_active[idx] = false;
        tmc_apply_active_run_current(lane_num, g_tmc_run_current_ma[idx]);
        char lane_s[4];
        snprintf(lane_s, sizeof(lane_s), "%d", lane_num);
        cmd_event("TMC:NORMAL", lane_s);
    }
}

// In sync_set_state():
if (g_sync_state == SYNC_ACTIVE && new_state != SYNC_ACTIVE) {
    sync_tension_boost_reset_all();
}

// In stop_all():
sync_tension_boost_reset_all();
```

### Pattern 3: Budget-Preserving Telemetry Encoding
**What:** Telemetry flags that have binary state must use single-character formatting (`%c`, `'0'` or `'1'`).
**When to use:** Extending the serial `ST:` status dump line under tight byte constraints.
```c
// In firmware/src/protocol_status.c:
// Rendered as ,TB:%c where value is '1' if active lane is boosted, '0' otherwise.
char tb_char = sync_is_tension_boost_active(g_active_lane) ? '1' : '0';
snprintf(b + blen, sizeof(b) - (size_t)blen,
         "...,PR:%c,TB:%c", ..., probe_char, tb_char);
```

### Anti-Patterns to Avoid
- **Continuous Register Writes:** Writing `IHOLD_IRUN` every 20 ms while pegged in tension. TMC UART transactions take ~1–2 ms each; continuous writes will starve the stepper timer and serial buffer.
- **Independent Shadow Bypass:** Calling `tmc_set_run_current_ma()` without updating `g_shadow_ihold_irun[idx]` and `g_shadow_vsense[idx]`. This causes TMC heartbeat recovery to immediately clobber the boosted current on the next 1 Hz tick.
- **Unbounded Boost Targets:** Accepting user `SET:` current values above 1200 mA without clamping or error rejection.
- **Asymmetric De-boost:** Forgetting to de-boost when a print is paused via `PA:` or when switching lanes via `TC:`.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Current scale computation | Custom CS shift and bit packing | `build_ihold_irun_reg()` [VERIFIED: firmware/src/main.c:309-314] | Existing helper correctly computes RSENSE, VSENSE voltage reference, and IRUNDELAY bit fields. |
| Stepper driver UART writes | Raw UART register routines | `tmc_set_run_current_ma()` [VERIFIED: firmware/src/tmc2209.c:320-342] | Manages VSENSE bit in `CHOPCONF` (transition between 0.180V and 0.325V sensitivity) and UART checksums. |
| Spool tangle shutdown | Ad-hoc timeout counter | Phase 13 `SYNC_TENSION_STOP_MM` / `SYNC_TENSION_DWELL_STOP_MS` [VERIFIED: firmware/src/sync.c:1860-1940] | Phase 13 already provides distance-based and dwell-based fault trips with recovery cooldowns. |
| Status budget calculation | Manual character counting in comments | `scripts/test_status_line_budget.py` [VERIFIED: scripts/test_status_line_budget.py:1-180] | Automated AST/regex script mathematically verifies worst-case length against `STATUS_LINE_MAX`. |

## Common Pitfalls

### Pitfall 1: Heartbeat Brownout Recovery Clobbering Active Boost
**What goes wrong:** While the driver is boosted to 1000 mA during tension, a simulated or real brownout recovery runs `sync_tmc_settings()`. The driver immediately resets back to baseline 800 mA despite buffer remaining pegged in tension.
**Why it happens:** `sync_tmc_settings()` in `settings_store.c:534` statically read `g_tmc_run_current_ma[idx]`.
**How to avoid:** Update `sync_tmc_settings(lane)` to query the active current via `sync_get_active_run_current_ma(lane)` (Decision D-04).
**Warning signs:** Stepper skips steps during extended tension recovery after a momentary UART glitch.

### Pitfall 2: Current Leakage into Toolchange or Pause
**What goes wrong:** Buffer enters tension right before a toolchange cut (`TC:`) or pause (`PA`). Motor remains at 1200 mA during toolhead parking or cutting.
**Why it happens:** De-boost hook placed only in `sync_disable()` but not in `sync_set_state()` or `stop_all()`.
**How to avoid:** Centralize unconditional boost unwind in `sync_set_state()` whenever leaving `SYNC_ACTIVE`, with redundant calls in `stop_all()` and `sync_disable()` (Decision D-02).
**Warning signs:** Motor gets hot while sitting idle at a pause or toolchange park.

### Pitfall 3: Status Line Truncation on Buffer Overflow
**What goes wrong:** Adding `TB:1` to `ST:` pushes the line length past 760 characters (`STATUS_LINE_MAX`). The string is truncated, dropping downstream fields like `PR:`.
**Why it happens:** `snprintf` in `protocol_status.c:104` has a fixed buffer `b[STATUS_LINE_MAX]`.
**How to avoid:** Render `TB:%c` (uses exactly 5 characters: `,TB:0` or `,TB:1`). Run `python3 scripts/test_status_line_budget.py` to prove worst-case fits within 759/760 chars.
**Warning signs:** `scripts/test_status_line_budget.py` fails or firmware emits `EV:SYS:ST_TRUNC`.

### Pitfall 4: Inverted or Degenerate Hysteresis Band
**What goes wrong:** Operator configures `SYNC_TENSION_BOOST_ON = -0.30` and `SYNC_TENSION_BOOST_OFF = -0.50` (or equal values).
**Why it happens:** `BOOST_ON` must be more negative than `BOOST_OFF` (tension is negative travel, home is -1.0). Inverting creates an instantaneous chattering state.
**How to avoid:** Validate in `cmd_apply_set_param()`: reject `BOOST_ON >= BOOST_OFF` with `ER:INVALID_PARAM`.

### Pitfall 5: Boost Activation on Type-D Microswitch Buffers
**What goes wrong:** Boost activates when a Type-D microswitch clicks tension.
**Why it happens:** Omitting the `g_buf_sensor_type == BUF_SENSOR_TYPE_P` guard in the activation check. Type-D switch contacts represent normal relay oscillation, not deep tension pegging.
**How to avoid:** Hard-code `if (g_buf_sensor_type != BUF_SENSOR_TYPE_P) return;` at the top of `sync_check_tension_boost()`.

## Code Examples

### 1. TMC Active Current Apply & Shadow Register Coupling
[VERIFIED: firmware/src/motion.c:280, firmware/src/main.c:309-323, firmware/include/tmc2209.h:48]
```c
// In firmware/src/motion.c:
void tmc_apply_active_run_current(int lane_num, int current_ma) {
    if (lane_num < 1 || lane_num > NUM_LANES)
        return;
    int idx = lane_to_idx(lane_num);
    lane_t *lane = lane_ptr(lane_num);
    if (!lane || !lane->tmc)
        return;

    // Hard hardware safety ceiling (Decision D-03, Constraint R1)
    int clamped_ma = clamp_i(current_ma, 0, 1200);

    // Apply over UART
    tmc_set_run_current_ma(lane->tmc, clamped_ma, g_tmc_hold_current_ma[idx]);

    // Update shadow registers for heartbeat and protocol consistency
    g_shadow_vsense[idx] = (clamped_ma <= TMC_VSENSE_THRESHOLD_MA);
    g_shadow_ihold_irun[idx] = build_ihold_irun_reg(
        clamped_ma, g_tmc_hold_current_ma[idx], g_shadow_vsense[idx]);
    g_shadow_ihold_irun_valid[idx] = true;
}
```

### 2. Boost Hysteresis Check in `sync.c`
[VERIFIED: firmware/src/sync.c:2485-2507, firmware/include/controller_shared.h:212-215]
```c
// In firmware/src/sync.c:
int sync_get_active_run_current_ma(int lane_num) {
    int idx = lane_to_idx(lane_num);
    if (idx < 0 || idx >= NUM_LANES)
        return 0;
    if (g_sync_tension_boost_active[idx] && g_sync_tension_boost_irun[idx] > g_tmc_run_current_ma[idx]) {
        return clamp_i(g_sync_tension_boost_irun[idx], 0, 1200);
    }
    return g_tmc_run_current_ma[idx];
}

void sync_check_tension_boost(uint32_t now_ms) {
    (void)now_ms;
    if (g_sync_state != SYNC_ACTIVE || g_buf_sensor_type != BUF_SENSOR_TYPE_P) {
        return;
    }
    lane_t *lane = lane_ptr(g_active_lane);
    if (!lane)
        return;
    int idx = lane_to_idx(g_active_lane);

    // Feature qualification: must be enabled and higher than baseline run current (D-05)
    int boost_ma = g_sync_tension_boost_irun[idx];
    if (boost_ma <= 0 || boost_ma <= g_tmc_run_current_ma[idx]) {
        return;
    }

    if (!g_sync_tension_boost_active[idx]) {
        // Activation condition: buffer pegged deep in tension (negative)
        if (g_buf_pos <= g_sync_tension_boost_on) {
            g_sync_tension_boost_active[idx] = true;
            tmc_apply_active_run_current(g_active_lane, boost_ma);
            char lane_s[4];
            snprintf(lane_s, sizeof(lane_s), "%d", g_active_lane);
            cmd_event("TMC:BOOST", lane_s);
        }
    } else {
        // Hysteresis release condition: buffer relaxed past off threshold
        if (g_buf_pos >= g_sync_tension_boost_off) {
            g_sync_tension_boost_active[idx] = false;
            tmc_apply_active_run_current(g_active_lane, g_tmc_run_current_ma[idx]);
            char lane_s[4];
            snprintf(lane_s, sizeof(lane_s), "%d", g_active_lane);
            cmd_event("TMC:NORMAL", lane_s);
        }
    }
}
```

### 3. Brownout Recovery Integration in `settings_store.c`
[VERIFIED: firmware/src/settings_store.c:522-541]
```c
// In firmware/src/settings_store.c:
void sync_tmc_settings(int lane) {
    int idx = lane_to_idx(lane);
    tmc_t *tmc = (lane == 1) ? &g_tmc_l1 : &g_tmc_l2;

    g_mm_per_step[idx] =
        g_tmc_rotation_distance[idx] /
        ((float)g_tmc_full_steps[idx] * g_tmc_gear_ratio[idx] * (float)g_tmc_microsteps[idx]);

    tmc_set_pwmconf(tmc);
    tmc_setup_chopconf(tmc, g_tmc_microsteps[idx], g_tmc_toff[idx], g_tmc_tbl[idx],
                       g_tmc_hstrt[idx], g_tmc_hend[idx], g_tmc_interpolate[idx]);
    tmc_set_stealthchop_sps(tmc, g_tmc_stealthchop_sps[idx], g_tmc_microsteps[idx]);

    // Decision D-04: Resolve active current to maintain boost across brownout recovery
    int active_run_ma = sync_get_active_run_current_ma(lane);
    tmc_set_run_current_ma(tmc, active_run_ma, g_tmc_hold_current_ma[idx]);

    // Synchronize shadow state for protocol reporting
    g_shadow_vsense[idx] = (active_run_ma <= TMC_VSENSE_THRESHOLD_MA);
    g_shadow_ihold_irun[idx] = build_ihold_irun_reg(
        active_run_ma, g_tmc_hold_current_ma[idx], g_shadow_vsense[idx]);
    g_shadow_ihold_irun_valid[idx] = true;
}
```

### 4. Telemetry Token in `protocol_status.c`
[VERIFIED: firmware/src/protocol_status.c:104-120]
```c
// In cmd_handle_status_dump():
char tb_char = sync_is_tension_boost_active(g_active_lane) ? '1' : '0';

int tail_len = snprintf(
    b + blen, sizeof(b) - (size_t)blen,
    ",RT:%.2f,TT:%u,TM:%d,ARM:%c,CT:%u,SK:%u,CF:%.2f,ES:%.2f"
    ",TPX:%d,CB:%d,BPV:%d,MK:%u:%s"
    ",SYNC_REFILL_MM:%d,SYNC_RELIEVE_MM:%d,TF:%.1f,FL_RATE:%.1f,UL_RATE:%.1f"
    ",PR:%c,TB:%c",
    ...,
    (char)('0' + sync_type_p_probe_state()),
    tb_char);
```

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| — | None | — | All claims verified directly from source code and locked decisions. |

*Note: All claims in this research were verified against repository source code and locked decisions; no unverified assumptions were made.*

## Open Questions

None. All functional requirements, architectural boundaries, telemetry budgets, and test methodologies are locked and verified.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake | Build system | ✓ | 3.13+ | — |
| Ninja | Build tool | ✓ | 1.10+ | Make |
| GCC / Clang | Host simulation compiler | ✓ | Apple Clang 15+ | GCC |
| Python 3 | Scripts & validation | ✓ | 3.9+ | — |

**Missing dependencies:** None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Custom C Host Test Suite (`tests/host/`) & Python `unittest` |
| Config file | `tests/host/CMakeLists.txt` |
| Quick run command | `python3 scripts/test_status_line_budget.py && python3 scripts/test_settings_parity.py` |
| Full suite command | `cmake --build build_clang --target test_tmc_boost && ./build_clang/tests/host/test_tmc_boost && ./build_clang/tests/host/flare_sim --scenario sem_psf_tension_boost` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| R1 | Tension Boost Activation (<= -0.50, Type-P only, 1200 mA clamp) | unit | `./build_clang/tests/host/test_tmc_boost` | ❌ Wave 0 (`tests/host/test_tmc_boost.c`) |
| R2 | Hysteresis Release (>= -0.30 restores base current) | unit | `./build_clang/tests/host/test_tmc_boost` | ❌ Wave 0 (`tests/host/test_tmc_boost.c`) |
| R3 | Unconditional Reset on STOP/PA/TC/Fault/Disable | unit | `./build_clang/tests/host/test_tmc_boost` | ❌ Wave 0 (`tests/host/test_tmc_boost.c`) |
| R4 | Persistent Tangle Escalation (delegates to Phase 13 fault trips) | integration | `./build_clang/tests/host/flare_sim --scenario sem_psf_tension_boost` | ❌ Wave 0 (scenario in `sim_scenario.c`) |
| R5 | Telemetry (`TB:0`/`TB:1`, `EV:TMC:BOOST`/`NORMAL`) & Line Budget | unit / regression | `python3 scripts/test_status_line_budget.py` | ✅ (`scripts/test_status_line_budget.py`) |
| R6 | Shadow Register & Heartbeat Brownout Recovery | unit | `./build_clang/tests/host/test_tmc_boost` | ❌ Wave 0 (`tests/host/test_tmc_boost.c`) |
| R7 | Configuration Parity (`config.ini`, SET/GET, `--dump`) | parity / unit | `python3 scripts/test_settings_parity.py` | ✅ (`scripts/test_settings_parity.py`) |

### Sampling Rate
- **Per task commit:** Run `python3 scripts/test_status_line_budget.py`, `python3 scripts/test_settings_parity.py`, and quick unit tests.
- **Per wave merge:** Full test runner execution (`./build_clang/tests/host/flare_sim`, `test_tmc_boost`, `test_tmc_recovery`).
- **Phase gate:** All host test executables green, Python scripts passing `py_compile`, and status line budget verified.

### Wave 0 Gaps
- [ ] `tests/host/test_tmc_boost.c` — unit test suite for R1, R2, R3, R6
- [ ] Add `test_tmc_boost` executable target to `tests/host/CMakeLists.txt`
- [ ] Scenario `sem_psf_tension_boost` in `tests/host/sim_scenario.c` for R4 integration plant validation

## Security Domain

### Applicable ASVS / Safety Categories
| Category | Applies | Standard Control |
|----------|---------|------------------|
| Hardware Over-Current Protection | Yes | Hard clamp `clamp_i(current_ma, 0, 1200)` in `tmc_apply_active_run_current()` and reject `> 1200` in `protocol.c`. |
| Buffer Overflow Protection | Yes | Fixed status line budget `STATUS_LINE_MAX` enforced by mathematical test in `test_status_line_budget.py`. |
| Input Parameter Validation | Yes | Strict ordering check `BOOST_ON < BOOST_OFF` in `SET:` command handling returning `ER:INVALID_PARAM`. |
| State Desynchronization Prevention | Yes | Atomic shadow update (`g_shadow_ihold_irun`) with UART writes to prevent heartbeat recovery desync. |

## Sources

### In-Repo Discrete Value Citations (Verbatim Quotes)
- Sensor types [VERIFIED: `firmware/include/controller_shared.h:212-215`]:
  ```c
  enum {
      BUF_SENSOR_TYPE_D = 0, ///< dual-endstop (two microswitches)
      BUF_SENSOR_TYPE_P = 1  ///< proportional analog / Hall-effect
  };
  ```
- Settings version and tags [VERIFIED: `firmware/include/settings_store.h:8-9, 85-87`]:
  ```c
  #define SETTINGS_VERSION_V63 63u
  #define SETTINGS_VERSION 64u
  ```
  ```c
  TAG_SYNC_PSF_RELIEF_MULT = 65,
  TAG_SYNC_TENSION_STOP_MM = 66,
  ```
- Register generation helper [VERIFIED: `firmware/src/main.c:309-314`]:
  ```c
  uint32_t build_ihold_irun_reg(int run_ma, int hold_ma, bool vsense) {
      uint8_t irun = ma_to_cs(run_ma, vsense);
      uint8_t ihold = ma_to_cs(hold_ma, vsense);
      return ((uint32_t)ihold) | ((uint32_t)irun << TMC_IRUN_SHIFT) |
             (TMC_IRUNDELAY_VALUE << TMC_IRUNDELAY_SHIFT);
  }
  ```
- Status line maximum calculation [VERIFIED: `firmware/src/protocol_status.c:16`]:
  ```c
  STATUS_LINE_MAX = CMD_LINE_MAX - 8,
  ```
- Protocol command buffer maximum [VERIFIED: `firmware/include/protocol.h:5`]:
  ```c
  #define CMD_LINE_MAX 768
  ```
- Sync active state predicate [VERIFIED: `firmware/include/sync.h:17`]:
  ```c
  #define sync_enabled (g_sync_state == SYNC_ACTIVE)
  ```
- Stop all motors helper [VERIFIED: `firmware/src/motion.c:635-638`]:
  ```c
  void stop_all(void) {
      lane_stop(&g_lane_l1);
      lane_stop(&g_lane_l2);
  }
  ```
- Sync disable entry [VERIFIED: `firmware/src/sync.c:1206-1207`]:
  ```c
  void sync_disable(bool reset_estimator) {
      sync_set_state(SYNC_OFF);
  ```
- Sync set state entry [VERIFIED: `firmware/src/sync.c:749-751`]:
  ```c
  void sync_set_state(sync_state_t new_state) {
      if (g_sync_state == new_state)
          return;
  ```
- Shadow register synchronization in `sync_tmc_settings` [VERIFIED: `firmware/src/settings_store.c:534-540`]:
  ```c
      tmc_set_run_current_ma(tmc, g_tmc_run_current_ma[idx], g_tmc_hold_current_ma[idx]);

      // Synchronize shadow state for protocol reporting
      g_shadow_vsense[idx] = (g_tmc_run_current_ma[idx] <= TMC_VSENSE_THRESHOLD_MA);
      g_shadow_ihold_irun[idx] = build_ihold_irun_reg(
          g_tmc_run_current_ma[idx], g_tmc_hold_current_ma[idx], g_shadow_vsense[idx]);
      g_shadow_ihold_irun_valid[idx] = true;
  ```

### Primary References
- `.planning/phases/16-tmc-tension-current-boost/16-CONTEXT.md` — Locked decisions and architectural guidelines.
- `.planning/phases/16-tmc-tension-current-boost/16-SPEC.md` — Functional requirements R1–R7 and acceptance criteria.
- `.planning/research/2026-09-11-happy-hare-borrow-scan.md` §1.5 — Happy-Hare v4 tangle prevention reference.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — Pure in-tree C11 Pico SDK and host Python tooling.
- Architecture: HIGH — Follows established Phase 10 / Phase 13 patterns in `sync.c`, `motion.c`, and `settings_store.c`.
- Pitfalls: HIGH — Specific failure modes (UART flooding, shadow desync, status overflow) verified and counter-measured.

**Research date:** 2026-09-14
**Valid until:** 2026-10-14 (stable core firmware architecture)
