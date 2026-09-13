# Phase 16: TMC Tension Current Boost — Specification

**Created:** 2026-09-13
**Ambiguity score:** 0.07 (gate: ≤ 0.20)
**Requirements:** 7 locked

## Goal

Automatically boost gear stepper motor run current (`IRUN`) to a dedicated safe limit during Type-P buffer tension spikes in active sync to prevent spool tangles, restoring base current via hysteresis or upon any sync exit.

## Background

When feeding filament during active printing sync, spool friction, tangles, or drag can pull the Type-P buffer into severe tension (negative travel). FLARE's stepper drivers currently run at a single fixed `g_tmc_run_current_ma[lane]` across all operations. If a spool starts to stick, the motor may skip steps or stall prematurely unless extra torque is applied.

Happy-Hare v4 implements a tangle prevention mechanism (`_check_tangle_prevention`) that raises gear current to 100% when tension exceeds a threshold and drops back to a reduced running current once tension subsides. Phase 16 brings this capability to FLARE: during active sync (`SYNC_ACTIVE`), when a Type-P buffer detects tension pegging (`g_buf_pos <= SYNC_TENSION_BOOST_ON`), gear run current temporarily increases to `SYNC_TENSION_BOOST_IRUN`. Once tension eases past `SYNC_TENSION_BOOST_OFF`, baseline run current is restored.

To preserve firmware reliability and hardware safety:
- Stepper motor current is clamped at a safe ceiling (1200 mA max).
- Current is restored unconditionally on any exit from active sync (pause, stop, fault, toolchange, lane switch).
- Writes to TMC drivers only execute on state-transition edges to avoid saturating the UART bus.
- Heartbeat shadow registers (`g_shadow_ihold_irun`) remain synchronized to prevent brownout recovery from clobbering active boost.
- If tension cannot be resolved despite boost, Phase 13 distance (`SYNC_TENSION_STOP_MM`) and dwell (`SYNC_TENSION_DWELL_STOP_MS`) fault trips halt motion cleanly.

## Requirements

1. **Tension Boost Activation**: Motor run current raises to `SYNC_TENSION_BOOST_IRUN` when Type-P buffer enters deep tension during active sync.
   - Current: Stepper run current is static at `g_tmc_run_current_ma[lane]`.
   - Target: When in `SYNC_ACTIVE` with Type-P buffer, if `g_buf_pos <= g_sync_tension_boost_on` (default -0.50), the active lane's TMC run current increases to `SYNC_TENSION_BOOST_IRUN[lane]` (clamped at 1200 mA max).
   - Acceptance: In `SYNC_ACTIVE`, driving buffer position to ≤ `SYNC_TENSION_BOOST_ON` updates TMC `IHOLD_IRUN` register with boosted CS scale and marks boost state active.

2. **Hysteresis Release**: Motor run current restores to baseline when buffer tension relieves past release threshold.
   - Current: No dynamic current reduction exists.
   - Target: When boost is active and buffer position rises to `g_buf_pos >= g_sync_tension_boost_off` (default -0.30), motor current restores to base `g_tmc_run_current_ma[lane]`.
   - Acceptance: With boost active, moving buffer position to ≥ `SYNC_TENSION_BOOST_OFF` restores base run current via TMC `IHOLD_IRUN` write and marks boost state inactive.

3. **Unconditional State Reset**: Any exit from active sync immediately disengages boost and restores base current.
   - Current: No boost state to unwind.
   - Target: Transitioning out of `SYNC_ACTIVE` (via `STOP`, `PA`, `sync_disable()`, fault trip, toolchange `TC:`, lane switch, or motor disable) immediately resets boost flag and restores base run current.
   - Acceptance: Triggering `STOP`, `PA`, toolchange, or fault while boost is active immediately writes base `IHOLD_IRUN` to the TMC driver; status reflects non-boosted state.

4. **Persistent Tangle Escalation**: Unrelieved tension defers to existing fault trips without getting stuck in indefinite boost.
   - Current: Phase 13 trips `FAULT_HOLD` on pegged tension distance (`SYNC_TENSION_STOP_MM`) or dwell (`SYNC_TENSION_DWELL_STOP_MS`).
   - Target: If boosted current cannot pull filament free, Phase 13 distance and dwell limits trigger `FAULT_HOLD`, stop motors via `stop_all()`, and restore base run current.
   - Acceptance: Holding buffer at tension rail during active sync causes Phase 13 distance or dwell fault trip to trigger, stopping motion and restoring base current.

5. **Telemetry & Event Visibility**: Host and operator have real-time visibility into boost state via status dumps and transition events.
   - Current: Status `ST:` line does not report boost state; no boost events exist.
   - Target: `ST:` output includes tension boost indicator (e.g. `TB:0` or `TB:1` within character budget); transitions emit `EV:TMC:BOOST:<lane>` and `EV:TMC:NORMAL:<lane>`.
   - Acceptance: Transition into boost emits `EV:TMC:BOOST:<lane>` and `ST:` reports `TB:1`; transition out of boost emits `EV:TMC:NORMAL:<lane>` and `ST:` reports `TB:0`.

6. **Shadow Register & Heartbeat Integrity**: TMC heartbeat recovery and register shadow state stay synchronized with active boost current.
   - Current: Heartbeat uses `g_shadow_ihold_irun` to verify driver state during idle.
   - Target: Boost state changes update `g_shadow_ihold_irun[idx]` so any TMC heartbeat check or recovery re-application applies the true active current without desync or clobbering boost.
   - Acceptance: Simulating a driver brownout recovery during boost restores boosted `IHOLD_IRUN`; recovery after boost release restores base `IHOLD_IRUN`.

7. **Configuration & Protocol Parity**: Boost parameters are configurable via `config.ini`, runtime `SET:` / `GET:`, and dumped by `flare_cmd.py --dump`.
   - Current: No tension boost parameters exist in settings.
   - Target: `SYNC_TENSION_BOOST_IRUN` (per-lane mA, default 0/disabled), `SYNC_TENSION_BOOST_ON` (float, default -0.50), and `SYNC_TENSION_BOOST_OFF` (float, default -0.30) supported across config, flash persistence, SET/GET, and dump.
   - Acceptance: Setting `SYNC_TENSION_BOOST_IRUN` via `SET:` persists, matches `GET:`, and `SET:` rejects invalid ordering (`BOOST_ON >= BOOST_OFF`) or over-current (> 1200 mA) with `ER:INVALID_PARAM`.

## Boundaries

**In scope:**
- Type-P analog buffer tension boost during `SYNC_ACTIVE` only.
- Per-lane boost current target `SYNC_TENSION_BOOST_IRUN` in mA (clamped to 1200 mA ceiling).
- Hysteresis thresholds `SYNC_TENSION_BOOST_ON` (on) and `SYNC_TENSION_BOOST_OFF` (off).
- Edge-triggered UART writes to TMC `IHOLD_IRUN` register and shadow state synchronization.
- Unconditional reset of boost on all exits from `SYNC_ACTIVE`.
- Telemetry integration: `ST:` status token and `EV:TMC:BOOST`/`EV:TMC:NORMAL` events.
- Configuration in `config.ini`, `scripts/gen_config.py`, SET/GET commands, `--dump`, and host sim tests.

**Out of scope:**
- Type-D microswitch buffer boost — Type-D cannot measure proportional tension severity.
- Extruder motor boost — FLARE does not control the toolhead extruder motor.
- Non-sync operations — Preload, load, unload, cutting, and toolhead parking run at standard currents.
- Thermal motor modeling — Motor thermal protection is enforced via conservative hard mA ceiling (1200 mA) and existing Phase 13 fault timeouts.

## Constraints

- **UART Bus Conservation**: Motor current changes MUST only transmit over UART on hysteresis threshold crossing edges. Continuous re-writes per loop tick are forbidden.
- **Hardware Safety Ceiling**: Boost current is hard-capped at 1200 mA regardless of user configuration to protect stepper coils and TMC2209 thermal limits.
- **Strict Inactive Reset**: Boost MUST NOT persist when motion stops or sync exits; failure to de-boost is treated as a safety defect.
- **Phase 10 Heartbeat Parity**: Boost writes must maintain `g_shadow_ihold_irun` consistency so idle heartbeat verification does not detect spurious register mismatches.
- **Status Character Budget**: Adding `TB:` to the `ST:` response must respect the serial response line budget.

## Acceptance Criteria

- [ ] Driving Type-P buffer position ≤ `SYNC_TENSION_BOOST_ON` during `SYNC_ACTIVE` boosts active lane `IHOLD_IRUN` to configured `SYNC_TENSION_BOOST_IRUN`
- [ ] Driving Type-P buffer position ≥ `SYNC_TENSION_BOOST_OFF` restores active lane `IHOLD_IRUN` to base run current
- [ ] Transitioning into boost emits `EV:TMC:BOOST:<lane>` and updates `ST:` status to `TB:1`
- [ ] Transitioning out of boost emits `EV:TMC:NORMAL:<lane>` and updates `ST:` status to `TB:0`
- [ ] Exiting `SYNC_ACTIVE` via `STOP`, `PA`, `sync_disable()`, or lane switch immediately restores base run current
- [ ] Phase 13 tension distance (`SYNC_TENSION_STOP_MM`) or dwell (`SYNC_TENSION_DWELL_STOP_MS`) fault halts motion and restores base current
- [ ] `SET:SYNC_TENSION_BOOST_IRUN` clamps at 1200 mA; `SET` commands reject `BOOST_ON >= BOOST_OFF` with `ER:INVALID_PARAM`
- [ ] Setting `SYNC_TENSION_BOOST_IRUN = 0` disables boost (no UART writes, no events, `TB:0`)
- [ ] Type-D buffer setups never trigger tension boost under any state
- [ ] TMC idle heartbeat recovery restores boosted current if recovered while boost is active, and base current if recovered while boost is inactive
- [ ] Host simulation (`flare_sim`) asserts boost activation, hysteresis release, and unconditional de-boost across all exit paths

## Edge Coverage

**Coverage:** 11/11 applicable edges resolved · 0 unresolved

| Category | Requirement | Status | Resolution / Reason |
|----------|-------------|--------|---------------------|
| boundary | R1 | ✅ covered | Explicit check: boost triggers exactly when `g_buf_pos <= SYNC_TENSION_BOOST_ON`; one tick above remains baseline. |
| precision | R1 | ✅ covered | Explicit check: `SYNC_TENSION_BOOST_IRUN` mA converts to TMC current scale (CS 0..31) clamped at 1200 mA max. |
| boundary | R2 | ✅ covered | Explicit check: boost releases exactly when `g_buf_pos >= SYNC_TENSION_BOOST_OFF`; one tick below remains boosted. |
| precision | R2 | ✅ covered | Explicit check: `BOOST_ON < BOOST_OFF` invariant enforced; hysteresis band cannot be inverted or zero. |
| unclassified | R3 | ✅ covered | Explicit check: all exit paths (`STOP`, `PA`, fault, toolchange, disable) call unconditional boost disengage and base current write. |
| unclassified | R4 | ✅ covered | Explicit check: persistent tension delegates to Phase 13 distance/dwell fault trips which execute `stop_all()` and base current restore. |
| unclassified | R5 | ✅ covered | Explicit check: status line character budget verified for `TB:0`/`TB:1`; events emit once per edge. |
| unclassified | R6 | ✅ covered | Explicit check: `g_shadow_ihold_irun` updated atomically with boost writes so heartbeat re-apply restores correct active current. |
| adjacency | R7 | ✅ covered | Explicit check: `BOOST_ON == BOOST_OFF` rejected with `ER:INVALID_PARAM`; `BOOST_ON` must be strictly more negative than `BOOST_OFF`. |
| empty | R7 | ✅ covered | Explicit check: `SYNC_TENSION_BOOST_IRUN = 0` (or <= base current) disables feature completely (no UART writes, `TB:0`). |
| ordering | R7 | ✅ covered | Explicit check: SET commands validate threshold relationship regardless of which parameter is configured first. |

## Prohibitions (must-NOT)

**Coverage:** 5/5 applicable prohibitions resolved · 0 unresolved

| Prohibition (must-NOT statement) | Requirement | Status | Verification / Reason |
|----------------------------------|-------------|--------|------------------------|
| MUST NOT engage boost when buffer position is in compression (> 0.0) or neutral | R1 | resolved | verification: test (negative unit test in `flare_sim`) |
| MUST NOT leave motor in boosted current state when active sync exits, pauses, stops, or faults | R3 | resolved | verification: test (exit path assertions in `flare_sim`) |
| MUST NOT activate boost on Type-D (microswitch) sensor configurations | R1 | resolved | verification: test (Type-D sim assertion) |
| MUST NOT allow boost current to exceed 1200 mA hardware safety ceiling | R1 | resolved | verification: test (parameter clamping test) |
| MUST NOT transmit TMC register writes on every control loop tick | R1, R2 | resolved | verification: test (UART transaction count assertion in sim) |

## Ambiguity Report

| Dimension          | Score | Min  | Status | Notes                              |
|--------------------|-------|------|--------|------------------------------------|
| Goal Clarity       | 0.95  | 0.75 | ✓      | Specific behavior and activation conditions defined |
| Boundary Clarity   | 0.95  | 0.70 | ✓      | Strict Type-P active sync boundary, out-of-scope list |
| Constraint Clarity | 0.90  | 0.65 | ✓      | 1200 mA clamp, edge-only UART writes, shadow sync |
| Acceptance Criteria| 0.90  | 0.70 | ✓      | 11 falsifiable pass/fail checkboxes |
| **Ambiguity**      | 0.07  | ≤0.20| ✓      | Gate passed                        |

Status: ✓ = met minimum, ⚠ = below minimum (planner treats as assumption)

## Interview Log

| Round | Perspective     | Question summary                                   | Decision locked                                                                 |
|-------|-----------------|----------------------------------------------------|---------------------------------------------------------------------------------|
| 1     | Researcher      | Motor current profile between normal & boost       | Standard run current during normal sync; boost raises to dedicated boost mA setting |
| 1     | Researcher      | Eligible sensor types and operating states         | Type-P only during active sync (`SYNC_ACTIVE`); disabled by default until tuned  |
| 1     | Researcher      | Persistent tangle / jam escalation behavior        | Defer to Phase 13 distance (`STOP_MM`) and dwell (`DWELL_MS`) fault trip to stop & restore |
| 2     | Simplifier      | Gate check vs more questions                       | Refine threshold defaults and telemetry visibility in Round 2                   |
| 2     | Simplifier      | Telemetry and status visibility                    | `ST:` status reports `TB:0`/`TB:1`; `EV:TMC:BOOST` / `EV:TMC:NORMAL` emit on edges |
| 2     | Boundary Keeper | Threshold definition & defaults                    | Fixed position thresholds: `BOOST_ON = -0.50`, `BOOST_OFF = -0.30`             |
| 2     | Boundary Keeper | Current configuration format & ceiling             | Absolute mA (`SYNC_TENSION_BOOST_IRUN`) per lane, hard-clamped at 1200 mA max  |
| 2     | Failure Analyst | State exit de-escalation                           | Immediate unconditional reset to base current on ANY sync exit                 |
| Edge  | Edge Probe      | Invalid thresholds & zero boost behavior           | Strict validation (`BOOST_ON < BOOST_OFF`), 0 disables feature                  |
| Edge  | Prohibitions    | Safety prohibitions (must-NOT guardrails)          | Locked 5 prohibitions: no compression boost, no boost leakage, no Type-D, max 1200mA, edge-only UART |

---

*Phase: 16-tmc-tension-current-boost*
*Spec created: 2026-09-13*
*Next step: /gsd-discuss-phase 16 — implementation decisions (how to build what's specified above)*
